// sudoku_core.h - generator & solver (pure C++, no Arduino deps)
#pragma once
#include <stdint.h>

namespace sudoku {

// grid: 81 cells, 0 = empty, 1..9 = digit
// Counts solutions up to `limit` (returns 0, 1 or limit).
int countSolutions(uint8_t* grid, int limit);

// Generates a puzzle with a unique solution.
// clues: target number of givens (e.g. 40 easy, 32 medium, 26 hard)
// rnd: function returning random uint32.
void generate(uint8_t* puzzle, uint8_t* solution, int clues, uint32_t (*rnd)());

}  // namespace sudoku
