/* =====================================================================================
 *  File : energy.cpp       Energy maps (gradient / Sobel) and incremental update
 * ===================================================================================== */
#include "energy.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace sc {

const char* energyName(EnergyKind k) { return k == EnergyKind::Sobel ? "sobel" : "gradient"; }

/* Energy at (x, y) given the three clamped rows (above / here / below) and clamped
 * column offsets.  Written once and shared by the full and incremental paths so the
 * two can never disagree.                                                             */
static inline int32_t energyAt(const uint8_t* up, const uint8_t* mid, const uint8_t* dn,
                               int xm, int x, int xp, EnergyKind kind) {
    int32_t e = 0;
    for (int c = 0; c < 3; ++c) {
        if (kind == EnergyKind::Gradient) {
            int dx = mid[xp * 3 + c] - mid[xm * 3 + c];
            int dy = dn[x * 3 + c]   - up[x * 3 + c];
            e += std::abs(dx) + std::abs(dy);
        } else {
            /* Sobel: Gx = (right column) - (left column) weighted 1-2-1,
             *        Gy = (bottom row)   - (top row)     weighted 1-2-1             */
            int gx = (up[xp * 3 + c] + 2 * mid[xp * 3 + c] + dn[xp * 3 + c])
                   - (up[xm * 3 + c] + 2 * mid[xm * 3 + c] + dn[xm * 3 + c]);
            int gy = (dn[xm * 3 + c] + 2 * dn[x * 3 + c] + dn[xp * 3 + c])
                   - (up[xm * 3 + c] + 2 * up[x * 3 + c] + up[xp * 3 + c]);
            e += std::abs(gx) + std::abs(gy);
        }
    }
    return e;
}

static inline void energyRow(const Image& img, int y, int x0, int x1, EnergyKind kind, int32_t* out) {
    const uint8_t* up  = img.ptr(0, std::max(y - 1, 0));
    const uint8_t* mid = img.ptr(0, y);
    const uint8_t* dn  = img.ptr(0, std::min(y + 1, img.h - 1));
    for (int x = x0; x <= x1; ++x)
        out[x] = energyAt(up, mid, dn, std::max(x - 1, 0), x, std::min(x + 1, img.w - 1), kind);
}

int32_t pixelEnergy(const Image& img, int x, int y, EnergyKind kind) {
    int32_t e = 0;
    const uint8_t* up  = img.ptr(0, std::max(y - 1, 0));
    const uint8_t* mid = img.ptr(0, y);
    const uint8_t* dn  = img.ptr(0, std::min(y + 1, img.h - 1));
    e = energyAt(up, mid, dn, std::max(x - 1, 0), x, std::min(x + 1, img.w - 1), kind);
    return e;
}

EnergyMap computeEnergy(const Image& img, EnergyKind kind, int threads) {
    EnergyMap e(img.w, img.h);
    const int h = img.h;
    (void)threads;
    /* Every pixel is independent -> embarrassingly parallel over rows. */
#pragma omp parallel for num_threads(threads) schedule(static) if (threads > 1)
    for (int y = 0; y < h; ++y)
        energyRow(img, y, 0, img.w - 1, kind, e.row(y));
    return e;
}

void updateEnergyAfterSeamRemoval(EnergyMap& energy, const Image& imgAfter,
                                  const std::vector<int>& seam, EnergyKind kind) {
    const int w = imgAfter.w;                 /* new width = old width - 1 */
    EnergyMap next(w, imgAfter.h);
    for (int y = 0; y < imgAfter.h; ++y) {
        const int c = seam[static_cast<size_t>(y)];
        const int32_t* src = energy.row(y);
        int32_t*       dst = next.row(y);
        /* 1. drop the seam cell: copy the parts left and right of it */
        if (c > 0)     std::memcpy(dst, src, static_cast<size_t>(c) * sizeof(int32_t));
        if (c < w)     std::memcpy(dst + c, src + c + 1, static_cast<size_t>(w - c) * sizeof(int32_t));
        /* 2. recompute only the band whose 3x3 window touched the seam */
        int x0 = std::max(0, c - 2), x1 = std::min(w - 1, c + 1);
        if (x0 <= x1) energyRow(imgAfter, y, x0, x1, kind, dst);
    }
    energy = std::move(next);
}

Image energyToImage(const EnergyMap& e) {
    int32_t mx = 1;
    for (int32_t v : e.d) mx = std::max(mx, v);
    Image out(e.w, e.h);
    for (size_t i = 0; i < e.d.size(); ++i) {
        /* square-root tone curve so faint texture is still visible next to strong edges */
        auto g = static_cast<uint8_t>(std::lround(255.0 * std::sqrt(static_cast<double>(e.d[i]) / mx)));
        out.px[i * 3] = out.px[i * 3 + 1] = out.px[i * 3 + 2] = g;
    }
    return out;
}

}  // namespace sc
