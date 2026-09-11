#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Keypad.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>

#define TFT_SCLK 9
#define TFT_MOSI 10
#define TFT_DC   6
#define TFT_CS   5
#define TFT_RST  -1
#define BUZZER   21

class MyST7789 : public Adafruit_ST7789 {
public:
  MyST7789(int8_t cs, int8_t dc, int8_t mosi, int8_t sclk, int8_t rst)
    : Adafruit_ST7789(cs, dc, mosi, sclk, rst) {}
  void setOffsets(uint8_t col, uint8_t row) { _colstart=_colstart2=col; _rowstart=_rowstart2=row; }
};
MyST7789 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

const byte ROWS=3, COLS=3;
char keys[ROWS][COLS] = {{'1','2','3'},{'4','5','6'},{'7','8','9'}};
byte rowPins[ROWS]={8,7,4};
byte colPins[COLS]={3,2,1};
Keypad keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

Preferences prefs;

struct Alarm {
  uint8_t hour=7, minute=0;
  bool enabled=false;
  uint8_t numQuestions=3;
  uint8_t snoozeMin=5;
  uint8_t difficulty=1;
};
#define MAX_ALARMS 5
Alarm alarms[MAX_ALARMS];

struct ColorOpt { const char* name; uint16_t val; };
ColorOpt palette[] = {
  {"White", ST77XX_WHITE}, {"Red", ST77XX_RED}, {"Green", ST77XX_GREEN},
  {"Blue", ST77XX_BLUE}, {"Yellow", ST77XX_YELLOW}, {"Cyan", ST77XX_CYAN},
  {"Magenta", ST77XX_MAGENTA}, {"Black", ST77XX_BLACK}
};
const int NUM_COLORS = 8;

const GFXfont* fonts[] = { nullptr, &FreeSans9pt7b, &FreeSerif9pt7b };
const char* fontNames[] = { "Default", "Sans", "Serif" };
const int NUM_FONTS = 3;

struct TZOpt { const char* name; const char* posix; };
TZOpt tzList[] = {
  {"UTC", "UTC0"},
  {"US Eastern", "EST5EDT,M3.2.0,M11.1.0"},
  {"US Pacific", "PST8PDT,M3.2.0,M11.1.0"},
  {"UK", "GMT0BST,M3.5.0/1,M10.5.0"},
  {"Central EU", "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"India", "IST-5:30"},
  {"Singapore", "SGT-8"}
};
const int NUM_TZ = 7;

struct Settings {
  uint8_t fgIdx=0, bgIdx=7;
  bool format24h=true;
  uint8_t fontIdx=0;
  uint8_t tzIdx=0;
} settings;

void loadall() {
  prefs.begin("alarmclock", true);
  prefs.getBytes("alarms", alarms, sizeof(alarms));
  prefs.getBytes("settings", &settings, sizeof(settings));
  prefs.end();
}
void saveall() {
  prefs.begin("alarmclock", false);
  prefs.putBytes("alarms", alarms, sizeof(alarms));
  prefs.putBytes("settings", &settings, sizeof(settings));
  prefs.end();
}

enum Screen { TIME_DATE, ALARM_LIST, ALARM_EDIT, SETTINGS_SCR, ALARM_RINGING, ALARM_PROMPT };
Screen screen = TIME_DATE;

int listIndex = 0;
int editIndex = -1;
int editField = 0;
int ringingAlarm = -1;
int questionsLeft = 0;
String typedAnswer = "";

bool snoozeActive = false;
unsigned long snoozeUntil = 0;
int snoozeAlarmIndex = -1;

const char* alarmEditFields[] = {"Hour","Minute","Enabled","#Questions","Snooze(min)","Difficulty"};
const int NUM_ALARM_FIELDS = 6;
const char* settingsFields[] = {"Text color","BG color","Format 24h","Font","Timezone"};
const int NUM_SETTINGS_FIELDS = 5;

const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASS";

enum WifiSyncState { WIFI_IDLE, WIFI_CONNECTING };
WifiSyncState wifiState = WIFI_IDLE;
unsigned long wifiAttemptStart = 0;
unsigned long lastSyncAttempt = 0;
const unsigned long SYNC_INTERVAL = 6UL * 60 * 60 * 1000;
const unsigned long WIFI_TIMEOUT = 8000;
bool timeSynced = false;

void applytimezone() {
  configTzTime(tzList[settings.tzIdx].posix, "pool.ntp.org", "time.nist.gov");
}

void startsync() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiAttemptStart = millis();
  wifiState = WIFI_CONNECTING;
}

