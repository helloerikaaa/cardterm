// sudoku.cpp - the Sudoku game (pencil notes, hints, mistake check, auto-save)
#include "sudoku_core.h"
#include "ui.h"
#include <Preferences.h>

static uint32_t rnd() { return esp_random(); }
static Preferences sp;

static uint32_t getScore(const char* key, uint32_t def) {
  sp.begin("scores", true);
  uint32_t v = sp.getUInt(key, def);
  sp.end();
  return v;
}
static void setScore(const char* key, uint32_t v) {
  sp.begin("scores", false);
  sp.putUInt(key, v);
  sp.end();
}
static void fmtTime(char* b, uint32_t secs) {
  if (secs >= 3600) sprintf(b, "%lu:%02lu:%02lu", (unsigned long)(secs / 3600),
                            (unsigned long)(secs / 60 % 60), (unsigned long)(secs % 60));
  else sprintf(b, "%lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
}

// =====================================================================
// SUDOKU
// =====================================================================
struct SudokuSave {
  uint8_t magic;
  uint8_t diff;
  uint8_t hints;
  uint8_t active;
  uint8_t puzzle[81], sol[81], cur[81];
  uint16_t notes[81];
  uint32_t elapsed;  // seconds
};
static SudokuSave SG;
static const char* DIFF_NAMES[] = {"easy", "medium", "hard"};
static const int DIFF_CLUES[] = {40, 32, 26};

static void sudokuStore() {
  sp.begin("sudoku", false);
  sp.putBytes("g", &SG, sizeof SG);
  sp.end();
}
static bool sudokuLoad() {
  sp.begin("sudoku", true);
  size_t n = sp.getBytes("g", &SG, sizeof SG);
  sp.end();
  return n == sizeof SG && SG.magic == 0x5D && SG.active;
}

static void sudokuNew(int diff) {
  message("~/sudoku", "generating...", DIFF_NAMES[diff], nullptr, false);
  memset(&SG, 0, sizeof SG);
  SG.magic = 0x5D;
  SG.diff = diff;
  SG.active = 1;
  sudoku::generate(SG.puzzle, SG.sol, DIFF_CLUES[diff], rnd);
  memcpy(SG.cur, SG.puzzle, 81);
}

static bool conflicts(int i) {
  int v = SG.cur[i];
  if (!v) return false;
  int r = i / 9, c = i % 9, br = r / 3 * 3, bc = c / 3 * 3;
  for (int k = 0; k < 9; k++) {
    if (k != c && SG.cur[r * 9 + k] == v) return true;
    if (k != r && SG.cur[k * 9 + c] == v) return true;
    int j = (br + k / 3) * 9 + bc + k % 3;
    if (j != i && SG.cur[j] == v) return true;
  }
  return false;
}

static void clearPeerNotes(int i, int v) {
  int r = i / 9, c = i % 9, br = r / 3 * 3, bc = c / 3 * 3;
  uint16_t m = ~(1 << v);
  for (int k = 0; k < 9; k++) {
    SG.notes[r * 9 + k] &= m;
    SG.notes[k * 9 + c] &= m;
    SG.notes[(br + k / 3) * 9 + bc + k % 3] &= m;
  }
}

static void runSudokuGame() {
  const char* P = "~/sudoku";
  const int CS = 13, GX = 3, GY = 14;
  int sel = 40;
  bool noteMode = false, check = false;
  uint32_t startMs = millis(), base = SG.elapsed, lastDraw = 0;
  bool dirty = true;
  auto elapsed = [&]() { return base + (millis() - startMs) / 1000; };
  for (;;) {
    if (dirty || millis() - lastDraw > 1000) {
      clearBody();
      header(P);
      int sr = sel / 9, sc = sel % 9, sv = SG.cur[sel];
      for (int i = 0; i < 81; i++) {
        int r = i / 9, c = i % 9;
        int x = GX + c * CS + c / 3, y = GY + r * CS + r / 3;
        uint16_t bg = col::bg;
        bool peer = r == sr || c == sc || (r / 3 == sr / 3 && c / 3 == sc / 3);
        if (peer) bg = col::bar;
        if (sv && SG.cur[i] == sv) bg = col::faint;
        if (i == sel) bg = noteMode ? cv.color565(90, 60, 0) : col::sel;
        cv.fillRect(x, y, CS - 1, CS - 1, bg);
        int v = SG.cur[i];
        if (v) {
          uint16_t fc = SG.puzzle[i] ? col::white : col::cyan;
          if (conflicts(i) || (check && !SG.puzzle[i] && v != SG.sol[i])) fc = col::red;
          char s[2] = {(char)('0' + v), 0};
          font(SMALL);
          cv.setTextColor(fc);
          cv.setTextDatum(middle_center);
          cv.drawString(s, x + CS / 2, y + CS / 2);
          cv.setTextDatum(top_left);
        } else if (SG.notes[i]) {
          for (int d = 1; d <= 9; d++)
            if (SG.notes[i] & (1 << d))
              cv.fillRect(x + 2 + ((d - 1) % 3) * 3, y + 2 + ((d - 1) / 3) * 3, 2, 2, col::amber);
        }
      }
      // thick box lines
      int span = 9 * CS + 2;
      for (int k = 0; k <= 3; k++) {
        int o = k * (3 * CS + 1) - 1;
        cv.drawFastHLine(GX - 1, GY + o, span, col::dim);
        cv.drawFastVLine(GX + o, GY - 1, span, col::dim);
      }
      // side panel
      int px = 128;
      char tb[12];
      fmtTime(tb, elapsed());
      txtf(px, 14, col::amber, SMALL, "sudoku %s", DIFF_NAMES[SG.diff]);
      txt(px, 26, tb, col::white, BIG);
      int filled = 0;
      for (int i = 0; i < 81; i++) filled += SG.cur[i] != 0;
      txtf(px, 44, col::dim, SMALL, "%d/81  hints %d", filled, SG.hints);
      txtf(px, 54, noteMode ? col::amber : col::dim, SMALL, "mode: %s", noteMode ? "NOTES" : "pen");
      if (SG.notes[sel] && !SG.cur[sel]) {
        String n = "notes:";
        for (int d = 1; d <= 9; d++) if (SG.notes[sel] & (1 << d)) { n += ' '; n += d; }
        txt(px, 64, n.c_str(), col::amber);
      }
      txt(px, 78, "1-9 place  0 clear", col::dim);
      txt(px, 88, "n notes  h hint", col::dim);
      txtf(px, 98, check ? col::fg : col::dim, SMALL, "c check: %s", check ? "on" : "off");
      txt(px, 108, "` save & exit", col::dim);
      present();
      dirty = false;
      lastDraw = millis();
    }
    Key k = pollKey();
    if (!k) { delay(15); continue; }
    dirty = true;
    int r = sel / 9, c = sel % 9;
    if (k.code == K_UP) r = (r + 8) % 9;
    else if (k.code == K_DOWN) r = (r + 1) % 9;
    else if (k.code == K_LEFT) c = (c + 8) % 9;
    else if (k.code == K_RIGHT) c = (c + 1) % 9;
    else if (k.code == K_ESC) {
      SG.elapsed = elapsed();
      sudokuStore();
      return;
    } else if (k.ch == 'n') noteMode = !noteMode;
    else if (k.ch == 'c') check = !check;
    else if (k.ch == 'h' && !SG.puzzle[sel] && SG.cur[sel] != SG.sol[sel]) {
      SG.cur[sel] = SG.sol[sel];
      SG.hints++;
      clearPeerNotes(sel, SG.cur[sel]);
    } else if (!SG.puzzle[sel]) {
      if (k.ch >= '1' && k.ch <= '9') {
        int v = k.ch - '0';
        if (noteMode) {
          if (!SG.cur[sel]) SG.notes[sel] ^= (1 << v);
        } else {
          SG.cur[sel] = SG.cur[sel] == v ? 0 : v;
          if (SG.cur[sel]) clearPeerNotes(sel, v);
        }
      } else if (k.ch == '0' || k.code == K_BACK || k.ch == ' ') {
        if (SG.cur[sel]) SG.cur[sel] = 0;
        else SG.notes[sel] = 0;
      }
    }
    sel = r * 9 + c;
    if (memcmp(SG.cur, SG.sol, 81) == 0) {
      uint32_t t = elapsed();
      SG.active = 0;
      sudokuStore();
      char key[12], tb[12], line[40];
      snprintf(key, sizeof key, "sd%d", SG.diff);
      uint32_t best = getScore(key, 0);
      bool rec = SG.hints == 0 && (best == 0 || t < best);
      if (rec) setScore(key, t);
      fmtTime(tb, t);
      snprintf(line, sizeof line, "%s in %s, %d hints", DIFF_NAMES[SG.diff], tb, SG.hints);
      message(P, "SOLVED!", line, rec ? "new best time!" : nullptr, true, col::amber);
      return;
    }
  }
}

void runSudoku() {
  for (;;) {
    bool resume = sudokuLoad();
    static String hb[3];
    static char h[24];
    const char* items[4];
    const char* hints[4];
    int n = 0, off = 0;
    if (resume) {
      items[n] = "continue";
      char tb[12];
      fmtTime(tb, SG.elapsed);
      snprintf(h, sizeof h, "%s %s", DIFF_NAMES[SG.diff], tb);
      hints[n++] = h;
      off = 1;
    }
    for (int d = 0; d < 3; d++) {
      char key[12], tb[12];
      snprintf(key, sizeof key, "sd%d", d);
      uint32_t b = getScore(key, 0);
      if (b) { fmtTime(tb, b); hb[d] = String("best ") + tb; } else hb[d] = "";
      items[n] = DIFF_NAMES[d];
      hints[n++] = hb[d].c_str();
    }
    int pick = listMenu("~/sudoku", resume ? "sudoku: continue or new game" : "sudoku: new game",
                        items, n, 0, hints);
    if (pick < 0) return;
    if (pick >= off) {
      if (resume && !confirm("~/sudoku", "abandon saved game?")) continue;
      sudokuNew(pick - off);
    }
    runSudokuGame();
  }
}

// small status for the dashboard
String sudokuStatus() {
  if (sudokuLoad()) {
    char tb[12];
    fmtTime(tb, SG.elapsed);
    return String("saved: ") + DIFF_NAMES[SG.diff] + " " + tb;
  }
  return "new puzzle";
}
