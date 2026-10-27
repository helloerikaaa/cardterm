#include "ui.h"
#include "settings.h"
#include <WiFi.h>
#include <stdarg.h>

// The Cardputer ADV keyboard (TCA8418 chip) needs M5Cardputer 1.1.0 or newer.
#if !__has_include(<utility/Keyboard/KeyboardReader/TCA8418.h>)
#error "Your M5Cardputer library is too old for the Cardputer ADV (its keyboard would not work). Update it to 1.1.0 or newer: Sketch > Include Library > Manage Libraries > search M5Cardputer > pick the newest version > Update."
#endif

// M5Unified learned to recognise the Cardputer ADV in 0.2.23. With an older
// copy the screen is never switched on (you just get a black screen).
#if defined(M5UNIFIED_VERSION_MAJOR) && \
    (M5UNIFIED_VERSION_MAJOR * 10000 + M5UNIFIED_VERSION_MINOR * 100 + M5UNIFIED_VERSION_PATCH) < 223
#error "Your M5Unified library is too old for the Cardputer ADV (black screen). Update it to 0.2.23 or newer: Sketch > Include Library > Manage Libraries > search M5Unified > Update. Update M5GFX too."
#endif

// No parent passed here on purpose: M5Cardputer.Display may not exist yet while
// globals are being built. present() names the display explicitly instead.
M5Canvas cv;

namespace col {
uint16_t bg, fg, dim, faint, bar, amber, cyan, red, white, blue, yellow, grey, sel;
}

static uint32_t lastActivity = 0;
static bool dimmed = false;

void uiBegin() {
  auto& d = M5Cardputer.Display;
  d.setRotation(1);
  d.setBrightness(S.bright);
  cv.setColorDepth(16);
  if (!cv.createSprite(W, H)) {
    Serial.println("[cardterm] ERROR: not enough RAM for screen buffer");
    d.fillScreen(TFT_RED);
    d.drawString("CardTerm: out of memory", 4, 4);
  }
  col::bg = cv.color565(0, 0, 0);
  col::fg = cv.color565(80, 255, 120);     // phosphor green
  col::dim = cv.color565(40, 150, 70);
  col::faint = cv.color565(18, 60, 30);
  col::bar = cv.color565(10, 42, 20);
  col::sel = cv.color565(20, 90, 40);
  col::amber = cv.color565(255, 176, 0);
  col::cyan = cv.color565(90, 220, 255);
  col::red = cv.color565(255, 80, 80);
  col::white = cv.color565(230, 240, 230);
  col::blue = cv.color565(80, 140, 255);
  col::yellow = cv.color565(255, 230, 60);
  col::grey = cv.color565(150, 160, 150);
  cv.setTextWrap(false);
  lastActivity = millis();
}

void present() { cv.pushSprite(&M5Cardputer.Display, 0, 0); }

void font(FontSz f) {
  if (f == SMALL) cv.setFont(&fonts::Font0);
  else cv.setFont(&fonts::AsciiFont8x16);
  cv.setTextSize(1);
}

void txt(int x, int y, const char* s, uint16_t c, FontSz f) {
  font(f);
  cv.setTextColor(c);
  cv.setTextDatum(top_left);
  cv.drawString(s, x, y);
}

void txtf(int x, int y, uint16_t c, FontSz f, const char* fmt, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  txt(x, y, buf, c, f);
}

void txtR(int xr, int y, const char* s, uint16_t c, FontSz f) {
  txt(xr - (int)strlen(s) * fw(f), y, s, c, f);
}

void txtC(int y, const char* s, uint16_t c, FontSz f) {
  txt((W - (int)strlen(s) * fw(f)) / 2, y, s, c, f);
}

void hline(int y, uint16_t c) {
  for (int x = 0; x < W; x += 4) cv.drawFastHLine(x, y, 2, c);
}

bool blink() { return (millis() / 500) % 2 == 0; }

bool timeValid() { return time(nullptr) > 1700000000; }

time_t nowLocal() { return time(nullptr) + S.tzOff; }

