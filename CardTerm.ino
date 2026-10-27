// CardTerm - a little terminal dashboard for the M5Stack Cardputer / Cardputer ADV
//   weather + what to wear, sun & moon times, sudoku
//
// Keys everywhere:  ; up   . down   , left   / right   enter ok   ` back
#include <M5Cardputer.h>
#include <WiFi.h>
#include "astro.h"
#include "settings.h"
#include "ui.h"
#include "weather.h"

void runSky();
void runSudoku();
String sudokuStatus();

// 16 KB instead of the default 8 KB, for headroom (HTTPS + JSON + UI)
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static const char* VERSION = "1.1";

static void bootLine(int& y, const char* what, int status, const char* detail = "") {
  // status: 0 ok, 1 fail, 2 skip, 3 pending
  const char* tag[] = {"[ ok ]", "[fail]", "[skip]", "[ .. ]"};
  uint16_t c[] = {col::fg, col::red, col::dim, col::amber};
  cv.fillRect(0, y, W, 9, col::bg);
  txt(2, y, tag[status], c[status]);
  txt(44, y, what, col::white);
  txt(44 + (strlen(what) + 1) * 6, y, detail, col::dim);
  present();
  if (status != 3) y += 10;
}

static void boot() {
  clearBody();
  txtf(2, 2, col::amber, SMALL, "CardTerm v%s", VERSION);
  int y = 16;
  bool adv = M5.getBoard() == m5::board_t::board_M5CardputerADV;
  bootLine(y, "keyboard", 0, adv ? "tca8418 (ADV)" : "io matrix");
  bootLine(y, "display", 0, "240x135 st7789");
  bootLine(y, "config", 0, S.place.c_str());
  if (S.ssid.isEmpty()) {
    bootLine(y, "wifi", 2, "not set up yet");
  } else {
    bootLine(y, "wifi", 3, S.ssid.c_str());
    bool ok = wifiConnect(9000);
    bootLine(y, "wifi", ok ? 0 : 1, ok ? WiFi.localIP().toString().c_str() : S.ssid.c_str());
    if (ok) {
      bootLine(y, "clock", 3, "ntp");
      bool t = syncTime(6000);
      bootLine(y, "clock", t ? 0 : 1, t ? "synced" : "ntp timeout");
      bootLine(y, "weather", 3, "open-meteo");
      String err;
      bool w = weatherFetch(err);
      bootLine(y, "weather", w ? 0 : 1, w ? weatherSummary().c_str() : err.c_str());
    }
  }
  if (wifiOk()) {
    wifiOff();
    bootLine(y, "wifi", 0, "radio off (saves battery)");
  }
  txt(2, y + 4, "ready.", col::fg);
  present();
  delay(600);
}

static String skyHint() {
  if (!timeValid()) return "clock not set";
  double now = time(nullptr);
  double dayStart = floor((now + S.tzOff) / 86400.0) * 86400.0 - S.tzOff;
  astro::SunDay s = astro::sunDay(dayStart, S.lat, S.lon);
  char b[8];
  String out;
  if (s.rise.ok && now < s.rise.t) { fmtHM(b, (time_t)s.rise.t + S.tzOff); out = String("rise ") + b; }
  else if (s.set.ok && now < s.set.t) { fmtHM(b, (time_t)s.set.t + S.tzOff); out = String("set ") + b; }
  else {
    astro::SunDay t = astro::sunDay(dayStart + 86400, S.lat, S.lon);
    if (t.rise.ok) { fmtHM(b, (time_t)t.rise.t + S.tzOff); out = String("rise ") + b; }
  }
  astro::MoonInfo m = astro::moonInfo(now);
  out += " moon " + String((int)lround(m.fraction * 100)) + "%";
  return out;
}

