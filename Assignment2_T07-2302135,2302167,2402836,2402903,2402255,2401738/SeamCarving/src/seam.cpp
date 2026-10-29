/* =====================================================================================
 *  File : seam.cpp         Seam validation, cost and removal
 * ===================================================================================== */
#include "seam.h"

#include <cstdlib>

namespace sc {

bool isValidSeam(const std::vector<int>& idx, int length, int span) {
    if (static_cast<int>(idx.size()) != length) return false;
    for (int i = 0; i < length; ++i) {
        int v = idx[static_cast<size_t>(i)];
        if (v < 0 || v >= span) return false;
        if (i > 0 && std::abs(v - idx[static_cast<size_t>(i - 1)]) > 1) return false;
    }
    return true;
}

int64_t verticalSeamEnergy(const EnergyMap& e, const std::vector<int>& idx) {
    int64_t s = 0;
    for (int y = 0; y < e.h; ++y) s += e.at(idx[static_cast<size_t>(y)], y);
    return s;
}

Image removeVerticalSeam(const Image& img, const std::vector<int>& idx) {
    if (img.w <= 1) throw std::runtime_error("cannot remove a vertical seam from a 1-pixel-wide image");
    Image out(img.w - 1, img.h);
    for (int y = 0; y < img.h; ++y) {
        int c = idx[static_cast<size_t>(y)];
        std::memcpy(out.ptr(0, y), img.ptr(0, y), static_cast<size_t>(c) * 3);
        std::memcpy(out.ptr(c, y), img.ptr(c + 1, y), static_cast<size_t>(img.w - 1 - c) * 3);
    }
    return out;
}

}  // namespace sc
