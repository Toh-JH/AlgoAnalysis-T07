/* =====================================================================================
 *  CSD3130 / CSD3131  Algorithm Analysis - Assignment 2 : Seam Carving
 *  File : tests.cpp        Correctness test suite  (exit code 0 = all passed)
 *
 *  The strongest checks are EXHAUSTIVE or CROSS-ALGORITHM:
 *    - DP is compared against brute-force enumeration of every seam (up to 3^(m-1) n)
 *    - graph cut (Edmonds-Karp and Dinic) must give exactly the DP cost on random maps
 *    - all DP variants (full / compact / parallel / native horizontal) give one seam
 *    - incremental energy update must equal a full recomputation after every removal
 *    - the multi-size map must reproduce sequential carving pixel-for-pixel
 *
 *  RUN   tests.exe            all tests, prints [PASS]/[FAIL] lines and a summary
 * ===================================================================================== */
#include "../src/carver.h"
#include "../src/convolution.h"
#include "../src/visualize.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <random>
#include <set>
#include <sstream>

using namespace sc;

/* ==================================================================================== *
 *  TEST HARNESS  (same style as Assignment 1)                                          *
 * ==================================================================================== */

static int g_pass = 0, g_fail = 0;
static void check(const std::string& label, bool ok, const std::string& detail = "") {
    std::cout << (ok ? "    [PASS] " : "    [FAIL] ") << label;
    if (!detail.empty()) std::cout << "  (" << detail << ")";
    std::cout << "\n";
    if (ok) ++g_pass; else ++g_fail;
}
static void section(const std::string& title) { std::cout << "\n== " << title << " ==\n"; }

template <typename F>
static bool throws(F&& f) { try { f(); } catch (const std::exception&) { return true; } return false; }

static EnergyMap randomEnergy(int w, int h, int maxV, std::mt19937& rng) {
    EnergyMap e(w, h);
    std::uniform_int_distribution<int> d(0, maxV);
    for (auto& v : e.d) v = d(rng);
    return e;
}

/* Every vertical seam of an energy map, by depth-first enumeration. */
static void enumerateSeams(const EnergyMap& e, std::vector<int>& cur, int64_t costSoFar,
                           int64_t& best, long long& count) {
    int y = static_cast<int>(cur.size());
    if (y == e.h) { ++count; best = std::min(best, costSoFar); return; }
    int lo = 0, hi = e.w - 1;
    if (y > 0) { lo = std::max(0, cur.back() - 1); hi = std::min(e.w - 1, cur.back() + 1); }
    for (int x = lo; x <= hi; ++x) {
        cur.push_back(x);
        enumerateSeams(e, cur, costSoFar + e.at(x, y), best, count);
        cur.pop_back();
    }
}

/* All seams, materialised (for the forward-energy brute force). */
static void allSeams(int w, int h, std::vector<int>& cur, std::vector<std::vector<int>>& out) {
    int y = static_cast<int>(cur.size());
    if (y == h) { out.push_back(cur); return; }
    int lo = 0, hi = w - 1;
    if (y > 0) { lo = std::max(0, cur.back() - 1); hi = std::min(w - 1, cur.back() + 1); }
    for (int x = lo; x <= hi; ++x) { cur.push_back(x); allSeams(w, h, cur, out); cur.pop_back(); }
}

static std::string tmpPath(const std::string& name) {
    return (std::filesystem::temp_directory_path() / ("seamcarving_test_" + name)).string();
}

/* ==================================================================================== *
 *  TESTS                                                                               *
 * ==================================================================================== */

static void testImageIO() {
    section("Image I/O and helpers");
    Image img = makeRandomImage(13, 7, 1);               /* odd width exercises BMP row padding */
    saveBMP(img, tmpPath("rt.bmp"));
    check("BMP round trip is lossless", loadBMP(tmpPath("rt.bmp")) == img);
    savePPM(img, tmpPath("rt.ppm"));
    check("PPM round trip is lossless", loadPPM(tmpPath("rt.ppm")) == img);
    if (hasNativeCodecs()) {
        saveImage(img, tmpPath("rt.png"));
        check("PNG round trip is lossless (WIC)", loadImage(tmpPath("rt.png")) == img);
    }
    check("transpose(transpose(I)) == I", transpose(transpose(img)) == img);
    Image t = transpose(img);
    check("transpose swaps dimensions", t.w == img.h && t.h == img.w);
    check("loading a missing file throws", throws([] { loadImage("definitely_missing_file.bmp"); }));
}

