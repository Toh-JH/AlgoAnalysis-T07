/* =====================================================================================
 *  File : visualize.h      Bonus 7(a) - seam visualisation, and synthetic test scenes
 * ===================================================================================== */
#pragma once

#include "carver.h"

namespace sc {

/* Paint every removed seam onto the ORIGINAL image (seams are stored in original
 * coordinates).  Vertical seams: red -> yellow in removal order; horizontal seams:
 * blue -> cyan.  This is the "all the seams visualised" figure.                    */
Image overlaySeams(const Image& original, const std::vector<SeamRecord>& seams);

/* Draw one seam (natural coordinates) on the current image - one animation frame. */
Image highlightSeam(const Image& current, const Seam& seam, bool vertical, RGB color = {255, 0, 0});

/* Nearest-neighbour enlargement, for making tiny test images visible in figures. */
Image upscaleNearest(const Image& img, int factor);

/* Deterministic synthetic scenes (no downloads needed for tests / CI / benchmarks). */
Image makeTowerScene(int w, int h, uint32_t seed = 7);     /* sky, clouds, towers, grass, person */
Image makeRandomImage(int w, int h, uint32_t seed);        /* uniform noise                       */

}  // namespace sc
