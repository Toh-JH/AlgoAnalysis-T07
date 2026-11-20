/* =====================================================================================
 *  File : seam.h           The Seam type and seam removal (step 1(c) of the brief)
 *
 *  A VERTICAL seam in an m-row, n-column image is one column index per row,
 *      idx[y] in [0, n-1],   |idx[y] - idx[y-1]| <= 1        (8-connected, top to bottom)
 *  A HORIZONTAL seam is the same thing on the transposed image: idx[x] is a row index.
 *  Every finder in this project returns its seam together with its total energy (cost).
 * ===================================================================================== */
#pragma once

#include "energy.h"

#include <algorithm>
#include <cstring>

namespace sc {

struct Seam {
    std::vector<int> idx;      /* vertical: idx[row] = column.  horizontal: idx[col] = row */
    int64_t          cost = 0; /* sum of the energies of the seam's pixels                 */
};

/* idx has `length` entries, each in [0, span-1], and neighbours differ by at most 1. */
bool isValidSeam(const std::vector<int>& idx, int length, int span);

/* Sum of e[y][idx[y]] for a vertical seam. */
int64_t verticalSeamEnergy(const EnergyMap& e, const std::vector<int>& idx);

/* Delete one pixel per row: Theta(m n) because every row to the right of the seam moves
 * left by one (a memmove per row).  Works for Image and every Grid<T>.                 */
Image removeVerticalSeam(const Image& img, const std::vector<int>& idx);

template <typename T>
Grid<T> removeVerticalSeam(const Grid<T>& g, const std::vector<int>& idx) {
    Grid<T> out(g.w - 1, g.h);
    for (int y = 0; y < g.h; ++y) {
        int c = idx[static_cast<size_t>(y)];
        const T* s = g.row(y);
        T*       d = out.row(y);
        std::copy(s, s + c, d);
        std::copy(s + c + 1, s + g.w, d + c);
    }
    return out;
}

}  // namespace sc
