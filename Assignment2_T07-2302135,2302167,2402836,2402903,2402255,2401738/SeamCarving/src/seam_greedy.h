/* =====================================================================================
 *  File : seam_greedy.h    Item 3 - GREEDY seam search
 *
 *  Start at the cheapest pixel of the top row; then, row by row, step to whichever of
 *  the (up to) three pixels below is cheapest.  Each decision looks only one row ahead
 *  and is never revisited, so:
 *      time  Theta(n + m)   (argmin of row 0, then 3 comparisons per row)
 *      space Theta(m)       (just the seam)
 *  but the result is NOT optimal: a cheap first step can lead into an expensive region
 *  (see the "trap" test and the report's counter-example).
 *
 *  findVerticalSeamGreedyBestStart is a stronger greedy variant: run the walk from every
 *  top-row column and keep the cheapest: Theta(m n), still not optimal.
 * ===================================================================================== */
#pragma once

#include "seam.h"

namespace sc {

Seam findVerticalSeamGreedy(const EnergyMap& e);
Seam findVerticalSeamGreedyFrom(const EnergyMap& e, int startCol);
Seam findVerticalSeamGreedyBestStart(const EnergyMap& e);

}  // namespace sc
