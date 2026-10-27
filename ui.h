// ui.h - terminal-style drawing + keyboard input helpers
#pragma once
#include <M5Cardputer.h>

extern M5Canvas cv;  // full-screen 240x135 back buffer

// ---- palette (phosphor terminal) ----
namespace col {
extern uint16_t bg, fg, dim, faint, bar, amber, cyan, red, white, blue, yellow, grey, sel;
}

// ---- fonts ----
enum FontSz { SMALL, BIG };     // SMALL = 6x8, BIG = 8x16
void font(FontSz f);
inline int fw(FontSz f) { return f == SMALL ? 6 : 8; }
inline int fh(FontSz f) { return f == SMALL ? 8 : 16; }

static const int W = 240, H = 135;
static const int TOP = 12;   // first body pixel row
static const int BOT = 125;  // last body pixel row (footer starts at 126)

void uiBegin();
void present();  // push canvas to display

// header: path on the left ("~/weather"), clock/battery/wifi on the right
void header(const char* path);
// footer hint line, e.g. "[;.] move  [ent] ok  [`] back"
void footer(const char* hint);
void clearBody();
void txt(int x, int y, const char* s, uint16_t c, FontSz f = SMALL);
void txtf(int x, int y, uint16_t c, FontSz f, const char* fmt, ...);
void txtR(int xRight, int y, const char* s, uint16_t c, FontSz f = SMALL);  // right aligned
void txtC(int y, const char* s, uint16_t c, FontSz f = SMALL);  // centered
void hline(int y, uint16_t c);
bool blink();  // cursor blink phase

// ---- input ----
enum KeyCode { K_NONE = 0, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_ENTER, K_BACK, K_ESC, K_TAB, K_CHAR };
struct Key {
  KeyCode code = K_NONE;
  char ch = 0;      // printable character (also set for arrows: ; . , /)
  bool fn = false;
  operator bool() const { return code != K_NONE; }
};
// textMode: ; . , / ` are returned as characters instead of arrows/esc
// (fn + those keys still produce arrows/esc).
Key pollKey(bool textMode = false);
Key waitKey(bool textMode = false);
void markActivity();

// ---- widgets ----
// Vertical list. Returns selected index or -1 on back.
// hints (optional) are shown dim at the right of each row.
int listMenu(const char* path, const char* title, const char* const* items, int n,
             int start = 0, const char* const* hints = nullptr);
// Text entry. Returns false if cancelled.
bool textInput(const char* path, const char* prompt, String& value, bool secret = false,
               int maxLen = 63);
// Full screen message, waits for any key if wait=true.
void message(const char* path, const char* l1, const char* l2 = nullptr,
             const char* l3 = nullptr, bool wait = true, uint16_t c = 0);
// Yes/No prompt
bool confirm(const char* path, const char* question);

// time helpers (local, using settings tz offset)
bool timeValid();
time_t nowLocal();  // unix seconds shifted by tz offset
void fmtHM(char* buf, time_t localT);
