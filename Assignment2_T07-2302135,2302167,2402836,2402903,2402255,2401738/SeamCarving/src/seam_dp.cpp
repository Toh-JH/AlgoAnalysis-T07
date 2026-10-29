/* =====================================================================================
 *  File : seam_dp.cpp      Dynamic-programming seam finders
 * ===================================================================================== */
#include "seam_dp.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace sc {

/* The one tie-breaking rule used by every DP variant: prefer straight down, then
 * down-left, then down-right (strict '<', so earlier candidates win ties).
 * Returns the parent's column offset: -1, 0 or +1.                                    */
static inline int bestParent(const int64_t* prev, int x, int w) {
    int best = 0;
    int64_t bv = prev[x];
    if (x > 0     && prev[x - 1] < bv) { bv = prev[x - 1]; best = -1; }
    if (x < w - 1 && prev[x + 1] < bv) { best = +1; }
    return best;
}

static inline int argminRow(const int64_t* row, int w) {
    int best = 0;
    for (int x = 1; x < w; ++x) if (row[x] < row[best]) best = x;
    return best;
}

/* ------------------------------------------------------------------------------------ *
 *  1. Full table.  M is m x n int64.  Backtracking re-applies bestParent on M.
 *     Time Theta(m n), space Theta(m n) * 8 bytes.
 * ------------------------------------------------------------------------------------ */
Seam findVerticalSeamDP(const EnergyMap& e, DPStats* stats) {
    const int w = e.w, h = e.h;
    Grid<int64_t> M(w, h);
    for (int x = 0; x < w; ++x) M.at(x, 0) = e.at(x, 0);                  /* base case */
    for (int y = 1; y < h; ++y) {
        const int64_t* prev = M.row(y - 1);
        int64_t*       cur  = M.row(y);
        const int32_t* er   = e.row(y);
        for (int x = 0; x < w; ++x)
            cur[x] = er[x] + prev[x + bestParent(prev, x, w)];             /* recurrence */
    }
    Seam s;
    s.idx.resize(static_cast<size_t>(h));
    int x = argminRow(M.row(h - 1), w);
    s.cost = M.at(x, h - 1);
    for (int y = h - 1; y >= 0; --y) {                                      /* backtrack */
        s.idx[static_cast<size_t>(y)] = x;
        if (y > 0) x += bestParent(M.row(y - 1), x, w);
    }
    if (stats) stats->tableBytes = M.d.size() * sizeof(int64_t);
    return s;
}

/* ------------------------------------------------------------------------------------ *
 *  2. Compact.  Only the previous row of M is needed to compute the next, so keep two
 *     rolling rows.  To rebuild the seam we still need to know where each cell came
 *     from, but that is one of {-1,0,+1}: one signed byte instead of an 8-byte cost.
 *     Space: 2n * 8 + m n * 1 bytes  (vs m n * 8).
 * ------------------------------------------------------------------------------------ */
Seam findVerticalSeamDPCompact(const EnergyMap& e, DPStats* stats) {
    const int w = e.w, h = e.h;
    std::vector<int64_t> prev(static_cast<size_t>(w)), cur(static_cast<size_t>(w));
    Grid<int8_t> from(w, h, 0);
    for (int x = 0; x < w; ++x) prev[static_cast<size_t>(x)] = e.at(x, 0);
    for (int y = 1; y < h; ++y) {
        const int32_t* er = e.row(y);
        int8_t*        fr = from.row(y);
        for (int x = 0; x < w; ++x) {
            int p = bestParent(prev.data(), x, w);
            fr[x] = static_cast<int8_t>(p);
            cur[static_cast<size_t>(x)] = er[x] + prev[static_cast<size_t>(x + p)];
        }
        std::swap(prev, cur);
    }
    Seam s;
    s.idx.resize(static_cast<size_t>(h));
    int x = argminRow(prev.data(), w);
    s.cost = prev[static_cast<size_t>(x)];
    for (int y = h - 1; y >= 0; --y) {
        s.idx[static_cast<size_t>(y)] = x;
        x += from.at(x, y);
    }
    if (stats) stats->tableBytes = from.d.size() + 2 * static_cast<size_t>(w) * sizeof(int64_t);
    return s;
}