void updatesync() {
  if (wifiState == WIFI_IDLE) {
    if (!timeSynced || millis() - lastSyncAttempt > SYNC_INTERVAL) startsync();
    return;
  }
  if (wifiState == WIFI_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      applytimezone();
      timeSynced = true;
      WiFi.disconnect(true);
      lastSyncAttempt = millis();
      wifiState = WIFI_IDLE;
    } else if (millis() - wifiAttemptStart > WIFI_TIMEOUT) {
      WiFi.disconnect(true);
      lastSyncAttempt = millis();
      wifiState = WIFI_IDLE;
    }
  }
}

void clearscr() {
  tft.fillScreen(palette[settings.bgIdx].val);
  tft.setCursor(0,0);
  tft.setTextColor(palette[settings.fgIdx].val);
  tft.setFont(fonts[settings.fontIdx]);
}

void drawwifiindicate() {
  uint16_t c;
  if (wifiState == WIFI_CONNECTING) c = ST77XX_YELLOW;
  else if (WiFi.status() == WL_CONNECTED) c = ST77XX_GREEN;
  else c = ST77XX_RED;
  tft.fillCircle(tft.width()-8, 8, 4, c);
}

void drawtimedate() {
  clearscr();
  struct tm t;
  tft.setTextSize(fonts[settings.fontIdx] ? 1 : 3);
  if (getLocalTime(&t)) {
    char buf[16];
    strftime(buf, sizeof(buf), settings.format24h ? "%H:%M:%S" : "%I:%M:%S", &t);
    tft.println(buf);
    tft.setTextSize(fonts[settings.fontIdx] ? 1 : 2);
    strftime(buf, sizeof(buf), "%a %d %b %Y", &t);
    tft.println(buf);
  } else {
    tft.println("No time sync");
  }
  if (snoozeActive) {
    tft.setTextSize(1);
    long remain = (snoozeUntil - millis())/1000;
    if (remain < 0) remain = 0;
    tft.print("Snoozed: "); tft.print(remain); tft.println("s");
  }
}

void drawalarmlist() {
  clearscr();
  tft.setTextSize(fonts[settings.fontIdx] ? 1 : 2);
  for (int i=0;i<MAX_ALARMS;i++) {
    tft.setCursor(0, i*20);
    tft.print(i==listIndex ? "> " : "  ");
    char buf[20];
    snprintf(buf, sizeof(buf), "%02d:%02d %s", alarms[i].hour, alarms[i].minute, alarms[i].enabled?"ON":"off");
    tft.println(buf);
  }
}

void drawalarmedit() {
  clearscr();
  Alarm &a = alarms[editIndex];
  tft.setTextSize(fonts[settings.fontIdx] ? 1 : 2);
  for (int i=0;i<NUM_ALARM_FIELDS;i++) {
    tft.setCursor(0, i*20);
    tft.print(i==editField ? "> " : "  ");
    tft.print(alarmEditFields[i]); tft.print(": ");
    switch(i) {
      case 0: tft.println(a.hour); break;
      case 1: tft.println(a.minute); break;
      case 2: tft.println(a.enabled?"ON":"OFF"); break;
      case 3: tft.println(a.numQuestions); break;
      case 4: tft.println(a.snoozeMin); break;
      case 5: tft.println(a.difficulty); break;
    }
  }
}

