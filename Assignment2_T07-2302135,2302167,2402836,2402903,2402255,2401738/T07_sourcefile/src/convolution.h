/* =====================================================================================
 *  File : convolution.h    Item 2(b) - can DIVIDE AND CONQUER speed up convolution?
 *
 *  Four ways of computing the same 2-D correlation  out = K (*) I  (replicate borders)
 *  for an m x n image and a k x k kernel:
 *
 *    convolveDirect        Theta(m n k^2)        the textbook double loop
 *    convolveSeparable     Theta(m n k)          only for rank-1 kernels (Sobel is one:
 *                                                Gx = [1 2 1]^T x [-1 0 1])
 *    convolveDivideConquer Theta(m n k^2)        spatial D&C: split the output into four
 *                                                quadrants recursively, leaves done
 *                                                directly.  Same work, + recursion; its
 *                                                value is cache locality & parallelism.
 *    convolveFFT           Theta(P log P)        frequency-domain D&C (Cooley-Tukey FFT,
 *                          P = padded m n        T(N) = 2T(N/2) + Theta(N)).  Independent
 *                                                of k - wins only when k is large.
 *
 *  For seam carving k = 3 and the direct method is already Theta(m n) - optimal, since
 *  every pixel must be read.  The benchmark measures the crossover point.
 * ===================================================================================== */
#pragma once

#include "image.h"

namespace sc {

using GrayF = Grid<double>;

GrayF toGrayF(const Image& img);

GrayF convolveDirect(const GrayF& img, const GrayF& kernel);
GrayF convolveSeparable(const GrayF& img, const std::vector<double>& colK, const std::vector<double>& rowK);
GrayF convolveDivideConquer(const GrayF& img, const GrayF& kernel, int leafSize = 64, int parallelDepth = 0);
GrayF convolveFFT(const GrayF& img, const GrayF& kernel);

/* Max |a - b| over all cells (sizes must match). */
double maxAbsDiff(const GrayF& a, const GrayF& b);

}  // namespace sc