/* ------------------------------------------------------------------------------------ *
 *  3. Parallel compact.  Row y depends on row y-1 (a sequential chain of m steps), but
 *     the n cells WITHIN a row are independent.  One parallel region for the whole
 *     sweep; '#pragma omp for' splits each row and its implicit barrier is the row
 *     dependency.  Span Theta(m), work Theta(m n): speed-up needs n >> threads and is
 *     limited by m barriers (measured in the benchmark).
 * ------------------------------------------------------------------------------------ */
Seam findVerticalSeamDPParallel(const EnergyMap& e, int threads, DPStats* stats) {
    const int w = e.w, h = e.h;
    std::vector<int64_t> bufA(static_cast<size_t>(w)), bufB(static_cast<size_t>(w));
    Grid<int8_t> from(w, h, 0);
    for (int x = 0; x < w; ++x) bufA[static_cast<size_t>(x)] = e.at(x, 0);
    int64_t* prev = bufA.data();
    int64_t* cur  = bufB.data();
    (void)threads;
#pragma omp parallel num_threads(threads) if (threads > 1)
    {
        for (int y = 1; y < h; ++y) {
            const int32_t* er = e.row(y);
            int8_t*        fr = from.row(y);
#pragma omp for schedule(static)
            for (int x = 0; x < w; ++x) {
                int p = bestParent(prev, x, w);
                fr[x] = static_cast<int8_t>(p);
                cur[x] = er[x] + prev[x + p];
            }                                                  /* implicit barrier */
#pragma omp single
            std::swap(prev, cur);                              /* implicit barrier */
        }
    }
    Seam s;
    s.idx.resize(static_cast<size_t>(h));
    int x = argminRow(prev, w);
    s.cost = prev[x];
    for (int y = h - 1; y >= 0; --y) {
        s.idx[static_cast<size_t>(y)] = x;
        x += from.at(x, y);
    }
    if (stats) stats->tableBytes = from.d.size() + 2 * static_cast<size_t>(w) * sizeof(int64_t);
    return s;
}

/* ------------------------------------------------------------------------------------ *
 *  4. Native horizontal seam: sweep columns left to right.
 *     M[x][y] = e[y][x] + min(M[x-1][y-1], M[x-1][y], M[x-1][y+1]).   idx[x] = row.
 *     Column-major storage keeps the inner loop contiguous.
 * ------------------------------------------------------------------------------------ */
Seam findHorizontalSeamDP(const EnergyMap& e, DPStats* stats) {
    const int w = e.w, h = e.h;
    std::vector<int64_t> M(static_cast<size_t>(w) * h);
    auto col = [&](int x) { return M.data() + static_cast<size_t>(x) * h; };
    for (int y = 0; y < h; ++y) col(0)[y] = e.at(0, y);
    for (int x = 1; x < w; ++x) {
        const int64_t* prev = col(x - 1);
        int64_t*       cur  = col(x);
        for (int y = 0; y < h; ++y) cur[y] = e.at(x, y) + prev[y + bestParent(prev, y, h)];
    }
    Seam s;
    s.idx.resize(static_cast<size_t>(w));
    int y = argminRow(col(w - 1), h);
    s.cost = col(w - 1)[y];
    for (int x = w - 1; x >= 0; --x) {
        s.idx[static_cast<size_t>(x)] = y;
        if (x > 0) y += bestParent(col(x - 1), y, h);
    }
    if (stats) stats->tableBytes = M.size() * sizeof(int64_t);
    return s;
}

/* ------------------------------------------------------------------------------------ *
 *  5. Forward energy.  Removing (x, y) makes I(x-1,y) and I(x+1,y) neighbours, and,
 *     depending on where the seam was in the row above, also joins I(x,y-1) to one of
 *     them.  Charge exactly those new edges:
 *        CU = |I(x+1,y) - I(x-1,y)|                     (came from directly above)
 *        CL = CU + |I(x,y-1) - I(x-1,y)|                (came from up-left)
 *        CR = CU + |I(x,y-1) - I(x+1,y)|                (came from up-right)
 *        M[y][x] = min(M[y-1][x-1] + CL, M[y-1][x] + CU, M[y-1][x+1] + CR)
 *     Still Theta(m n); it avoids the jagged artefacts backward energy can introduce.
 * ------------------------------------------------------------------------------------ */