static void testEnergy() {
    section("Energy maps (item 1a)");
    Image flat(9, 6);
    std::fill(flat.px.begin(), flat.px.end(), uint8_t(123));
    bool allZero = true;
    for (EnergyKind k : {EnergyKind::Gradient, EnergyKind::Sobel})
        for (int32_t v : computeEnergy(flat, k).d) allZero &= (v == 0);
    check("constant image has zero energy everywhere (both kinds)", allZero);

    /* 3x1 grey ramp [0, 100, 200]: hand-computed values (see report, section 2) */
    Image ramp(3, 1);
    for (int x = 0; x < 3; ++x) ramp.set(x, 0, {uint8_t(100 * x), uint8_t(100 * x), uint8_t(100 * x)});
    EnergyMap g = computeEnergy(ramp, EnergyKind::Gradient), s = computeEnergy(ramp, EnergyKind::Sobel);
    check("gradient energy of ramp = [300, 600, 300]", g.d == std::vector<int32_t>{300, 600, 300});
    check("Sobel energy of ramp    = [1200, 2400, 1200]", s.d == std::vector<int32_t>{1200, 2400, 1200});

    Image img = makeRandomImage(31, 17, 2);
    for (EnergyKind k : {EnergyKind::Gradient, EnergyKind::Sobel}) {
        bool sym = computeEnergy(transpose(img), k).d == transpose(computeEnergy(img, k)).d;
        check(std::string("energy(transpose I) == transpose(energy I)  [") + energyName(k) + "]", sym);
        bool par = computeEnergy(img, k, 4).d == computeEnergy(img, k, 1).d;
        check(std::string("4-thread energy == 1-thread energy  [") + energyName(k) + "]", par);
        bool pix = true;
        EnergyMap full = computeEnergy(img, k);
        for (int y = 0; y < img.h; ++y) for (int x = 0; x < img.w; ++x) pix &= full.at(x, y) == pixelEnergy(img, x, y, k);
        check(std::string("pixelEnergy agrees with the full map  [") + energyName(k) + "]", pix);
    }
}

static void testIncrementalEnergy() {
    section("Incremental energy update after seam removal (item 5d improvement)");
    std::mt19937 rng(3);
    for (EnergyKind k : {EnergyKind::Gradient, EnergyKind::Sobel}) {
        Image img = makeRandomImage(40, 30, 4);
        EnergyMap e = computeEnergy(img, k);
        bool ok = true;
        for (int step = 0; step < 30 && ok; ++step) {
            std::vector<int> seam;
            int mode = step % 4;                    /* DP seams + seams hugging both borders */
            if (mode == 0) seam = findVerticalSeamDP(e).idx;
            else if (mode == 1) seam.assign(static_cast<size_t>(img.h), 0);
            else if (mode == 2) seam.assign(static_cast<size_t>(img.h), img.w - 1);
            else {                                  /* random zig-zag seam */
                seam.resize(static_cast<size_t>(img.h));
                seam[0] = static_cast<int>(rng() % static_cast<unsigned>(img.w));
                for (int y = 1; y < img.h; ++y)
                    seam[static_cast<size_t>(y)] = std::clamp(seam[static_cast<size_t>(y - 1)] + int(rng() % 3) - 1, 0, img.w - 1);
            }
            img = removeVerticalSeam(img, seam);
            updateEnergyAfterSeamRemoval(e, img, seam, k);
            ok = (e.d == computeEnergy(img, k).d);
        }
        check(std::string("30 removals: incremental == full recompute  [") + energyName(k) + "]", ok);
    }
}

static void testConvolution() {
    section("Convolution methods agree (item 2b)");
    std::mt19937 rng(5);
    GrayF img = toGrayF(makeRandomImage(37, 23, 6));
    GrayF sobel(3, 3);
    double sx[9] = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
    for (int i = 0; i < 9; ++i) sobel.d[static_cast<size_t>(i)] = sx[i];
    GrayF d = convolveDirect(img, sobel);
    check("separable Sobel == direct Sobel", maxAbsDiff(d, convolveSeparable(img, {1, 2, 1}, {-1, 0, 1})) < 1e-9);
    for (int k : {3, 5, 7, 11}) {
        GrayF K(k, k);
        std::uniform_real_distribution<double> u(-1, 1);
        for (auto& v : K.d) v = u(rng);
        GrayF ref = convolveDirect(img, K);
        std::ostringstream lbl; lbl << "k=" << k << ": ";
        check(lbl.str() + "divide&conquer (leaf 8) == direct", maxAbsDiff(ref, convolveDivideConquer(img, K, 8, 0)) == 0.0);
        check(lbl.str() + "parallel divide&conquer == direct", maxAbsDiff(ref, convolveDivideConquer(img, K, 8, 2)) == 0.0);
        double e = maxAbsDiff(ref, convolveFFT(img, K));
        std::ostringstream det; det << "max error " << e;
        check(lbl.str() + "FFT == direct (to 1e-6)", e < 1e-6, det.str());
    }
}

