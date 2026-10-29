/* =====================================================================================
 *  File : energy.h         Step 1(a) of the brief - the ENERGY MAP
 *
 *  e[y][x] measures how "disruptive" it would be to delete pixel (x, y).  Smooth regions
 *  (sky, grass) have low energy; edges and texture have high energy.  Two variants:
 *
 *    Gradient  e = sum_{c in RGB} |I(x+1,y) - I(x-1,y)| + |I(x,y+1) - I(x,y-1)|
 *              (central differences, the "dual-gradient" energy, L1 norm)
 *
 *    Sobel     e = sum_{c in RGB} |Gx| + |Gy|,   Gx = [-1 0 1; -2 0 2; -1 0 1] (*) I
 *                                                Gy = [-1 -2 -1; 0 0 0; 1 2 1] (*) I
 *
 *  Borders replicate the nearest pixel.  Both are integer-valued (max 6120 per pixel),
 *  which lets the DP and the max-flow seam costs be compared for EXACT equality.
 *
 *  Each pixel needs O(1) work (a fixed 3x3 neighbourhood), so a full map is Theta(m n).
 *  After removing a seam only a narrow band around it changes, which
 *  updateEnergyAfterSeamRemoval() exploits to drop the per-seam cost to Theta(m).
 * ===================================================================================== */
#pragma once

#include "image.h"

namespace sc {

enum class EnergyKind { Gradient, Sobel };
const char* energyName(EnergyKind k);

using EnergyMap = Grid<int32_t>;

/* Energy of a single pixel (borders replicate). */
int32_t pixelEnergy(const Image& img, int x, int y, EnergyKind kind);

/* Full map, Theta(m n).  threads > 1 splits rows across OpenMP threads. */
EnergyMap computeEnergy(const Image& img, EnergyKind kind, int threads = 1);

/* Incremental update after a VERTICAL seam was removed.
 *    energy   : the map BEFORE removal (width w); on return it has width w-1
 *    imgAfter : the image AFTER removal (width w-1)
 *    seam     : seam[y] = removed column in row y
 * Only columns [seam[y]-2, seam[y]+1] of each row can change (proof in the report:
 * the 3x3 window of any pixel further away contains the same original pixels in the
 * same arrangement, because adjacent seam pixels differ by at most one column).      */
void updateEnergyAfterSeamRemoval(EnergyMap& energy, const Image& imgAfter,
                                  const std::vector<int>& seam, EnergyKind kind);

/* Normalised greyscale picture of an energy map (for figures / the GUI). */
Image energyToImage(const EnergyMap& e);

}  // namespace sc
