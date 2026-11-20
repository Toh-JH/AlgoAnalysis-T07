/* =====================================================================================
 *  CSD3130 / CSD3131  Algorithm Analysis - Assignment 2 : Seam Carving
 *  File : image.h          RGB image container, 2-D grids, image I/O, small helpers
 *
 *  Image I/O is deliberately dependency-free so the whole project builds from a bare
 *  compiler (and on the Linux CI runner):
 *    - BMP (24/32-bit, uncompressed) and PPM (P3/P6) are read/written by hand.
 *    - On Windows, PNG / JPEG / GIF / TIFF are additionally handled through WIC (the
 *      Windows Imaging Component that ships with the OS).  WIC is only used to decode
 *      and encode pixels - every seam-carving step is our own code, as the brief asks.
 * ===================================================================================== */
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

namespace sc {

/* ------------------------------------------------------------------------------------ *
 *  Grid<T> : a dense row-major 2-D array.  Used for energy maps, DP tables, index maps.
 * ------------------------------------------------------------------------------------ */
template <typename T>
struct Grid {
    int w = 0, h = 0;
    std::vector<T> d;

    Grid() = default;
    Grid(int width, int height, T fill = T()) : w(width), h(height),
        d(static_cast<size_t>(width) * static_cast<size_t>(height), fill) {}

    T&       at(int x, int y)       { return d[static_cast<size_t>(y) * w + x]; }
    const T& at(int x, int y) const { return d[static_cast<size_t>(y) * w + x]; }
    T*       row(int y)             { return d.data() + static_cast<size_t>(y) * w; }
    const T* row(int y) const       { return d.data() + static_cast<size_t>(y) * w; }
    bool     empty() const          { return w == 0 || h == 0; }
};

/* Swap rows and columns.  Horizontal seams are found as vertical seams of the transpose. */
template <typename T>
Grid<T> transpose(const Grid<T>& g) {
    Grid<T> t(g.h, g.w);
    for (int y = 0; y < g.h; ++y)
        for (int x = 0; x < g.w; ++x)
            t.at(y, x) = g.at(x, y);
    return t;
}

/* ------------------------------------------------------------------------------------ *
 *  Image : 8-bit RGB, interleaved, row-major (px[(y*w + x)*3 + c]).
 * ------------------------------------------------------------------------------------ */
struct RGB { uint8_t r = 0, g = 0, b = 0; };

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;

    Image() = default;
    Image(int width, int height) : w(width), h(height),
        px(static_cast<size_t>(width) * static_cast<size_t>(height) * 3, 0) {}

    uint8_t*       ptr(int x, int y)       { return px.data() + (static_cast<size_t>(y) * w + x) * 3; }
    const uint8_t* ptr(int x, int y) const { return px.data() + (static_cast<size_t>(y) * w + x) * 3; }

    RGB  get(int x, int y) const { const uint8_t* p = ptr(x, y); return {p[0], p[1], p[2]}; }
    void set(int x, int y, RGB c) { uint8_t* p = ptr(x, y); p[0] = c.r; p[1] = c.g; p[2] = c.b; }
    bool empty() const { return w == 0 || h == 0; }
    bool operator==(const Image& o) const { return w == o.w && h == o.h && px == o.px; }
};

Image transpose(const Image& img);

/* Integer luma, 0..255 (ITU-R BT.601 weights in 8.8 fixed point). Used by forward energy. */
Grid<uint8_t> toGray(const Image& img);

/* Load / save.  Format chosen by file extension.  Throws std::runtime_error on failure. */
Image loadImage(const std::string& path);
void  saveImage(const Image& img, const std::string& path);

/* Format-specific entry points (always available, on every platform). */
Image loadBMP(const std::string& path);
void  saveBMP(const Image& img, const std::string& path);
Image loadPPM(const std::string& path);
void  savePPM(const Image& img, const std::string& path);

/* True when PNG/JPEG can be read/written on this build (Windows + WIC). */
bool hasNativeCodecs();

/* Baselines for the "scaling vs cropping vs seam carving" comparison in the report. */
Image resizeBilinear(const Image& img, int newW, int newH);
Image cropCenter(const Image& img, int newW, int newH);

/* Lay images out left-to-right on a white canvas with a gap (for comparison figures). */
Image hconcat(const std::vector<Image>& imgs, int gap = 8);

}  // namespace sc