void drawsettings() {
  clearscr();
  tft.setTextSize(fonts[settings.fontIdx] ? 1 : 2);
  for (int i=0;i<NUM_SETTINGS_FIELDS;i++) {
    tft.setCursor(0, i*20);
    tft.print(i==listIndex ? "> " : "  ");
    tft.print(settingsFields[i]); tft.print(": ");
    switch(i) {
      case 0: tft.println(palette[settings.fgIdx].name); break;
      case 1: tft.println(palette[settings.bgIdx].name); break;
      case 2: tft.println(settings.format24h ? "24h" : "12h"); break;
      case 3: tft.println(fontNames[settings.fontIdx]); break;
      case 4: tft.println(tzList[settings.tzIdx].name); break;
    }
  }
}

int genop(int difficulty) {
  int maxVal = 1; for (int i=0;i<difficulty;i++) maxVal*=10;
  return random(1, maxVal);
}

int curA, curB; char curOp;

void newmath() {
  Alarm &al = alarms[ringingAlarm];
  curA = genop(al.difficulty);
  curB = genop(al.difficulty);
  const char ops[] = {'+','-','*'};
  curOp = ops[random(0,3)];
  if (curOp=='-' && curB>curA) { int t=curA; curA=curB; curB=t; }
}

void drawmath() {
  clearscr();
  tft.setTextSize(fonts[settings.fontIdx] ? 1 : 2);
  tft.print("Questions left: "); tft.println(questionsLeft);
  tft.setTextSize(fonts[settings.fontIdx] ? 1 : 3);
  tft.setCursor(0,40);
  tft.print(curA); tft.print(' '); tft.print(curOp); tft.print(' '); tft.print(curB); tft.println(" =");
  tft.setCursor(0,80);
  tft.println(typedAnswer);
}

void drawprompt() {
  clearscr();
  tft.setTextSize(2);
  tft.println("Solved!");
  tft.println("# = Stop");
  tft.println("* = Snooze");
}

int computeanswer() {
  switch(curOp) { case '+': return curA+curB; case '-': return curA-curB; case '*': return curA*curB; }
  return 0;
}

void drawscreen() {
  switch(screen) {
    case TIME_DATE: drawtimedate(); break;
    case ALARM_LIST: drawalarmlist(); break;
    case ALARM_EDIT: drawalarmedit(); break;
    case SETTINGS_SCR: drawsettings(); break;
    case ALARM_RINGING: drawmath(); break;
    case ALARM_PROMPT: drawprompt(); break;
  }
  drawwifiindicate();
}

void checkalarm() {
  if (snoozeActive) {
    if ((long)(millis() - snoozeUntil) >= 0) {
      snoozeActive = false;
      ringingAlarm = snoozeAlarmIndex;
      questionsLeft = alarms[ringingAlarm].numQuestions;
      newmath();
      typedAnswer = "";
      screen = ALARM_RINGING;
      digitalWrite(BUZZER, HIGH);
      drawscreen();
    }
    return;
  }
  if (screen==ALARM_RINGING || screen==ALARM_PROMPT) return;

  struct tm t;
  if (!getLocalTime(&t)) return;
  static int lastMinuteChecked = -1;
  if (t.tm_min == lastMinuteChecked) return;
  lastMinuteChecked = t.tm_min;
  for (int i=0;i<MAX_ALARMS;i++) {
    if (alarms[i].enabled && alarms[i].hour==t.tm_hour && alarms[i].minute==t.tm_min) {
      ringingAlarm = i;
      questionsLeft = alarms[i].numQuestions;
      newmath();
      typedAnswer = "";
      screen = ALARM_RINGING;
      digitalWrite(BUZZER, HIGH);
      drawscreen();
      return;
    }
  }
}

void handletimedate(char k) {
  if (k=='9') { screen=ALARM_LIST; listIndex=0; drawscreen(); }
}

void handlealarmlist(char k) {
  if (k=='2') listIndex=(listIndex+MAX_ALARMS-1)%MAX_ALARMS;
  else if (k=='8') listIndex=(listIndex+1)%MAX_ALARMS;
  else if (k=='5') { editIndex=listIndex; editField=0; screen=ALARM_EDIT; }
  else if (k=='9') { screen=SETTINGS_SCR; listIndex=0; }
  else if (k=='*') { screen=TIME_DATE; }
  else return;
  drawscreen();
}