struct FwdCosts { int64_t cl, cu, cr; };

static inline FwdCosts forwardCosts(const Grid<uint8_t>& g, int x, int y) {
    const int w = g.w;
    int L = g.at(std::max(x - 1, 0), y), R = g.at(std::min(x + 1, w - 1), y);
    int cu = std::abs(R - L);
    if (y == 0) return {cu, cu, cu};
    int U = g.at(x, y - 1);
    return {cu + std::abs(U - L), cu, cu + std::abs(U - R)};
}

Seam findVerticalSeamForwardDP(const Grid<uint8_t>& gray, DPStats* stats) {
    const int w = gray.w, h = gray.h;
    std::vector<int64_t> prev(static_cast<size_t>(w)), cur(static_cast<size_t>(w));
    Grid<int8_t> from(w, h, 0);
    for (int x = 0; x < w; ++x) prev[static_cast<size_t>(x)] = forwardCosts(gray, x, 0).cu;
    for (int y = 1; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            FwdCosts c = forwardCosts(gray, x, y);
            int     best = 0;
            int64_t bv   = prev[static_cast<size_t>(x)] + c.cu;
            if (x > 0     && prev[static_cast<size_t>(x - 1)] + c.cl < bv) { bv = prev[static_cast<size_t>(x - 1)] + c.cl; best = -1; }
            if (x < w - 1 && prev[static_cast<size_t>(x + 1)] + c.cr < bv) { bv = prev[static_cast<size_t>(x + 1)] + c.cr; best = +1; }
            from.at(x, y) = static_cast<int8_t>(best);
            cur[static_cast<size_t>(x)] = bv;
        }
        std::swap(prev, cur);
    }
    Seam s;
    s.idx.resize(static_cast<size_t>(h));
    int x = argminRow(prev.data(), w);
    s.cost = prev[static_cast<size_t>(x)];
    for (int y = h - 1; y >= 0; --y) {
        s.idx[static_cast<size_t>(y)] = x;
        x += from.at(x, y);
    }
    if (stats) stats->tableBytes = from.d.size() + 2 * static_cast<size_t>(w) * sizeof(int64_t);
    return s;
}

int64_t forwardSeamCost(const Grid<uint8_t>& gray, const std::vector<int>& idx) {
    int64_t total = forwardCosts(gray, idx[0], 0).cu;
    for (int y = 1; y < gray.h; ++y) {
        int x = idx[static_cast<size_t>(y)], d = idx[static_cast<size_t>(y - 1)] - x;
        FwdCosts c = forwardCosts(gray, x, y);
        total += d < 0 ? c.cl : (d == 0 ? c.cu : c.cr);
    }
    return total;
}

/* ------------------------------------------------------------------------------------ *
 *  Seam counting DP (item 2(a)(i)).  N[i][j] = number of seams from row 0 ending at
 *  (i, j).  long double keeps ~64 bits of mantissa: exact for the sizes we tabulate.
 * ------------------------------------------------------------------------------------ */
long double countSeams(int n, int m) {
    if (n <= 0 || m <= 0) return 0;
    std::vector<long double> prev(static_cast<size_t>(n), 1.0L), cur(static_cast<size_t>(n));
    for (int i = 1; i < m; ++i) {
        for (int j = 0; j < n; ++j) {
            long double s = prev[static_cast<size_t>(j)];
            if (j > 0)     s += prev[static_cast<size_t>(j - 1)];
            if (j < n - 1) s += prev[static_cast<size_t>(j + 1)];
            cur[static_cast<size_t>(j)] = s;
        }
        std::swap(prev, cur);
    }
    long double total = 0;
    for (long double v : prev) total += v;
    return total;
}

}  // namespace sc