void fmtHM(char* buf, time_t t) {
  struct tm tmv;
  gmtime_r(&t, &tmv);
  sprintf(buf, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
}

void header(const char* path) {
  cv.fillRect(0, 0, W, 10, col::bar);
  txt(2, 1, path, col::fg);
  // right side: wifi, battery, clock
  int x = W - 2;
  char clk[8] = "--:--";
  if (timeValid()) fmtHM(clk, nowLocal());
  txtR(x, 1, clk, col::fg);
  x -= 6 * 5 + 6;
  int bat = M5Cardputer.Power.getBatteryLevel();
  if (bat >= 0 && bat <= 100) {
    // tiny battery icon
    int bx = x - 14;
    cv.drawRect(bx, 2, 12, 6, col::dim);
    cv.fillRect(bx + 12, 4, 1, 2, col::dim);
    int w = (bat * 10 + 50) / 100;
    cv.fillRect(bx + 1, 3, w, 4, bat < 20 ? col::red : col::fg);
    x = bx - 4;
  }
  // wifi bars
  bool ok = WiFi.status() == WL_CONNECTED;
  for (int i = 0; i < 3; i++) {
    int h = 2 + i * 2;
    uint16_t c = ok ? col::fg : col::faint;
    cv.fillRect(x - 10 + i * 3, 8 - h, 2, h, c);
  }
}

void footer(const char* hint) {
  cv.fillRect(0, 126, W, 9, col::bg);
  hline(125, col::faint);
  txt(2, 127, hint, col::dim);
}

void clearBody() { cv.fillScreen(col::bg); }

// ---------------- input ----------------
static Key lastKey;
static uint32_t pressedAt = 0, lastRepeat = 0;
static size_t lastCount = 0;

void markActivity() {
  lastActivity = millis();
  if (dimmed) {
    M5Cardputer.Display.setBrightness(S.bright);
    dimmed = false;
  }
}

// Decodes the raw key matrix ourselves, so this works with every
// M5Cardputer library version (their KeysState struct changed in 1.1).
static Key decode(bool textMode) {
  auto& kb = M5Cardputer.Keyboard;
  const auto& keys = kb.keyList();
  bool fn = false, shift = false;
  for (const auto& p : keys) {
    uint8_t v = (uint8_t)kb.getKeyValue(p).value_first;
    if (v == KEY_FN) fn = true;
    else if (v == KEY_LEFT_SHIFT || v == KEY_LEFT_CTRL) shift = true;
  }
  Key k;
  k.fn = fn;
  // the most recently added non-modifier key wins
  for (const auto& p : keys) {
    auto kv = kb.getKeyValue(p);
    uint8_t v = (uint8_t)kv.value_first;
    if (v == 0 || v >= 0x80) continue;  // fn, shift, ctrl, alt, opt
    if (v == KEY_ENTER) { k.code = K_ENTER; k.ch = 0; continue; }
    if (v == KEY_BACKSPACE) { k.code = K_BACK; k.ch = 0; continue; }
    if (v == KEY_TAB) { k.code = K_TAB; k.ch = 0; continue; }
    k.ch = shift ? kv.value_second : kv.value_first;
    k.code = K_CHAR;
    if (!textMode || fn) {
      switch (kv.value_first) {  // the arrow keys printed on the keyboard
        case ';': k.code = K_UP; break;
        case '.': k.code = K_DOWN; break;
        case ',': k.code = K_LEFT; break;
        case '/': k.code = K_RIGHT; break;
        case '`': k.code = K_ESC; break;
      }
    }
  }
  return k;
}

Key pollKey(bool textMode) {
  M5Cardputer.update();
  // auto-dim after 45 s idle
  if (!dimmed && millis() - lastActivity > 45000) {
    M5Cardputer.Display.setBrightness(S.bright / 6 + 4);
    dimmed = true;
  }
  auto& kb = M5Cardputer.Keyboard;
  size_t cnt = kb.isPressed();
  Key out;
  if (kb.isChange()) {
    if (cnt > lastCount) {
      out = decode(textMode);
      lastKey = out;
      pressedAt = lastRepeat = millis();
    } else if (cnt == 0) {
      lastKey = Key();
    }
    lastCount = cnt;
  } else if (cnt > 0 && lastKey) {
    // auto-repeat for arrows and backspace
    bool rep = lastKey.code == K_UP || lastKey.code == K_DOWN || lastKey.code == K_LEFT ||
               lastKey.code == K_RIGHT || lastKey.code == K_BACK;
    uint32_t now = millis();
    if (rep && now - pressedAt > 380 && now - lastRepeat > 90) {
      lastRepeat = now;
      out = lastKey;
    }
  }
  if (out) {
    bool wasDim = dimmed;
    markActivity();
    if (wasDim) return Key();  // first key just wakes the screen
  }
  return out;
}

Key waitKey(bool textMode) {
  for (;;) {
    Key k = pollKey(textMode);
    if (k) return k;
    delay(10);
  }
}

// ---------------- widgets ----------------
int listMenu(const char* path, const char* title, const char* const* items, int n, int sel,
             const char* const* hints) {
  const int rowH = 16, top = 24, rows = (BOT - top) / rowH;  // 6 rows
  int scroll = 0;
  uint32_t lastDraw = 0;
  bool dirty = true;
  for (;;) {
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + rows) scroll = sel - rows + 1;
    if (dirty || millis() - lastDraw > 500) {
      clearBody();
      header(path);
      txtf(2, 13, col::dim, SMALL, "# %s", title);
      for (int r = 0; r < rows && scroll + r < n; r++) {
        int i = scroll + r, y = top + r * rowH;
        bool s = i == sel;
        if (s) cv.fillRect(0, y, W, rowH, col::sel);
        txt(4, y, s ? ">" : " ", col::amber, BIG);
        txt(16, y, items[i], s ? col::white : col::fg, BIG);
        if (hints && hints[i]) txtR(W - 4, y + 4, hints[i], s ? col::fg : col::dim);
      }
      if (scroll > 0) txtR(W - 2, 13, "^", col::dim);
      if (scroll + rows < n) txtR(W - 10, 13, "v", col::dim);
      footer("[;.] move  [ent] select  [`] back");
      present();
      lastDraw = millis();
      dirty = false;
    }
    Key k = pollKey();
    if (!k) { delay(10); continue; }
    dirty = true;
    if (k.code == K_UP) sel = (sel + n - 1) % n;
    else if (k.code == K_DOWN) sel = (sel + 1) % n;
    else if (k.code == K_ENTER || k.code == K_RIGHT) return sel;
    else if (k.code == K_ESC || k.code == K_BACK || k.code == K_LEFT) return -1;
    else if (k.code == K_CHAR && k.ch >= '1' && k.ch <= '9' && k.ch - '1' < n) return k.ch - '1';
  }
}

