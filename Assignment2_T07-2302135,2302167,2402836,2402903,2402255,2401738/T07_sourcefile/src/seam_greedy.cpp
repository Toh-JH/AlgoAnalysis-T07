/* =====================================================================================
 *  File : seam_greedy.cpp  Greedy (locally optimal, row-by-row) seam finders
 * ===================================================================================== */
#include "seam_greedy.h"

namespace sc {

Seam findVerticalSeamGreedyFrom(const EnergyMap& e, int startCol) {
    const int w = e.w, h = e.h;
    Seam s;
    s.idx.resize(static_cast<size_t>(h));
    int x = startCol;
    s.idx[0] = x;
    s.cost = e.at(x, 0);
    for (int y = 1; y < h; ++y) {
        /* local choice: same tie order as the DP (down, down-left, down-right) */
        int best = x;
        if (x > 0     && e.at(x - 1, y) < e.at(best, y)) best = x - 1;
        if (x < w - 1 && e.at(x + 1, y) < e.at(best, y)) best = x + 1;
        x = best;
        s.idx[static_cast<size_t>(y)] = x;
        s.cost += e.at(x, y);
    }
    return s;
}

Seam findVerticalSeamGreedy(const EnergyMap& e) {
    int start = 0;
    for (int x = 1; x < e.w; ++x) if (e.at(x, 0) < e.at(start, 0)) start = x;
    return findVerticalSeamGreedyFrom(e, start);
}

Seam findVerticalSeamGreedyBestStart(const EnergyMap& e) {
    Seam best = findVerticalSeamGreedyFrom(e, 0);
    for (int x = 1; x < e.w; ++x) {
        Seam s = findVerticalSeamGreedyFrom(e, x);
        if (s.cost < best.cost) best = std::move(s);
    }
    return best;
}

}  // namespace sc