static void testDPOptimal() {
    section("DP is optimal: exhaustive comparison with brute force (item 1b / 2a)");
    std::mt19937 rng(7);
    int cases = 0; bool ok = true, countOk = true; long long totalSeams = 0;
    for (int m = 1; m <= 7; ++m)
        for (int n = 1; n <= 6; ++n)
            for (int rep = 0; rep < 4; ++rep) {
                EnergyMap e = randomEnergy(n, m, rep % 2 ? 3 : 50, rng);    /* small range = many ties */
                std::vector<int> cur; int64_t best = INT64_MAX; long long cnt = 0;
                enumerateSeams(e, cur, 0, best, cnt);
                Seam s = findVerticalSeamDP(e);
                ok &= s.cost == best && isValidSeam(s.idx, m, n) && verticalSeamEnergy(e, s.idx) == s.cost;
                countOk &= static_cast<long double>(cnt) == countSeams(n, m);
                totalSeams += cnt; ++cases;
            }
    std::ostringstream det; det << cases << " grids, " << totalSeams << " seams enumerated";
    check("DP cost == brute-force minimum on every grid up to 7x6", ok, det.str());
    check("counting DP == number of seams enumerated", countOk);
}

static void testDPVariants() {
    section("DP variants return the same seam");
    std::mt19937 rng(8);
    bool same = true, horiz = true;
    for (int rep = 0; rep < 40; ++rep) {
        int w = 1 + static_cast<int>(rng() % 60), h = 1 + static_cast<int>(rng() % 45);
        EnergyMap e = randomEnergy(w, h, rep % 3 == 0 ? 2 : 1000, rng);
        DPStats sf, sc2;
        Seam a = findVerticalSeamDP(e, &sf), b = findVerticalSeamDPCompact(e, &sc2), c = findVerticalSeamDPParallel(e, 4);
        same &= a.idx == b.idx && a.idx == c.idx && a.cost == b.cost && a.cost == c.cost;
        Seam hz = findHorizontalSeamDP(e), vt = findVerticalSeamDP(transpose(e));
        horiz &= hz.idx == vt.idx && hz.cost == vt.cost && isValidSeam(hz.idx, w, h);
    }
    check("full table == compact == 4-thread parallel (40 random maps, ties included)", same);
    check("native horizontal DP == vertical DP on the transpose", horiz);
    EnergyMap e = randomEnergy(500, 400, 100, rng);
    DPStats sf, sc2;
    findVerticalSeamDP(e, &sf); findVerticalSeamDPCompact(e, &sc2);
    std::ostringstream det; det << sf.tableBytes << " B vs " << sc2.tableBytes << " B";
    check("compact table uses < 1/7 of the full table's memory (500x400)", sc2.tableBytes * 7 < sf.tableBytes, det.str());
}

static void testGreedy() {
    section("Greedy (item 3)");
    std::mt19937 rng(9);
    bool valid = true, neverBetter = true; int worse = 0, total = 0;
    for (int rep = 0; rep < 200; ++rep) {
        EnergyMap e = randomEnergy(2 + static_cast<int>(rng() % 30), 1 + static_cast<int>(rng() % 30), 100, rng);
        Seam g = findVerticalSeamGreedy(e), d = findVerticalSeamDP(e), gb = findVerticalSeamGreedyBestStart(e);
        valid &= isValidSeam(g.idx, e.h, e.w) && verticalSeamEnergy(e, g.idx) == g.cost;
        neverBetter &= g.cost >= d.cost && gb.cost >= d.cost && gb.cost <= g.cost;
        worse += g.cost > d.cost; ++total;
    }
    check("greedy seams are valid and their cost is reported correctly", valid);
    check("greedy cost >= DP cost, best-start greedy in between (200 maps)", neverBetter);
    std::ostringstream det; det << worse << "/" << total << " maps";
    check("greedy is strictly worse than DP on most random maps", worse > total / 2, det.str());

    /* The trap from the report: the cheapest first pixel leads into a wall. */
    EnergyMap trap(3, 3);
    trap.d = {1, 9, 5,
              100, 100, 5,
              100, 100, 5};
    Seam g = findVerticalSeamGreedy(trap), d = findVerticalSeamDP(trap);
    std::ostringstream t; t << "greedy " << g.cost << " vs DP " << d.cost;
    check("trap example: greedy 201, DP 15", g.cost == 201 && d.cost == 15, t.str());
}

