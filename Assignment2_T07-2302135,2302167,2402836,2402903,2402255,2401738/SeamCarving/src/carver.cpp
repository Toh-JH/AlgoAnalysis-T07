/* =====================================================================================
 *  File : carver.cpp       Seam removal loop, seam insertion, multi-size maps
 * ===================================================================================== */
#include "carver.h"

#include <algorithm>
#include <chrono>
#include <climits>

namespace sc {

const char* algorithmName(Algorithm a) {
    switch (a) {
        case Algorithm::DP:       return "dp";
        case Algorithm::Greedy:   return "greedy";
        case Algorithm::GraphCut: return "graphcut";
    }
    return "?";
}

bool parseAlgorithm(const std::string& s, Algorithm& out) {
    if (s == "dp")       { out = Algorithm::DP;       return true; }
    if (s == "greedy")   { out = Algorithm::Greedy;   return true; }
    if (s == "graphcut") { out = Algorithm::GraphCut; return true; }
    return false;
}

using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

/* ==================================================================================== *
 *  SeamCarver                                                                          *
 * ==================================================================================== */

SeamCarver::SeamCarver(const Image& img, const CarveOptions& opt)
    : opt_(opt), img_(img), origin_(img.w, img.h), W0_(img.w), H0_(img.h) {
    if (img.empty()) throw std::invalid_argument("empty image");
    if (opt.forwardEnergy && opt.algo != Algorithm::DP)
        throw std::invalid_argument("forward energy is a DP formulation: use --algo dp");
    for (size_t i = 0; i < origin_.d.size(); ++i) origin_.d[i] = static_cast<int32_t>(i);
    recomputeEnergy();
}

void SeamCarver::recomputeEnergy() {
    auto t0 = Clock::now();
    if (opt_.forwardEnergy) gray_ = toGray(img_);
    else                    energy_ = computeEnergy(img_, opt_.energy, opt_.threads);
    stats_.energyMs += msSince(t0);
}

/* Internal invariant: seams are always vertical in the internal orientation.
 * Gradient and Sobel energies are symmetric under transposition (|Gx|+|Gy| just swaps
 * its two terms), so the energy map is transposed instead of being recomputed.      */
void SeamCarver::orient(bool vertical) {
    bool want = !vertical;
    if (want == transposed_) return;
    auto t0 = Clock::now();
    img_    = transpose(img_);
    origin_ = transpose(origin_);
    if (opt_.forwardEnergy) gray_ = transpose(gray_);
    else                    energy_ = transpose(energy_);
    transposed_ = want;
    stats_.removeMs += msSince(t0);
}

Image SeamCarver::image() const { return transposed_ ? transpose(img_) : img_; }

EnergyMap SeamCarver::energy() {
    if (opt_.forwardEnergy) return computeEnergy(image(), opt_.energy);
    return transposed_ ? transpose(energy_) : energy_;
}

Seam SeamCarver::findSeam(bool vertical) {
    orient(vertical);
    auto t0 = Clock::now();
    Seam s;
    if (opt_.forwardEnergy) {
        s = findVerticalSeamForwardDP(gray_);
    } else {
        switch (opt_.algo) {
            case Algorithm::DP:
                s = opt_.threads > 1 ? findVerticalSeamDPParallel(energy_, opt_.threads)
                  : opt_.compactDP   ? findVerticalSeamDPCompact(energy_)
                                     : findVerticalSeamDP(energy_);
                break;
            case Algorithm::Greedy:
                s = findVerticalSeamGreedy(energy_);
                break;
            case Algorithm::GraphCut: {
                GraphCutOptions g;
                g.algo = opt_.flow;
                s = findVerticalSeamGraphCut(energy_, g);
                break;
            }
        }
    }
    stats_.findMs += msSince(t0);
    return s;
}

void SeamCarver::removeSeam(bool vertical, const Seam& seam) {
    orient(vertical);
    if (img_.w <= 1) throw std::runtime_error("image is already 1 pixel in that direction");
    if (!isValidSeam(seam.idx, img_.h, img_.w)) throw std::invalid_argument("invalid seam");

    SeamRecord rec;                                         /* remember it in original coords */
    rec.vertical = vertical;
    rec.cost = seam.cost;
    rec.pixels.resize(static_cast<size_t>(img_.h));
    for (int y = 0; y < img_.h; ++y) rec.pixels[static_cast<size_t>(y)] = origin_.at(seam.idx[static_cast<size_t>(y)], y);
    history_.push_back(std::move(rec));

    auto t0 = Clock::now();
    img_    = removeVerticalSeam(img_, seam.idx);
    origin_ = removeVerticalSeam(origin_, seam.idx);
    stats_.removeMs += msSince(t0);

    t0 = Clock::now();
    if (opt_.forwardEnergy)          gray_ = removeVerticalSeam(gray_, seam.idx);
    else if (opt_.incrementalEnergy) updateEnergyAfterSeamRemoval(energy_, img_, seam.idx, opt_.energy);
    else                             energy_ = computeEnergy(img_, opt_.energy, opt_.threads);
    stats_.energyMs += msSince(t0);

    stats_.seams += 1;
    stats_.totalCost += seam.cost;
}

/* ==================================================================================== *
 *  Multi-size map and seam insertion                                                   *
 * ==================================================================================== */

static RemovalMap buildMap(const Image& img, int steps, const CarveOptions& opt,
                           const std::function<bool(int, int)>& progress,
                           std::vector<SeamRecord>* history, CarveStats* stats) {
    steps = std::clamp(steps, 0, img.w - 1);
    SeamCarver c(img, opt);
    for (int s = 0; s < steps; ++s) {
        if (progress && !progress(s, steps)) break;
        c.step(true);
    }
    RemovalMap map;
    map.removedAt = Grid<int32_t>(img.w, img.h, INT32_MAX);
    map.steps = static_cast<int>(c.history().size());
    for (int s = 0; s < map.steps; ++s)
        for (int L : c.history()[static_cast<size_t>(s)].pixels) map.removedAt.d[static_cast<size_t>(L)] = s;
    if (history) *history = c.history();
    if (stats) {
        const CarveStats& cs = c.stats();
        stats->seams += cs.seams; stats->totalCost += cs.totalCost;
        stats->energyMs += cs.energyMs; stats->findMs += cs.findMs; stats->removeMs += cs.removeMs;
    }
    return map;
}

RemovalMap buildVerticalRemovalMap(const Image& img, int steps, const CarveOptions& opt,
                                   const std::function<bool(int, int)>& progress) {
    return buildMap(img, steps, opt, progress, nullptr, nullptr);
}

Image renderWidth(const Image& original, const RemovalMap& map, int targetW) {
    const int w = original.w, h = original.h;
    if (targetW < w - map.steps || targetW > w + map.steps)
        throw std::out_of_range("target width outside the precomputed range");
    Image out(targetW, h);
    if (targetW <= w) {                                   /* keep pixels removed at step >= k */
        const int k = w - targetW;
        for (int y = 0; y < h; ++y) {
            int ox = 0;
            for (int x = 0; x < w; ++x)
                if (map.removedAt.at(x, y) >= k) std::memcpy(out.ptr(ox++, y), original.ptr(x, y), 3);
        }
    } else {                                              /* duplicate pixels removed at step < k */
        const int k = targetW - w;
        for (int y = 0; y < h; ++y) {
            int ox = 0;
            for (int x = 0; x < w; ++x) {
                std::memcpy(out.ptr(ox++, y), original.ptr(x, y), 3);
                if (map.removedAt.at(x, y) < k) {
                    /* the inserted pixel is the average of the seam pixel and its right
                     * neighbour, so the new column blends in instead of repeating */
                    const uint8_t* a = original.ptr(x, y);
                    const uint8_t* b = original.ptr(std::min(x + 1, w - 1), y);
                    uint8_t* d = out.ptr(ox++, y);
                    for (int c = 0; c < 3; ++c) d[c] = static_cast<uint8_t>((a[c] + b[c] + 1) / 2);
                }
            }
        }
    }
    return out;
}

static Image enlargeWidthImpl(const Image& img, int k, const CarveOptions& opt,
                              std::vector<SeamRecord>* seams, CarveStats* stats) {
    Image cur = img;
    bool first = true;
    while (k > 0) {
        if (cur.w == 1) {                                  /* the single column is the seam */
            Image out(cur.w + k, cur.h);
            for (int y = 0; y < cur.h; ++y)
                for (int x = 0; x < out.w; ++x) std::memcpy(out.ptr(x, y), cur.ptr(0, y), 3);
            return out;
        }
        /* passes of at most half the width: beyond that the "k cheapest seams" start to
         * include important content and every pixel would get duplicated */
        int chunk = std::min(k, std::max(1, cur.w / 2));
        std::vector<SeamRecord> hist;
        RemovalMap map = buildMap(cur, chunk, opt, nullptr, &hist, stats);
        if (first && seams) seams->insert(seams->end(), hist.begin(), hist.end());
        cur = renderWidth(cur, map, cur.w + map.steps);
        k -= map.steps;
        first = false;
    }
    return cur;
}

Image enlargeWidth(const Image& img, int k, const CarveOptions& opt, std::vector<SeamRecord>* seams) {
    return enlargeWidthImpl(img, k, opt, seams, nullptr);
}

/* ==================================================================================== *
 *  carveTo : the public "resize to W x H" entry point                                  *
 * ==================================================================================== */

bool shrinkTo(SeamCarver& c, int targetW, int targetH, const StepCallback& cb) {
    const int dW = std::max(0, c.width() - targetW), dH = std::max(0, c.height() - targetH);
    const int total = dW + dH;
    const Order order = c.options().order;
    int doneW = 0, doneH = 0;
    for (int step = 0; step < total; ++step) {
        bool vertical;
        if (doneW == dW)      vertical = false;
        else if (doneH == dH) vertical = true;
        else if (order == Order::WidthFirst)  vertical = true;
        else if (order == Order::HeightFirst) vertical = false;
        else  /* Alternate: keep both directions progressing at the same rate */
            vertical = static_cast<long long>(doneW) * dH <= static_cast<long long>(doneH) * dW;
        Seam s = c.findSeam(vertical);
        if (cb && !cb(c.image(), s, vertical, step, total)) return false;   /* cancelled */
        c.removeSeam(vertical, s);
        (vertical ? doneW : doneH) += 1;
    }
    return true;
}

Image carveTo(const Image& img, int targetW, int targetH, const CarveOptions& opt,
              const StepCallback& cb, CarveStats* stats, std::vector<SeamRecord>* seams) {
    if (targetW < 1 || targetH < 1) throw std::invalid_argument("target size must be at least 1x1");
    auto t0 = Clock::now();
    CarveStats local;
    Image cur = img;

    /* ---- shrink: remove seams ---- */
    const int dW = std::max(0, img.w - targetW), dH = std::max(0, img.h - targetH);
    if (dW + dH > 0) {
        SeamCarver c(img, opt);
        shrinkTo(c, targetW, targetH, cb);
        cur = c.image();
        local = c.stats();
        if (seams) *seams = c.history();
    }

    /* ---- grow: insert seams ---- */
    const bool shrank = dW + dH > 0;
    if (targetW > cur.w) {
        std::vector<SeamRecord> rec;
        cur = enlargeWidthImpl(cur, targetW - cur.w, opt, shrank ? nullptr : &rec, &local);
        if (seams && !shrank) seams->insert(seams->end(), rec.begin(), rec.end());
    }
    if (targetH > cur.h) {
        std::vector<SeamRecord> rec;
        const int natW = cur.w;
        Image t = enlargeWidthImpl(transpose(cur), targetH - cur.h, opt, shrank ? nullptr : &rec, &local);
        cur = transpose(t);
        if (seams && !shrank && targetW <= img.w) {
            for (SeamRecord& r : rec) {                      /* transposed -> natural coords */
                r.vertical = false;
                const int Wt = img.h;
                for (int& L : r.pixels) { int yT = L / Wt, xT = L % Wt; L = xT * natW + yT; }
            }
            seams->insert(seams->end(), rec.begin(), rec.end());
        }
    }
    local.totalMs = msSince(t0);
    if (stats) *stats = local;
    return cur;
}

}  // namespace sc