void handlealarmedit(char k) {
  Alarm &a = alarms[editIndex];
  if (k=='2') editField=(editField+NUM_ALARM_FIELDS-1)%NUM_ALARM_FIELDS;
  else if (k=='8') editField=(editField+1)%NUM_ALARM_FIELDS;
  else if (k=='4' || k=='6') {
    int dir = (k=='6') ? 1 : -1;
    switch(editField) {
      case 0: a.hour=(a.hour+24+dir)%24; break;
      case 1: a.minute=(a.minute+60+dir)%60; break;
      case 2: a.enabled=!a.enabled; break;
      case 3: a.numQuestions=constrain((int)a.numQuestions+dir,1,20); break;
      case 4: a.snoozeMin=constrain((int)a.snoozeMin+dir,1,30); break;
      case 5: a.difficulty=constrain((int)a.difficulty+dir,1,3); break;
    }
  }
  else if (k=='9') { saveall(); screen=ALARM_LIST; }
  else if (k=='*') { screen=ALARM_LIST; }
  else return;
  drawscreen();
}

void handlesetting(char k) {
  if (k=='2') listIndex=(listIndex+NUM_SETTINGS_FIELDS-1)%NUM_SETTINGS_FIELDS;
  else if (k=='8') listIndex=(listIndex+1)%NUM_SETTINGS_FIELDS;
  else if (k=='4' || k=='6') {
    int dir = (k=='6') ? 1 : -1;
    switch(listIndex) {
      case 0: settings.fgIdx=(settings.fgIdx+NUM_COLORS+dir)%NUM_COLORS; break;
      case 1: settings.bgIdx=(settings.bgIdx+NUM_COLORS+dir)%NUM_COLORS; break;
      case 2: settings.format24h=!settings.format24h; break;
      case 3: settings.fontIdx=(settings.fontIdx+NUM_FONTS+dir)%NUM_FONTS; break;
      case 4: settings.tzIdx=(settings.tzIdx+NUM_TZ+dir)%NUM_TZ; applytimezone(); break;
    }
  }
  else if (k=='9') { saveall(); screen=TIME_DATE; }
  else if (k=='*') { screen=TIME_DATE; }
  else return;
  drawscreen();
}

void handleringing(char k) {
  if (k>='1' && k<='9') typedAnswer += k;
  else if (k=='*') typedAnswer += '0';
  else if (k=='#') {
    if (typedAnswer.toInt() == computeanswer()) {
      questionsLeft--;
      typedAnswer="";
      if (questionsLeft<=0) {
        digitalWrite(BUZZER, LOW);
        screen = ALARM_PROMPT;
      } else {
        newmath();
      }
    } else {
      typedAnswer="";
    }
  }
  drawscreen();
}

void handleprompt(char k) {
  if (k=='#') {
    screen = TIME_DATE;
  } else if (k=='*') {
    snoozeActive = true;
    snoozeAlarmIndex = ringingAlarm;
    snoozeUntil = millis() + (unsigned long)alarms[ringingAlarm].snoozeMin * 60000UL;
    screen = TIME_DATE;
  } else return;
  drawscreen();
}

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER, OUTPUT); digitalWrite(BUZZER, LOW);
  randomSeed(analogRead(0));

  tft.init(76, 284);
  tft.setOffsets(82, 18);
  tft.invertDisplay(false);
  tft.setRotation(1);

  loadall();
  startsync();
  drawscreen();
}

void loop() {
  updatelync();
  checkalarm();
  char k = keypad.getKey();
  if (k) {
    switch(screen) {
      case TIME_DATE: handletimedate(k); break;
      case ALARM_LIST: handlealarmlist(k); break;
      case ALARM_EDIT: handlealarmedit(k); break;
      case SETTINGS_SCR: handlesetting(k); break;
      case ALARM_RINGING: handleringing(k); break;
      case ALARM_PROMPT: handleprompt(k); break;
    }
  }
  static unsigned long last=0;
  if ((screen==TIME_DATE) && millis()-last>1000) { last=millis(); drawscreen(); }
}