static void testMaxFlow() {
    section("Max-flow on a textbook network (CLRS Fig. 26.1, max flow 23)");
    for (MaxFlowAlgo a : {MaxFlowAlgo::EdmondsKarp, MaxFlowAlgo::Dinic}) {
        MaxFlow f(6);
        f.addEdge(0, 1, 16); f.addEdge(0, 2, 13); f.addEdge(1, 3, 12); f.addEdge(2, 1, 4);
        f.addEdge(2, 4, 14); f.addEdge(3, 2, 9);  f.addEdge(3, 5, 20); f.addEdge(4, 3, 7); f.addEdge(4, 5, 4);
        int64_t v = a == MaxFlowAlgo::Dinic ? f.dinic(0, 5) : f.edmondsKarp(0, 5);
        std::vector<char> S = f.sourceSide(0);
        check(std::string(maxFlowName(a)) + " finds flow 23", v == 23);
        check(std::string(maxFlowName(a)) + " min cut S = {s, v1, v2, v4}", S[0] && S[1] && S[2] && !S[3] && S[4] && !S[5]);
    }
}

static void testGraphCut() {
    section("Graph-cut seams are optimal: cost == DP cost (item 4)");
    std::mt19937 rng(10);
    for (MaxFlowAlgo a : {MaxFlowAlgo::EdmondsKarp, MaxFlowAlgo::Dinic}) {
        bool ok = true, flowEq = true; int cases = 0;
        for (int rep = 0; rep < 150; ++rep) {
            int w = 1 + static_cast<int>(rng() % (rep < 120 ? 8 : 25)), h = 1 + static_cast<int>(rng() % (rep < 120 ? 8 : 20));
            EnergyMap e = randomEnergy(w, h, rep % 2 ? 4 : 200, rng);
            GraphCutOptions o; o.algo = a;
            GraphCutStats st;
            Seam g = findVerticalSeamGraphCut(e, o, &st);
            Seam d = findVerticalSeamDP(e);
            ok &= isValidSeam(g.idx, h, w) && g.cost == d.cost && verticalSeamEnergy(e, g.idx) == g.cost;
            flowEq &= st.flow == g.cost;
            ++cases;
        }
        std::ostringstream det; det << cases << " random maps, 1x1 .. 25x20";
        check(std::string(maxFlowName(a)) + ": seam valid and cost == DP", ok, det.str());
        check(std::string(maxFlowName(a)) + ": max-flow value == cut capacity == seam cost", flowEq);
    }

    /* The brief's construction (INF sink edges) can never pick the last column. */
    EnergyMap e(3, 4);
    e.d = {9, 9, 0,  9, 9, 0,  9, 9, 0,  9, 9, 0};
    GraphCutOptions fixedOpt, briefOpt; briefOpt.briefConstruction = true;
    Seam fixedSeam = findVerticalSeamGraphCut(e, fixedOpt), briefSeam = findVerticalSeamGraphCut(e, briefOpt);
    std::ostringstream det; det << "ours " << fixedSeam.cost << ", brief's " << briefSeam.cost << ", DP " << findVerticalSeamDP(e).cost;
    check("cheap seam in the LAST column: our construction finds it (cost 0)", fixedSeam.cost == 0 && fixedSeam.idx[0] == 2, det.str());
    check("...the brief's INF-sink construction cannot (documented deviation)", briefSeam.cost > 0);
}

