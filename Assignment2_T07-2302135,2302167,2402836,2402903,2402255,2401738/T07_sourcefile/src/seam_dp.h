/* =====================================================================================
 *  File : seam_dp.h        Step 1(b) - DYNAMIC PROGRAMMING seam search
 *
 *  Subproblem   M[i][j] = minimum total energy of any 8-connected path that starts
 *                         anywhere in row 0 and ends at pixel (i, j).
 *
 *  Recurrence   M[0][j] = e[0][j]
 *               M[i][j] = e[i][j] + min( M[i-1][j-1], M[i-1][j], M[i-1][j+1] )
 *                         (terms with an out-of-range column are dropped)
 *
 *  Answer       min_j M[m-1][j];  the seam is rebuilt by walking back up the argmins.
 *
 *  Optimal substructure: the prefix of an optimal seam ending at (i, j) is itself an
 *  optimal seam ending at its row-(i-1) pixel - otherwise swapping in a cheaper prefix
 *  (which still ends adjacent to j) would give a cheaper seam.  Overlapping
 *  subproblems: each M[i-1][j] is reused by three cells of row i.  So m*n subproblems,
 *  O(1) each: Theta(m n) time, versus Omega(2^m) seams for brute force.
 *
 *  Variants (all return the SAME seam - identical tie-breaking, verified by the tests):
 *    findVerticalSeamDP          full m x n int64 table, backtrack by re-reading it
 *    findVerticalSeamDPCompact   two rolling int64 rows + an m x n int8 "came-from"
 *                                table : 8x less memory (answers item 5(d))
 *    findVerticalSeamDPParallel  compact + each row's n cells computed by OpenMP threads
 *                                (cells of one row are independent; rows are not)
 *    findHorizontalSeamDP        native left-to-right sweep (no transpose)
 *    findVerticalSeamForwardDP   "forward energy" (Rubinstein et al. 2008): the cost of
 *                                a step is the NEW edge it creates, not the pixel removed
 * ===================================================================================== */
#pragma once

#include "seam.h"

namespace sc {

struct DPStats {
    size_t tableBytes = 0;     /* memory used by the DP tables for this call */
};

Seam findVerticalSeamDP(const EnergyMap& e, DPStats* stats = nullptr);
Seam findVerticalSeamDPCompact(const EnergyMap& e, DPStats* stats = nullptr);
Seam findVerticalSeamDPParallel(const EnergyMap& e, int threads, DPStats* stats = nullptr);
Seam findHorizontalSeamDP(const EnergyMap& e, DPStats* stats = nullptr);

/* protect/penalty: optional object protection - penalty is added to every protected
 * pixel's cost (forward energy has no energy map to add it to).                   */
Seam    findVerticalSeamForwardDP(const Grid<uint8_t>& gray, DPStats* stats = nullptr,
                                  const Grid<uint8_t>* protect = nullptr, int64_t penalty = 0);
int64_t forwardSeamCost(const Grid<uint8_t>& gray, const std::vector<int>& idx);

/* Item 2(a)(i): exact number of vertical seams in an m-row, n-column image, by the
 * counting DP  N[0][j] = 1,  N[i][j] = N[i-1][j-1] + N[i-1][j] + N[i-1][j+1].        */
long double countSeams(int n, int m);

}  // namespace sc
