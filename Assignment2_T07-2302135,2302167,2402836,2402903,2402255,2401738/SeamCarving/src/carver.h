/* =====================================================================================
 *  File : carver.h         Step 1(c) - REMOVE THE SEAM AND REPEAT (plus enlarging)
 *
 *  SeamCarver keeps the working image, its energy map and an "origin" map (which
 *  original pixel each current pixel came from).  One step = find seam + remove it.
 *
 *  Horizontal seams reuse every vertical-seam finder on the TRANSPOSED state; the state
 *  is only transposed when the seam direction changes (once for width-then-height),
 *  so the transpose costs Theta(m n) per direction switch, not per seam.
 *
 *  Per-seam cost (m rows, n columns):
 *      energy   Theta(m n) full recompute,  Theta(m) with incremental update
 *      find     DP Theta(m n) | greedy Theta(m + n) | graph cut: max-flow on ~m n nodes
 *      remove   Theta(m n)   (each row shifts left by one)
 *  Removing k seams: Theta(k m n) for DP.
 *
 *  Enlarging (Avidan & Shamir sec. 3.5): find the k cheapest seams by carving a COPY,
 *  then duplicate those pixels in the original - re-using a single seam k times would
 *  just stretch one column.
 * ===================================================================================== */
#pragma once

#include "seam_dp.h"
#include "seam_graphcut.h"
#include "seam_greedy.h"

#include <functional>

namespace sc {

enum class Algorithm { DP, Greedy, GraphCut };
enum class Order { WidthFirst, HeightFirst, Alternate };

const char* algorithmName(Algorithm a);
bool parseAlgorithm(const std::string& s, Algorithm& out);

struct CarveOptions {
    Algorithm   algo              = Algorithm::DP;
    EnergyKind  energy            = EnergyKind::Sobel;
    bool        forwardEnergy     = false;   /* DP only: Rubinstein forward energy        */
    bool        incrementalEnergy = true;    /* Theta(m) band update instead of Theta(mn) */
    bool        compactDP         = true;    /* rolling rows + int8 parents               */
    int         threads           = 1;       /* >1: OpenMP energy + parallel-row DP       */
    MaxFlowAlgo flow              = MaxFlowAlgo::Dinic;
    Order       order             = Order::WidthFirst;
};

/* One removed seam, in ORIGINAL image coordinates (linear index y*W0 + x). */
struct SeamRecord {
    bool             vertical = true;
    std::vector<int> pixels;
    int64_t          cost = 0;
};

struct CarveStats {
    int     seams = 0;
    int64_t totalCost = 0;
    double  energyMs = 0, findMs = 0, removeMs = 0, totalMs = 0;
};

/* Called before each seam is removed with the current image and the chosen seam.
 * Return false to cancel (used by the GUI's Stop button).                           */
using StepCallback = std::function<bool(const Image& current, const Seam& seam, bool vertical,
                                        int step, int totalSteps)>;

class SeamCarver {
public:
    SeamCarver(const Image& img, const CarveOptions& opt);

    int   width()  const { return transposed_ ? img_.h : img_.w; }
    int   height() const { return transposed_ ? img_.w : img_.h; }
    Image image() const;                          /* current image, natural orientation */
    EnergyMap energy();                           /* current energy, natural orientation */

    /* vertical: idx[y] = x ;  horizontal: idx[x] = y  (natural coordinates) */
    Seam    findSeam(bool vertical);
    void    removeSeam(bool vertical, const Seam& seam);
    int64_t step(bool vertical) { Seam s = findSeam(vertical); removeSeam(vertical, s); return s.cost; }

    const std::vector<SeamRecord>& history() const { return history_; }
    const CarveStats&              stats()   const { return stats_; }
    const CarveOptions&            options() const { return opt_; }
    int originalWidth()  const { return W0_; }
    int originalHeight() const { return H0_; }

private:
    void orient(bool vertical);                   /* make internal seams vertical */
    void recomputeEnergy();

    CarveOptions  opt_;
    Image         img_;                           /* internal orientation */
    EnergyMap     energy_;
    Grid<uint8_t> gray_;                          /* forward energy only  */
    Grid<int32_t> origin_;                        /* original linear index per pixel */
    bool          transposed_ = false;
    int           W0_, H0_;
    std::vector<SeamRecord> history_;
    CarveStats    stats_;
};

/* Remove seams from an existing carver until it is at most targetW x targetH, choosing
 * directions by opt.order.  Returns false if the callback cancelled.                  */
bool shrinkTo(SeamCarver& c, int targetW, int targetH, const StepCallback& cb = nullptr);

/* Resize to exactly targetW x targetH: seam removal to shrink, seam insertion to grow. */
Image carveTo(const Image& img, int targetW, int targetH, const CarveOptions& opt,
              const StepCallback& cb = nullptr, CarveStats* stats = nullptr,
              std::vector<SeamRecord>* seams = nullptr);

/* Insert k vertical seams (k may exceed the width: done in passes of <= w/2). */
Image enlargeWidth(const Image& img, int k, const CarveOptions& opt,
                   std::vector<SeamRecord>* seams = nullptr);

/* ------------------------------------------------------------------------------------ *
 *  Multi-size map (Avidan & Shamir sec. 4): carve once, remember WHEN each pixel was
 *  removed.  The image at width w-k is then "keep pixels removed at step >= k" and at
 *  width w+k is "duplicate pixels removed at step < k": any width in Theta(m n), which
 *  is what makes the GUI slider real-time.
 * ------------------------------------------------------------------------------------ */
struct RemovalMap {
    Grid<int32_t> removedAt;    /* seam number that removed the pixel, or INT32_MAX */
    int           steps = 0;
};

RemovalMap buildVerticalRemovalMap(const Image& img, int steps, const CarveOptions& opt,
                                   const std::function<bool(int, int)>& progress = nullptr);
Image renderWidth(const Image& original, const RemovalMap& map, int targetW);

}  // namespace sc