static void testForwardEnergy() {
    section("Forward-energy DP is optimal for its cost function");
    std::mt19937 rng(11);
    bool ok = true;
    for (int m = 1; m <= 6; ++m)
        for (int n = 1; n <= 5; ++n) {
            Grid<uint8_t> g(n, m);
            for (auto& v : g.d) v = static_cast<uint8_t>(rng() % 256);
            std::vector<int> cur; std::vector<std::vector<int>> seams;
            allSeams(n, m, cur, seams);
            int64_t best = INT64_MAX;
            for (const auto& s : seams) best = std::min(best, forwardSeamCost(g, s));
            Seam f = findVerticalSeamForwardDP(g);
            ok &= f.cost == best && forwardSeamCost(g, f.idx) == f.cost && isValidSeam(f.idx, m, n);
        }
    check("forward DP == brute-force minimum forward cost (all grids up to 6x5)", ok);
}

static void testSeamCounting() {
    section("Number of seams grows exponentially (item 2a(i))");
    bool n2 = true, lower = true, upper = true;
    for (int m = 1; m <= 40; ++m) {
        n2 &= countSeams(2, m) == std::pow(2.0L, m);
        for (int n : {3, 5, 10, 100}) {
            lower &= countSeams(n, m) >= std::pow(2.0L, m);
            upper &= countSeams(n, m) <= n * std::pow(3.0L, m - 1);
        }
    }
    check("n = 2: exactly 2^m seams (every row picks either column)", n2);
    check("n >= 2: at least 2^m seams, m = 1..40", lower);
    check("upper bound n * 3^(m-1) holds", upper);
}

static void testRemovalAndCarver() {
    section("Seam removal and the resize loop (item 1c)");
    Image img = makeRandomImage(5, 4, 12);
    std::vector<int> seam = {1, 2, 2, 3};
    Image r = removeVerticalSeam(img, seam);
    bool kept = r.w == 4 && r.h == 4;
    for (int y = 0; y < 4 && kept; ++y)
        for (int x = 0, ox = 0; x < 5; ++x) {
            if (x == seam[static_cast<size_t>(y)]) continue;
            kept &= std::memcmp(r.ptr(ox++, y), img.ptr(x, y), 3) == 0;
        }
    check("removal keeps every other pixel, in order", kept);
    check("removing from a 1-pixel-wide image throws", throws([] { removeVerticalSeam(Image(1, 3), {0, 0, 0}); }));

    Image scene = makeTowerScene(96, 64);
    struct Case { int w, h; };
    for (Algorithm a : {Algorithm::DP, Algorithm::Greedy, Algorithm::GraphCut})
        for (Case c : std::vector<Case>{{70, 64}, {96, 50}, {60, 40}, {130, 64}, {96, 90}, {80, 80}, {96, 64}}) {
            CarveOptions o; o.algo = a;
            if (a == Algorithm::GraphCut && (c.w < 70 || c.h < 50)) continue;   /* keep the suite fast */
            Image out = carveTo(scene, c.w, c.h, o);
            std::ostringstream lbl; lbl << algorithmName(a) << ": 96x64 -> " << c.w << "x" << c.h;
            check(lbl.str(), out.w == c.w && out.h == c.h);
        }
    for (Order ord : {Order::WidthFirst, Order::HeightFirst, Order::Alternate}) {
        CarveOptions o; o.order = ord;
        Image out = carveTo(scene, 70, 45, o);
        check(std::string("order ") + (ord == Order::WidthFirst ? "width-first" : ord == Order::HeightFirst ? "height-first" : "alternate")
              + " reaches 70x45", out.w == 70 && out.h == 45);
    }
    CarveOptions fw; fw.forwardEnergy = true;
    Image f = carveTo(scene, 70, 50, fw);
    check("forward energy carve reaches 70x50", f.w == 70 && f.h == 50);
    CarveOptions full; full.incrementalEnergy = false;
    CarveOptions inc;
    check("incremental and full energy give identical carved images",
          carveTo(scene, 70, 50, full) == carveTo(scene, 70, 50, inc));
    CarveOptions th; th.threads = 4;
    check("4-thread carve == 1-thread carve", carveTo(scene, 70, 50, th) == carveTo(scene, 70, 50, inc));
    check("deterministic: same input, same output", carveTo(scene, 77, 51, inc) == carveTo(scene, 77, 51, inc));

    /* horizontal carving equals vertical carving of the transposed image */
    Image h1 = carveTo(scene, 96, 50, inc), h2 = transpose(carveTo(transpose(scene), 50, 96, inc));
    check("horizontal seams == vertical seams of the transpose", h1 == h2);
}

