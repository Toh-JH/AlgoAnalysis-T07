/* =====================================================================================
 *  File : visualize.cpp    Seam overlays and synthetic scenes
 * ===================================================================================== */
#include "visualize.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace sc {

static RGB lerp(RGB a, RGB b, double t) {
    auto mix = [t](uint8_t u, uint8_t v) { return static_cast<uint8_t>(std::lround(u + (v - u) * t)); };
    return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b)};
}

Image overlaySeams(const Image& original, const std::vector<SeamRecord>& seams) {
    Image out = original;
    int nv = 0, nh = 0;
    for (const SeamRecord& s : seams) (s.vertical ? nv : nh) += 1;
    int iv = 0, ih = 0;
    for (const SeamRecord& s : seams) {
        double t = s.vertical ? (nv > 1 ? double(iv++) / (nv - 1) : 0.0)
                              : (nh > 1 ? double(ih++) / (nh - 1) : 0.0);
        RGB c = s.vertical ? lerp({230, 20, 20}, {255, 220, 0}, t) : lerp({20, 60, 230}, {0, 220, 230}, t);
        for (int L : s.pixels) out.set(L % original.w, L / original.w, c);
    }
    return out;
}

Image highlightSeam(const Image& current, const Seam& seam, bool vertical, RGB color) {
    Image out = current;
    for (size_t i = 0; i < seam.idx.size(); ++i) {
        int a = static_cast<int>(i), b = seam.idx[i];
        if (vertical) out.set(b, a, color); else out.set(a, b, color);
    }
    return out;
}

Image upscaleNearest(const Image& img, int f) {
    Image out(img.w * f, img.h * f);
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x) std::memcpy(out.ptr(x, y), img.ptr(x / f, y / f), 3);
    return out;
}

Image makeRandomImage(int w, int h, uint32_t seed) {
    std::mt19937 rng(seed);
    Image img(w, h);
    for (auto& v : img.px) v = static_cast<uint8_t>(rng() & 0xFF);
    return img;
}

/* A stylised version of the brief's Broadway-Tower photo: smooth sky (cheap to carve),
 * detailed towers and a person (expensive), lightly textured grass.                 */
Image makeTowerScene(int w, int h, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> noise(-5, 5);
    Image img(w, h);
    const int horizon = static_cast<int>(h * 0.74);

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            RGB c;
            if (y < horizon) {                                         /* sky gradient */
                c = lerp({52, 110, 200}, {160, 200, 238}, double(y) / horizon);
                int n = noise(rng) / 2;                                /* faint sensor-like noise */
                c = {static_cast<uint8_t>(std::clamp(c.r + n, 0, 255)),
                     static_cast<uint8_t>(std::clamp(c.g + n, 0, 255)),
                     static_cast<uint8_t>(std::clamp(c.b + n, 0, 255))};
            } else {                                                   /* grass */
                double t = double(y - horizon) / std::max(1, h - horizon);
                c = lerp({112, 160, 60}, {76, 128, 40}, t);
                int n = noise(rng);
                c = {static_cast<uint8_t>(std::clamp(c.r + n, 0, 255)),
                     static_cast<uint8_t>(std::clamp(c.g + n, 0, 255)),
                     static_cast<uint8_t>(std::clamp(c.b + n, 0, 255))};
            }
            img.set(x, y, c);
        }

    /* soft clouds: a few elliptical blobs blended towards white */
    std::uniform_real_distribution<double> ur(0.0, 1.0);
    for (int k = 0; k < 4; ++k) {
        double cx = ur(rng) * w * 0.55, cy = (0.08 + ur(rng) * 0.3) * h;
        double rx = (0.10 + ur(rng) * 0.10) * w, ry = (0.03 + ur(rng) * 0.03) * h;
        for (int y = std::max(0, int(cy - ry)); y < std::min(horizon, int(cy + ry) + 1); ++y)
            for (int x = std::max(0, int(cx - rx)); x < std::min(w, int(cx + rx) + 1); ++x) {
                double d = std::pow((x - cx) / rx, 2) + std::pow((y - cy) / ry, 2);
                if (d < 1.0) img.set(x, y, lerp(img.get(x, y), {245, 248, 252}, 0.85 * (1.0 - d)));
            }
    }

    /* towers: stone blocks with mortar lines, dark windows and crenellations */
    auto tower = [&](double fx0, double fx1, double ftop) {
        int x0 = int(fx0 * w), x1 = int(fx1 * w), top = int(ftop * h);
        int bw = std::max(2, w / 80), bh = std::max(2, h / 60);
        for (int y = top; y < horizon + h / 40 && y < h; ++y)
            for (int x = x0; x < x1 && x < w; ++x) {
                bool mortar = ((y - top) % bh == 0) || (((x - x0) + ((y - top) / bh % 2) * bw / 2) % bw == 0);
                RGB c = mortar ? RGB{150, 130, 98} : RGB{198, 176, 134};
                double shade = 0.85 + 0.25 * double(x - x0) / std::max(1, x1 - x0);   /* lit from right */
                int grain = 2 * noise(rng);                                          /* stone texture  */
                c = {static_cast<uint8_t>(std::clamp(c.r * shade + grain, 0.0, 255.0)),
                     static_cast<uint8_t>(std::clamp(c.g * shade + grain, 0.0, 255.0)),
                     static_cast<uint8_t>(std::clamp(c.b * shade + grain, 0.0, 255.0))};
                img.set(x, y, c);
            }
        int merlon = std::max(2, (x1 - x0) / 7);                      /* crenellations */
        for (int x = x0; x < x1 && x < w; ++x)
            if (((x - x0) / merlon) % 2 == 0)
                for (int y = std::max(0, top - merlon); y < top; ++y) img.set(x, y, {186, 165, 124});
        int ww = std::max(1, (x1 - x0) / 6), wh = std::max(2, h / 25);   /* windows */
        for (int row = 0; row < 3; ++row) {
            int wy = top + (horizon - top) * (row + 1) / 4 - wh / 2;
            int wx = (x0 + x1) / 2 - ww / 2;
            for (int y = wy; y < wy + wh && y < h; ++y)
                for (int x = wx; x < wx + ww && x < w; ++x) if (y >= 0) img.set(x, y, {40, 38, 45});
        }
    };
    tower(0.56, 0.66, 0.26);
    tower(0.66, 0.74, 0.34);
    tower(0.74, 0.84, 0.22);

    /* a small person standing on the grass, left of centre */
    int px = int(0.16 * w), py = horizon - h / 60;
    int ph = std::max(4, h / 14), pw = std::max(2, w / 110);
    for (int y = py - ph; y < py; ++y)
        for (int x = px; x < px + pw; ++x)
            if (y >= 0 && x < w) img.set(x, y, y < py - ph + ph / 4 ? RGB{220, 180, 150} : RGB{150, 20, 30});
    return img;
}

}  // namespace sc