static void dashboard() {
  static const char* items[] = {"weather", "sun+moon", "sudoku", "settings"};
  static const char* cmds[] = {"./weather --wear", "./sky --today", "./sudoku", "./settings"};
  int sel = 0;
  bool dirty = true;
  uint32_t lastDraw = 0;
  String hints[4];
  uint32_t hintsAt = 0;
  for (;;) {
    if (millis() - hintsAt > 15000 || hintsAt == 0) {
      hints[0] = WX.ok ? weatherSummary() : (S.ssid.isEmpty() ? "needs wifi" : "enter to fetch");
      hints[1] = skyHint();
      hints[2] = sudokuStatus();
      hints[3] = S.ssid.isEmpty() ? "set up wifi!" : "wifi: " + S.ssid.substring(0, 12);
      hintsAt = millis();
    }
    if (dirty || millis() - lastDraw > 400) {
      clearBody();
      header("~");
      // big clock
      char clk[8] = "--:--";
      if (timeValid()) fmtHM(clk, nowLocal());
      cv.setFont(&fonts::AsciiFont8x16);
      cv.setTextSize(2);
      cv.setTextColor(col::fg);
      cv.drawString(clk, 4, 14);
      cv.setTextSize(1);
      if (timeValid()) {
        time_t lt = nowLocal();
        struct tm tmv;
        gmtime_r(&lt, &tmv);
        static const char* WD[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                   "Thursday", "Friday", "Saturday"};
        static const char* MO[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        txt(94, 17, WD[tmv.tm_wday], col::white);
        txtf(94, 27, col::dim, SMALL, "%d %s %d", tmv.tm_mday, MO[tmv.tm_mon], tmv.tm_year + 1900);
      } else {
        txt(94, 17, "clock not set", col::amber);
        txt(94, 27, "settings > wifi", col::dim);
      }
      String pl = S.place.substring(0, 16);
      txt(94, 37, pl.c_str(), col::dim);

      for (int i = 0; i < 4; i++) {
        int y = 50 + i * 15;
        bool s = i == sel;
        if (s) cv.fillRect(0, y - 1, W, 15, col::sel);
        txtf(4, y, s ? col::amber : col::dim, SMALL, "%d", i + 1);
        txt(16, y - 4 + 3, items[i], s ? col::white : col::fg, BIG);
        String h = hints[i].substring(0, 21);
        txtR(W - 3, y + 3, h.c_str(), s ? col::fg : col::dim);
      }
      // prompt line
      int py = 112;
      String cmd = cmds[sel];
      if (sel == 0 && WX.ok) cmd = "wear: " + outfitHeadline();
      cmd = cmd.substring(0, 36);
      txt(2, py, "$", col::amber);
      txt(12, py, cmd.c_str(), sel == 0 && WX.ok ? col::amber : col::fg);
      if (blink()) cv.fillRect(12 + cmd.length() * 6 + 2, py, 5, 8, col::fg);
      footer("[;.] move  [1-4/ent] open");
      present();
      dirty = false;
      lastDraw = millis();
    }
    Key k = pollKey();
    if (!k) { delay(15); continue; }
    dirty = true;
    int open = -1;
    if (k.code == K_UP) sel = (sel + 3) % 4;
    else if (k.code == K_DOWN) sel = (sel + 1) % 4;
    else if (k.code == K_ENTER || k.code == K_RIGHT) open = sel;
    else if (k.ch >= '1' && k.ch <= '4') open = sel = k.ch - '1';
    else if (k.ch == 'w') open = sel = 0;
    else if (k.ch == 's') open = sel = 1;
    else if (k.ch == 'p' || k.ch == 'k') open = sel = 2;
    switch (open) {
      case 0: runWeather(); break;
      case 1: runSky(); break;
      case 2: runSudoku(); break;
      case 3: runSettings(); break;
    }
    if (open >= 0) hintsAt = 0;
  }
}

void setup() {
  Serial.begin(115200);
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  Serial.printf("[cardterm] started, board id %d (ADV = %d)\n", (int)M5.getBoard(),
                (int)m5::board_t::board_M5CardputerADV);
  // draw straight to the display first, so a blank screen means the
  // display itself isn't starting, not our code
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(128);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setTextColor(TFT_GREEN);
  M5Cardputer.Display.drawString("CardTerm starting...", 4, 4);
  loadSettings();
  Serial.println("[cardterm] settings loaded");
  uiBegin();
  Serial.println("[cardterm] screen buffer ready");
  WiFi.setAutoReconnect(true);
  boot();
  Serial.println("[cardterm] boot done, dashboard");
}

void loop() { dashboard(); }
