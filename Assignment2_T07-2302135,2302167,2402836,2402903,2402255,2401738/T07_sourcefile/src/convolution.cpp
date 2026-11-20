/* =====================================================================================
 *  File : convolution.cpp  Direct / separable / divide-and-conquer / FFT convolution
 * ===================================================================================== */
#include "convolution.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <future>

namespace sc {

GrayF toGrayF(const Image& img) {
    GrayF g(img.w, img.h);
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const uint8_t* p = img.ptr(x, y);
            g.at(x, y) = 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
        }
    return g;
}

/* Replicate-pad by r on every side, so every method below computes a plain "valid"
 * correlation on the padded image and they all agree at the borders.                */
static GrayF padReplicate(const GrayF& img, int r) {
    GrayF p(img.w + 2 * r, img.h + 2 * r);
    for (int y = 0; y < p.h; ++y) {
        int sy = std::clamp(y - r, 0, img.h - 1);
        for (int x = 0; x < p.w; ++x)
            p.at(x, y) = img.at(std::clamp(x - r, 0, img.w - 1), sy);
    }
    return p;
}

/* ------------------------------------------------------------------------------------ *
 *  Direct:  out(x,y) = sum_{dy,dx} K(dx,dy) * P(x+dx, y+dy)        Theta(m n k^2)
 * ------------------------------------------------------------------------------------ */
static void directRect(const GrayF& P, const GrayF& K, GrayF& out, int x0, int x1, int y0, int y1) {
    const int k = K.w;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            double s = 0.0;
            for (int dy = 0; dy < k; ++dy) {
                const double* prow = P.row(y + dy) + x;
                const double* krow = K.row(dy);
                for (int dx = 0; dx < k; ++dx) s += krow[dx] * prow[dx];
            }
            out.at(x, y) = s;
        }
}

GrayF convolveDirect(const GrayF& img, const GrayF& kernel) {
    GrayF P = padReplicate(img, kernel.w / 2), out(img.w, img.h);
    directRect(P, kernel, out, 0, img.w, 0, img.h);
    return out;
}

/* ------------------------------------------------------------------------------------ *
 *  Separable:  K = colK (outer) rowK  ->  two 1-D passes,           Theta(m n k)
 * ------------------------------------------------------------------------------------ */
GrayF convolveSeparable(const GrayF& img, const std::vector<double>& colK, const std::vector<double>& rowK) {
    const int k = static_cast<int>(colK.size()), r = k / 2;
    GrayF P = padReplicate(img, r);
    GrayF tmp(P.w, img.h);                                    /* vertical pass */
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < P.w; ++x) {
            double s = 0.0;
            for (int d = 0; d < k; ++d) s += colK[static_cast<size_t>(d)] * P.at(x, y + d);
            tmp.at(x, y) = s;
        }
    GrayF out(img.w, img.h);                                  /* horizontal pass */
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            double s = 0.0;
            const double* t = tmp.row(y) + x;
            for (int d = 0; d < k; ++d) s += rowK[static_cast<size_t>(d)] * t[d];
            out.at(x, y) = s;
        }
    return out;
}

/* ------------------------------------------------------------------------------------ *
 *  Spatial divide and conquer:
 *     DC(R) = if |R| <= leaf^2 : direct(R)
 *             else             : DC(R1); DC(R2); DC(R3); DC(R4)    (four quadrants)
 *  T(A) = 4 T(A/4) + Theta(1)  with leaves costing Theta(leaf^2 k^2)
 *       -> Theta(A k^2) total: no asymptotic gain, the work is identical to direct.
 *  Quadrants are independent, so the top `parallelDepth` levels run concurrently.
 * ------------------------------------------------------------------------------------ */