bool textInput(const char* path, const char* prompt, String& value, bool secret, int maxLen) {
  String v = value;
  bool show = !secret;
  uint32_t lastDraw = 0;
  bool dirty = true;
  for (;;) {
    if (dirty || millis() - lastDraw > 250) {
      clearBody();
      header(path);
      txtf(2, 16, col::dim, SMALL, "# %s", prompt);
      // wrap the value over up to 4 lines of 28 chars
      String shown = show ? v : String();
      if (!show) for (size_t i = 0; i < v.length(); i++) shown += '*';
      const int perLine = 27;
      int lines = shown.length() / perLine + 1;
      if (lines > 4) lines = 4;
      int start = (int)shown.length() > perLine * 4 ? shown.length() - perLine * 4 + 1 : 0;
      String vis = shown.substring(start);
      for (int l = 0; l < lines; l++) {
        String part = vis.substring(l * perLine, (l + 1) * perLine);
        int y = 32 + l * 18;
        txt(2, y, l == 0 ? "$" : " ", col::amber, BIG);
        txt(14, y, part.c_str(), col::white, BIG);
        if (l == lines - 1 && blink()) cv.fillRect(14 + part.length() * 8, y + 13, 7, 2, col::fg);
      }
      if (secret) footer("ent ok  del erase  tab show  fn+` esc");
      else footer("[ent] ok   [del] erase   [fn+`] cancel");
      present();
      lastDraw = millis();
      dirty = false;
    }
    Key k = pollKey(true);
    if (!k) { delay(10); continue; }
    dirty = true;
    if (k.code == K_ENTER) { value = v; return true; }
    if (k.code == K_ESC) return false;
    if (k.code == K_BACK) { if (v.length()) v.remove(v.length() - 1); }
    else if (k.code == K_TAB) show = !show;
    else if (k.code == K_CHAR && (int)v.length() < maxLen) v += k.ch;
  }
}

void message(const char* path, const char* l1, const char* l2, const char* l3, bool wait,
             uint16_t c) {
  if (!c) c = col::fg;
  clearBody();
  header(path);
  int y = 40;
  txtC(y, l1, c, BIG);
  if (l2) txtC(y + 22, l2, col::dim);
  if (l3) txtC(y + 34, l3, col::dim);
  if (wait) footer("press any key");
  present();
  if (wait) waitKey();
}

bool confirm(const char* path, const char* q) {
  clearBody();
  header(path);
  txtC(44, q, col::amber, BIG);
  txtC(70, "[y] yes     [n] no", col::fg);
  present();
  for (;;) {
    Key k = waitKey();
    if (k.code == K_ENTER || k.ch == 'y' || k.ch == 'Y') return true;
    if (k.code == K_ESC || k.code == K_BACK || k.ch == 'n' || k.ch == 'N') return false;
  }
}