static void testHistoryAndMaps() {
    section("Seam history, overlays and the multi-size map");
    Image scene = makeTowerScene(80, 50);
    CarveOptions o;
    std::vector<SeamRecord> seams;
    carveTo(scene, 60, 40, o, nullptr, nullptr, &seams);
    std::set<int> seen; bool unique = true, lengths = true;
    for (const SeamRecord& s : seams) {
        for (int L : s.pixels) unique &= seen.insert(L).second;
        lengths &= static_cast<int>(s.pixels.size()) == (s.vertical ? 50 : 60);
    }
    check("30 seams recorded (20 vertical + 10 horizontal)", seams.size() == 30);
    check("no original pixel is removed twice", unique);
    check("vertical seams have 50 pixels, horizontal ones 60 (width after vertical pass)", lengths);
    check("removed + remaining pixels == all original pixels", static_cast<int>(seen.size()) + 60 * 40 == 80 * 50);

    RemovalMap map = buildVerticalRemovalMap(scene, 40, o);
    bool matches = true;
    for (int k : {0, 1, 7, 25, 40}) matches &= renderWidth(scene, map, 80 - k) == carveTo(scene, 80 - k, 50, o);
    check("multi-size map == sequential carving for k = 0, 1, 7, 25, 40", matches);
    bool enlarge = true;
    for (int k : {1, 10, 40}) enlarge &= renderWidth(scene, map, 80 + k) == enlargeWidth(scene, k, o);
    check("multi-size map enlargement == seam insertion for k = 1, 10, 40", enlarge);
    check("rendering outside the precomputed range throws", throws([&] { renderWidth(scene, map, 39); }));

    /* content awareness on the tower scene: seams should avoid the detailed towers */
    Image big = makeTowerScene(240, 160);
    std::vector<SeamRecord> rec;
    carveTo(big, 168, 160, o, nullptr, nullptr, &rec);
    long removed = 0, inTowers = 0;
    for (const SeamRecord& s : rec)
        for (int L : s.pixels) { int x = L % 240; ++removed; inTowers += (x >= int(0.56 * 240) && x < int(0.84 * 240)); }
    double frac = double(inTowers) / removed, area = 0.28;
    std::ostringstream det; det << 100.0 * frac << "% of removed pixels, towers are " << 100 * area << "% of the width";
    check("DP seams avoid the towers (hit them at < half the rate of uniform removal)", frac < area / 2, det.str());
}

static void testEdgeCases() {
    section("Edge cases");
    CarveOptions o;
    check("1x1 -> 1x1 is a no-op", carveTo(Image(1, 1), 1, 1, o).w == 1);
    Image row = makeRandomImage(9, 1, 13);
    Image r = carveTo(row, 4, 1, o);
    check("single-row image: each seam is one pixel, 9 -> 4", r.w == 4 && r.h == 1);
    Image col = makeRandomImage(1, 9, 14);
    check("single-column image: 1x9 -> 1x3 with horizontal seams", carveTo(col, 1, 3, o).h == 3);
    check("1-wide image enlarged to 5 wide", carveTo(col, 5, 9, o).w == 5);
    Image sm = makeRandomImage(10, 8, 15);
    check("carve down to 1x1", carveTo(sm, 1, 1, o).w == 1);
    check("enlarge 10x8 -> 35x20 (several insertion passes)", [&] { Image e = carveTo(sm, 35, 20, o); return e.w == 35 && e.h == 20; }());
    check("target 0 is rejected", throws([&] { carveTo(sm, 0, 5, o); }));
    CarveOptions bad; bad.forwardEnergy = true; bad.algo = Algorithm::Greedy;
    check("forward energy with greedy is rejected", throws([&] { SeamCarver c(sm, bad); }));
    check("graph cut on a 1-column map picks column 0", findVerticalSeamGraphCut(EnergyMap(1, 5, 3)).cost == 15);
    check("empty image is rejected", throws([&] { SeamCarver c(Image(), o); }));
}

int main() {
    std::cout << "Seam carving - correctness suite\n";
    testImageIO();
    testEnergy();
    testIncrementalEnergy();
    testConvolution();
    testDPOptimal();
    testDPVariants();
    testGreedy();
    testMaxFlow();
    testGraphCut();
    testForwardEnergy();
    testSeamCounting();
    testRemovalAndCarver();
    testHistoryAndMaps();
    testEdgeCases();
    std::cout << "\n== SUMMARY ==\n    " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