static void dcRect(const GrayF& P, const GrayF& K, GrayF& out,
                   int x0, int x1, int y0, int y1, int leaf, int depth, int parallelDepth) {
    if ((x1 - x0) <= leaf && (y1 - y0) <= leaf) { directRect(P, K, out, x0, x1, y0, y1); return; }
    int xm = (x1 - x0) > leaf ? (x0 + x1) / 2 : x1;           /* only split dims > leaf */
    int ym = (y1 - y0) > leaf ? (y0 + y1) / 2 : y1;
    struct R { int x0, x1, y0, y1; };
    R parts[4] = {{x0, xm, y0, ym}, {xm, x1, y0, ym}, {x0, xm, ym, y1}, {xm, x1, ym, y1}};
    if (depth < parallelDepth) {
        std::vector<std::future<void>> fs;
        for (const R& q : parts)
            if (q.x0 < q.x1 && q.y0 < q.y1)
                fs.push_back(std::async(std::launch::async, [&, q] {
                    dcRect(P, K, out, q.x0, q.x1, q.y0, q.y1, leaf, depth + 1, parallelDepth); }));
        for (auto& f : fs) f.get();
    } else {
        for (const R& q : parts)
            if (q.x0 < q.x1 && q.y0 < q.y1)
                dcRect(P, K, out, q.x0, q.x1, q.y0, q.y1, leaf, depth + 1, parallelDepth);
    }
}

GrayF convolveDivideConquer(const GrayF& img, const GrayF& kernel, int leafSize, int parallelDepth) {
    GrayF P = padReplicate(img, kernel.w / 2), out(img.w, img.h);
    dcRect(P, kernel, out, 0, img.w, 0, img.h, std::max(1, leafSize), 0, parallelDepth);
    return out;
}

/* ------------------------------------------------------------------------------------ *
 *  FFT convolution (Cooley-Tukey radix-2, iterative form of the D&C recursion
 *  X = FFT(even half) combined with twiddle * FFT(odd half)).
 * ------------------------------------------------------------------------------------ */
using cd = std::complex<double>;

static void fft1d(cd* a, int n, bool inverse) {
    for (int i = 1, j = 0; i < n; ++i) {                      /* bit-reversal permutation */
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    const double PI = std::acos(-1.0);
    for (int len = 2; len <= n; len <<= 1) {                  /* combine halves bottom-up */
        double ang = 2 * PI / len * (inverse ? 1 : -1);
        cd wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            cd w(1);
            for (int j = 0; j < len / 2; ++j) {
                cd u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse) for (int i = 0; i < n; ++i) a[i] /= n;
}

static void fft2d(std::vector<cd>& a, int nx, int ny, bool inverse) {
    for (int y = 0; y < ny; ++y) fft1d(&a[static_cast<size_t>(y) * nx], nx, inverse);
    std::vector<cd> col(static_cast<size_t>(ny));
    for (int x = 0; x < nx; ++x) {
        for (int y = 0; y < ny; ++y) col[static_cast<size_t>(y)] = a[static_cast<size_t>(y) * nx + x];
        fft1d(col.data(), ny, inverse);
        for (int y = 0; y < ny; ++y) a[static_cast<size_t>(y) * nx + x] = col[static_cast<size_t>(y)];
    }
}

static int nextPow2(int v) { int p = 1; while (p < v) p <<= 1; return p; }

GrayF convolveFFT(const GrayF& img, const GrayF& kernel) {
    const int k = kernel.w;
    GrayF P = padReplicate(img, k / 2);
    const int nx = nextPow2(P.w + k - 1), ny = nextPow2(P.h + k - 1);   /* no wrap-around */
    std::vector<cd> A(static_cast<size_t>(nx) * ny), B(A.size());
    for (int y = 0; y < P.h; ++y)
        for (int x = 0; x < P.w; ++x) A[static_cast<size_t>(y) * nx + x] = P.at(x, y);
    for (int y = 0; y < k; ++y)                                /* flip: correlation -> convolution */
        for (int x = 0; x < k; ++x) B[static_cast<size_t>(y) * nx + x] = kernel.at(k - 1 - x, k - 1 - y);
    fft2d(A, nx, ny, false);
    fft2d(B, nx, ny, false);
    for (size_t i = 0; i < A.size(); ++i) A[i] *= B[i];         /* convolution theorem */
    fft2d(A, nx, ny, true);
    GrayF out(img.w, img.h);
    for (int y = 0; y < img.h; ++y)                            /* valid part starts at k-1 */
        for (int x = 0; x < img.w; ++x)
            out.at(x, y) = A[static_cast<size_t>(y + k - 1) * nx + (x + k - 1)].real();
    return out;
}

double maxAbsDiff(const GrayF& a, const GrayF& b) {
    double m = 0.0;
    for (size_t i = 0; i < a.d.size(); ++i) m = std::max(m, std::fabs(a.d[i] - b.d[i]));
    return m;
}

}  // namespace sc
