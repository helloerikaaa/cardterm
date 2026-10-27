// sudoku_core.cpp - generator & solver.
// The solver is iterative (no recursion): its search state lives in a static
// table, not on the call stack. A recursive version needs ~8 KB of stack on the
// ESP32-S3, which is the whole Arduino loop stack, and crashed the Cardputer.
#include "sudoku_core.h"
#include <string.h>

namespace sudoku {

struct Frame {
  uint8_t cell;       // grid index being filled at this depth
  uint8_t n;          // number of candidate digits
  uint8_t k;          // how many candidates tried so far
  uint8_t digits[9];  // candidate order
};

struct Ctx {
  uint8_t* g;
  uint16_t row[9], col[9], box[9];
};

static Frame stack_[82];  // one frame per empty cell, max 81

static inline int boxOf(int r, int c) { return (r / 3) * 3 + c / 3; }

static int popc(uint16_t v) {
  int n = 0;
  while (v) { v &= v - 1; n++; }
  return n;
}

static bool init(Ctx& x) {
  memset(x.row, 0, sizeof x.row);
  memset(x.col, 0, sizeof x.col);
  memset(x.box, 0, sizeof x.box);
  for (int i = 0; i < 81; i++) {
    int v = x.g[i];
    if (!v) continue;
    int r = i / 9, c = i % 9, b = boxOf(r, c);
    uint16_t m = 1 << v;
    if ((x.row[r] | x.col[c] | x.box[b]) & m) return false;
    x.row[r] |= m; x.col[c] |= m; x.box[b] |= m;
  }
  return true;
}

static inline void place(Ctx& x, int i, int v) {
  int r = i / 9, c = i % 9;
  uint16_t m = 1 << v;
  x.g[i] = v;
  x.row[r] |= m; x.col[c] |= m; x.box[boxOf(r, c)] |= m;
}

static inline void unplace(Ctx& x, int i) {
  int r = i / 9, c = i % 9, v = x.g[i];
  uint16_t m = ~(1 << v);
  x.g[i] = 0;
  x.row[r] &= m; x.col[c] &= m; x.box[boxOf(r, c)] &= m;
}

// Picks the empty cell with the fewest candidates and fills frame f.
// Returns 0 = grid full (solved), 1 = frame ready, -1 = dead end.
static int makeFrame(Ctx& x, Frame& f, uint32_t (*rnd)()) {
  int best = -1, bestN = 10;
  uint16_t bestMask = 0;
  for (int i = 0; i < 81; i++) {
    if (x.g[i]) continue;
    int r = i / 9, c = i % 9;
    uint16_t free = ~(x.row[r] | x.col[c] | x.box[boxOf(r, c)]) & 0x3FE;
    int n = popc(free);
    if (n < bestN) {
      bestN = n; best = i; bestMask = free;
      if (n <= 1) break;
    }
  }
  if (best < 0) return 0;
  if (bestN == 0) return -1;
  f.cell = best;
  f.n = 0;
  f.k = 0;
  for (int v = 1; v <= 9; v++)
    if (bestMask & (1 << v)) f.digits[f.n++] = v;
  if (rnd) {
    for (int i = f.n - 1; i > 0; i--) {
      int j = rnd() % (i + 1);
      uint8_t t = f.digits[i]; f.digits[i] = f.digits[j]; f.digits[j] = t;
    }
  }
  return 1;
}

// Depth-first search. Counts solutions up to `limit`.
// keepFirst: stop at the first solution and leave it in the grid.
static int search(Ctx& x, int limit, uint32_t (*rnd)(), bool keepFirst) {
  int count = 0;
  int s = makeFrame(x, stack_[0], rnd);
  if (s == 0) return 1;
  if (s < 0) return 0;
  int depth = 0;
  while (depth >= 0) {
    Frame& f = stack_[depth];
    if (f.k > 0) unplace(x, f.cell);  // undo the previous try at this depth
    if (f.k >= f.n) { depth--; continue; }  // exhausted: backtrack
    place(x, f.cell, f.digits[f.k++]);
    int r = makeFrame(x, stack_[depth + 1], rnd);
    if (r == 0) {  // solved
      if (++count >= limit) {
        if (keepFirst) return count;
        // undo everything so the caller's grid is untouched
        for (int d = depth; d >= 0; d--) unplace(x, stack_[d].cell);
        return count;
      }
    } else if (r == 1) {
      depth++;
    }
  }
  return count;
}

int countSolutions(uint8_t* grid, int limit) {
  uint8_t tmp[81];
  memcpy(tmp, grid, 81);
  Ctx x;
  x.g = tmp;
  if (!init(x)) return 0;
  return search(x, limit, nullptr, false);
}

void generate(uint8_t* puzzle, uint8_t* solution, int clues, uint32_t (*rnd)()) {
  // 1) random full grid
  memset(solution, 0, 81);
  Ctx x;
  x.g = solution;
  init(x);
  search(x, 1, rnd, true);
  memcpy(puzzle, solution, 81);

  // 2) remove cells in symmetric pairs while the solution stays unique
  uint8_t order[81];
  for (int i = 0; i < 81; i++) order[i] = i;
  for (int i = 80; i > 0; i--) {
    int j = rnd() % (i + 1);
    uint8_t t = order[i]; order[i] = order[j]; order[j] = t;
  }
  int filled = 81;
  for (int k = 0; k < 81 && filled > clues; k++) {
    int a = order[k], b = 80 - a;
    if (!puzzle[a]) continue;
    uint8_t va = puzzle[a], vb = puzzle[b];
    puzzle[a] = 0; puzzle[b] = 0;
    int removed = (a == b) ? 1 : 2;
    if (filled - removed < clues - 1 || countSolutions(puzzle, 2) != 1) {
      puzzle[a] = va; puzzle[b] = vb;  // put back
    } else {
      filled -= removed;
    }
  }
}

}  // namespace sudoku
