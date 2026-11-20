/* =====================================================================================
 *  CSD3130 / CSD3131  Algorithm Analysis - Assignment 2 : Seam Carving
 *  File : benchmark.cpp    Empirical analysis behind item 5 of the report
 *
 *    B1  seam-finding time vs image size  (DP full / compact, greedy, graph cut)
 *    B2  where the time goes when carving (energy / find / remove; incremental energy)
 *    B3  quality: greedy vs DP vs graph cut seam energies over many images
 *    B4  DP table memory, full vs compact
 *    B5  multithreading: energy map and row-parallel DP, 1..P threads
 *    B6  convolution: direct / separable / divide-and-conquer / FFT  (item 2b)
 *    B7  number of seams vs m  (item 2a(i))
 *    B8  max-flow internals: Edmonds-Karp vs Dinic
 *
 *  RUN   seamcarve_bench [OUTDIR] [--quick]      CSV files are written to OUTDIR
 * ===================================================================================== */
#include "../src/carver.h"
#include "../src/convolution.h"
#include "../src/visualize.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>

#ifdef _OPENMP
#include <omp.h>
#endif

using namespace sc;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

static bool g_quick = false;

/* Median wall time (ms) of fn, repeated until ~minTotalMs has elapsed (at least once). */
static double timeMs(const std::function<void()>& fn, double minTotalMs = 150, int maxReps = 25) {
    std::vector<double> t;
    double total = 0;
    do {
        auto t0 = Clock::now();
        fn();
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        t.push_back(ms); total += ms;
    } while (total < minTotalMs && static_cast<int>(t.size()) < maxReps);
    std::sort(t.begin(), t.end());
    return t[t.size() / 2];
}

static std::ofstream openCsv(const fs::path& dir, const std::string& name, const std::string& header) {
    std::ofstream f(dir / name);
    f << header << "\n";
    return f;
}

static void title(const std::string& s) { std::cout << "\n=== " << s << " ===\n"; }

/* ------------------------------------------------------------------------------------ */
static void b1_scaling(const fs::path& dir) {
    title("B1  Single seam search time vs image size (n x n tower scene)");
    auto csv = openCsv(dir, "b1_seam_time.csv", "n,pixels,algorithm,ms,ns_per_pixel");
    std::vector<int> sizes = g_quick ? std::vector<int>{64, 128, 256} : std::vector<int>{64, 128, 256, 512, 1024, 2048, 4096};
    std::cout << std::setw(6) << "n" << std::setw(12) << "DP full" << std::setw(12) << "DP compact"
              << std::setw(12) << "greedy" << std::setw(14) << "GC Dinic" << std::setw(14) << "GC E-K" << "   (ms)\n";
    for (int n : sizes) {
        Image img = makeTowerScene(n, n);
        EnergyMap e = computeEnergy(img, EnergyKind::Sobel);
        double px = double(n) * n;
        auto rec = [&](const char* name, double ms) {
            csv << n << "," << static_cast<long long>(px) << "," << name << "," << ms << "," << ms * 1e6 / px << "\n";
            return ms;
        };
        double dpf = rec("dp_full",    timeMs([&] { findVerticalSeamDP(e); }));
        double dpc = rec("dp_compact", timeMs([&] { findVerticalSeamDPCompact(e); }));
        double gr  = rec("greedy",     timeMs([&] { findVerticalSeamGreedy(e); }));
        double gcd = -1, gce = -1;
        if (n <= (g_quick ? 128 : 512)) {
            GraphCutOptions o; o.algo = MaxFlowAlgo::Dinic;
            gcd = rec("graphcut_dinic", timeMs([&] { findVerticalSeamGraphCut(e, o); }, 0, 3));
        }
        if (n <= (g_quick ? 64 : 256)) {
            GraphCutOptions o; o.algo = MaxFlowAlgo::EdmondsKarp;
            gce = rec("graphcut_ek", timeMs([&] { findVerticalSeamGraphCut(e, o); }, 0, 1));
        }
        std::cout << std::fixed << std::setprecision(3) << std::setw(6) << n << std::setw(12) << dpf << std::setw(12) << dpc
                  << std::setw(12) << gr << std::setw(14);
        if (gcd >= 0) std::cout << gcd; else std::cout << "-";
        std::cout << std::setw(14);
        if (gce >= 0) std::cout << gce; else std::cout << "-";
        std::cout << "\n";
    }
}

/* ------------------------------------------------------------------------------------ */
static void b2_breakdown(const fs::path& dir) {
    title("B2  Carving 25% of the width: where the time goes");
    auto csv = openCsv(dir, "b2_breakdown.csv", "width,height,algorithm,incremental,seams,energy_ms,find_ms,remove_ms,total_ms,total_seam_energy");
    std::vector<std::pair<int, int>> sizes = g_quick ? std::vector<std::pair<int, int>>{{240, 160}}
                                                     : std::vector<std::pair<int, int>>{{480, 320}, {960, 640}, {1920, 1280}};
    std::cout << std::setw(10) << "size" << std::setw(12) << "algo" << std::setw(6) << "inc" << std::setw(11) << "energy"
              << std::setw(11) << "find" << std::setw(11) << "remove" << std::setw(11) << "total" << "  ms\n";
    for (auto [w, h] : sizes) {
        Image img = makeTowerScene(w, h);
        for (Algorithm a : {Algorithm::DP, Algorithm::Greedy})
            for (bool inc : {false, true}) {
                CarveOptions o; o.algo = a; o.incrementalEnergy = inc;
                CarveStats st;
                carveTo(img, w * 3 / 4, h, o, nullptr, &st);
                csv << w << "," << h << "," << algorithmName(a) << "," << inc << "," << st.seams << "," << st.energyMs << ","
                    << st.findMs << "," << st.removeMs << "," << st.totalMs << "," << st.totalCost << "\n";
                std::cout << std::fixed << std::setprecision(1) << std::setw(10) << (std::to_string(w) + "x" + std::to_string(h))
                          << std::setw(12) << algorithmName(a) << std::setw(6) << (inc ? "yes" : "no") << std::setw(11) << st.energyMs
                          << std::setw(11) << st.findMs << std::setw(11) << st.removeMs << std::setw(11) << st.totalMs << "\n";
            }
    }
}

/* ------------------------------------------------------------------------------------ */
static void b3_quality(const fs::path& dir) {
    title("B3  Seam quality: energy of the chosen seam (lower is better)");
    auto csv = openCsv(dir, "b3_quality.csv", "image,seed,width,height,dp_first,greedy_first,greedy_best_start_first,graphcut_first,dp_total,greedy_total,graphcut_total");
    std::mt19937 rng(2024);
    int trials = g_quick ? 4 : 20;
    double sumRatioFirst = 0, sumRatioBest = 0, sumRatioTotal = 0, sumGcTotal = 0;
    int greedyOptimal = 0;
    for (int t = 0; t < trials; ++t) {
        bool scene = t % 2 == 0;
        uint32_t seed = rng();
        int w = 96 + static_cast<int>(rng() % 64), h = 64 + static_cast<int>(rng() % 48);
        Image img = scene ? makeTowerScene(w, h, seed) : resizeBilinear(makeRandomImage(w / 8, h / 8, seed), w, h);
        EnergyMap e = computeEnergy(img, EnergyKind::Sobel);
        int64_t dpF = findVerticalSeamDP(e).cost, grF = findVerticalSeamGreedy(e).cost,
                gbF = findVerticalSeamGreedyBestStart(e).cost, gcF = findVerticalSeamGraphCut(e).cost;
        CarveStats sd, sg, sc2;
        CarveOptions od, og, oc; og.algo = Algorithm::Greedy; oc.algo = Algorithm::GraphCut;
        int k = w / 5;
        carveTo(img, w - k, h, od, nullptr, &sd);
        carveTo(img, w - k, h, og, nullptr, &sg);
        carveTo(img, w - k, h, oc, nullptr, &sc2);
        csv << (scene ? "tower" : "smooth_random") << "," << seed << "," << w << "," << h << "," << dpF << "," << grF << ","
            << gbF << "," << gcF << "," << sd.totalCost << "," << sg.totalCost << "," << sc2.totalCost << "\n";
        sumRatioFirst += double(grF) / std::max<int64_t>(1, dpF);
        sumRatioBest  += double(gbF) / std::max<int64_t>(1, dpF);
        sumRatioTotal += double(sg.totalCost) / std::max<int64_t>(1, sd.totalCost);
        sumGcTotal    += double(sc2.totalCost) / std::max<int64_t>(1, sd.totalCost);
        greedyOptimal += grF == dpF;
    }
    std::cout << std::fixed << std::setprecision(3)
              << "  over " << trials << " images (half tower scenes, half smooth random):\n"
              << "    first seam   greedy / DP            = " << sumRatioFirst / trials << "   (greedy optimal in " << greedyOptimal << "/" << trials << ")\n"
              << "    first seam   best-start greedy / DP = " << sumRatioBest / trials << "\n"
              << "    first seam   graph cut / DP         = 1.000 (asserted equal in the test suite)\n"
              << "    20% carve    greedy total / DP      = " << sumRatioTotal / trials << "\n"
              << "    20% carve    graph cut total / DP   = " << sumGcTotal / trials << "   (ties broken differently)\n";
}

/* ------------------------------------------------------------------------------------ */
static void b4_memory(const fs::path& dir) {
    title("B4  DP table memory");
    auto csv = openCsv(dir, "b4_memory.csv", "n,full_bytes,compact_bytes,ratio");
    for (int n : {256, 512, 1024, 2048, 4096}) {
        EnergyMap e(n, n, 1);
        DPStats f, c;
        findVerticalSeamDP(e, &f); findVerticalSeamDPCompact(e, &c);
        csv << n << "," << f.tableBytes << "," << c.tableBytes << "," << double(f.tableBytes) / c.tableBytes << "\n";
        std::cout << "  " << std::setw(5) << n << "x" << std::left << std::setw(6) << n << std::right
                  << std::setw(10) << std::fixed << std::setprecision(2) << f.tableBytes / 1048576.0 << " MiB full   "
                  << std::setw(8) << c.tableBytes / 1048576.0 << " MiB compact   (" << double(f.tableBytes) / c.tableBytes << "x)\n";
        if (g_quick && n >= 1024) break;
    }
}

/* ------------------------------------------------------------------------------------ */
static void b5_threads(const fs::path& dir) {
    title("B5  Multithreading (OpenMP)");
    auto csv = openCsv(dir, "b5_threads.csv", "n,threads,energy_ms,dp_ms,energy_speedup,dp_speedup");
    int hw = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    std::vector<int> ts = {1, 2, 4, 8, 12, 16};
    ts.erase(std::remove_if(ts.begin(), ts.end(), [&](int t) { return t > hw; }), ts.end());
    std::cout << "  hardware threads: " << hw << "\n";
    std::vector<int> sizes = g_quick ? std::vector<int>{512} : std::vector<int>{256, 1024, 4096};
    for (int n : sizes) {
        Image img = makeTowerScene(n, n);
        EnergyMap e = computeEnergy(img, EnergyKind::Sobel);
        double e1 = 0, d1 = 0;
        for (int t : ts) {
            double em = timeMs([&] { computeEnergy(img, EnergyKind::Sobel, t); });
            double dm = timeMs([&] { if (t == 1) findVerticalSeamDPCompact(e); else findVerticalSeamDPParallel(e, t); });
            if (t == 1) { e1 = em; d1 = dm; }
            csv << n << "," << t << "," << em << "," << dm << "," << e1 / em << "," << d1 / dm << "\n";
            std::cout << "  n=" << std::setw(5) << n << "  threads " << std::setw(2) << t << std::fixed << std::setprecision(3)
                      << "   energy " << std::setw(9) << em << " ms (x" << std::setprecision(2) << e1 / em << ")"
                      << std::setprecision(3) << "   DP " << std::setw(9) << dm << " ms (x" << std::setprecision(2) << d1 / dm << ")\n";
        }
    }
}

/* ------------------------------------------------------------------------------------ */
static void b6_convolution(const fs::path& dir) {
    title("B6  Convolution: direct vs separable vs divide&conquer vs FFT  (item 2b)");
    auto csv = openCsv(dir, "b6_convolution.csv", "n,k,method,ms");
    std::vector<int> sizes = g_quick ? std::vector<int>{256} : std::vector<int>{256, 512, 1024};
    std::vector<int> ks = g_quick ? std::vector<int>{3, 9} : std::vector<int>{3, 5, 9, 15, 25, 41};
    std::cout << std::setw(6) << "n" << std::setw(5) << "k" << std::setw(11) << "direct" << std::setw(11) << "separable"
              << std::setw(11) << "D&C" << std::setw(11) << "D&C par" << std::setw(11) << "FFT" << "   (ms)\n";
    for (int n : sizes) {
        GrayF img = toGrayF(makeTowerScene(n, n));
        for (int k : ks) {
            std::vector<double> g(static_cast<size_t>(k));
            for (int i = 0; i < k; ++i) g[static_cast<size_t>(i)] = std::exp(-0.5 * std::pow((i - k / 2) / (k / 4.0 + 0.5), 2));
            GrayF K(k, k);
            for (int y = 0; y < k; ++y) for (int x = 0; x < k; ++x) K.at(x, y) = g[static_cast<size_t>(y)] * g[static_cast<size_t>(x)];
            double tD = timeMs([&] { convolveDirect(img, K); }, 100, 5);
            double tS = timeMs([&] { convolveSeparable(img, g, g); }, 100, 5);
            double tC = timeMs([&] { convolveDivideConquer(img, K, 64, 0); }, 100, 5);
            double tP = timeMs([&] { convolveDivideConquer(img, K, 64, 2); }, 100, 5);
            double tF = timeMs([&] { convolveFFT(img, K); }, 100, 5);
            for (auto [name, ms] : {std::pair<const char*, double>{"direct", tD}, {"separable", tS}, {"divide_conquer", tC},
                                    {"divide_conquer_parallel", tP}, {"fft", tF}})
                csv << n << "," << k << "," << name << "," << ms << "\n";
            std::cout << std::fixed << std::setprecision(2) << std::setw(6) << n << std::setw(5) << k << std::setw(11) << tD
                      << std::setw(11) << tS << std::setw(11) << tC << std::setw(11) << tP << std::setw(11) << tF << "\n";
        }
    }
}

/* ------------------------------------------------------------------------------------ */
static void b7_counting(const fs::path& dir) {
    title("B7  Number of vertical seams N(m, n)  vs  2^m  (item 2a(i))");
    auto csv = openCsv(dir, "b7_seam_count.csv", "m,n2,n3,n10,n100,pow2m,n_pow3_m1_for_n100");
    std::cout << std::setw(4) << "m" << std::setw(14) << "n=2" << std::setw(14) << "n=3" << std::setw(14) << "n=10"
              << std::setw(14) << "n=100" << std::setw(14) << "2^m" << "\n";
    for (int m : {1, 2, 3, 4, 5, 8, 10, 15, 20, 30, 40, 50}) {
        long double c2 = countSeams(2, m), c3 = countSeams(3, m), c10 = countSeams(10, m), c100 = countSeams(100, m);
        long double p2 = std::pow(2.0L, m);
        csv << m << "," << static_cast<double>(c2) << "," << static_cast<double>(c3) << "," << static_cast<double>(c10) << ","
            << static_cast<double>(c100) << "," << static_cast<double>(p2) << "," << static_cast<double>(100 * std::pow(3.0L, m - 1)) << "\n";
        std::cout << std::setw(4) << m << std::scientific << std::setprecision(4) << std::setw(14) << static_cast<double>(c2)
                  << std::setw(14) << static_cast<double>(c3) << std::setw(14) << static_cast<double>(c10)
                  << std::setw(14) << static_cast<double>(c100) << std::setw(14) << static_cast<double>(p2) << "\n";
    }
    std::cout << std::defaultfloat;
}

/* ------------------------------------------------------------------------------------ */
static void b8_maxflow(const fs::path& dir) {
    title("B8  Max-flow internals for one graph-cut seam");
    auto csv = openCsv(dir, "b8_maxflow.csv", "n,algorithm,nodes,arcs,augmentations,phases,build_ms,flow_ms,cut_ms,flow,dp_cost");
    std::vector<int> sizes = g_quick ? std::vector<int>{32, 64} : std::vector<int>{32, 64, 128, 256};
    std::cout << std::setw(5) << "n" << std::setw(14) << "algorithm" << std::setw(10) << "nodes" << std::setw(10) << "arcs"
              << std::setw(10) << "augment" << std::setw(8) << "phases" << std::setw(11) << "flow ms" << "   flow == DP?\n";
    for (int n : sizes)
        for (MaxFlowAlgo a : {MaxFlowAlgo::EdmondsKarp, MaxFlowAlgo::Dinic}) {
            if (a == MaxFlowAlgo::EdmondsKarp && n > (g_quick ? 64 : 256)) continue;
            EnergyMap e = computeEnergy(makeTowerScene(n, n), EnergyKind::Sobel);
            GraphCutOptions o; o.algo = a;
            GraphCutStats st;
            findVerticalSeamGraphCut(e, o, &st);
            int64_t dp = findVerticalSeamDP(e).cost;
            csv << n << "," << maxFlowName(a) << "," << st.nodes << "," << st.arcs << "," << st.augmentations << "," << st.phases
                << "," << st.buildMs << "," << st.flowMs << "," << st.cutMs << "," << st.flow << "," << dp << "\n";
            std::cout << std::setw(5) << n << std::setw(14) << maxFlowName(a) << std::setw(10) << st.nodes << std::setw(10) << st.arcs
                      << std::setw(10) << st.augmentations << std::setw(8) << st.phases << std::fixed << std::setprecision(2)
                      << std::setw(11) << st.flowMs << "   " << (st.flow == dp ? "yes" : "NO") << "\n";
        }
}

int main(int argc, char** argv) {
    fs::path dir = "results";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--quick") g_quick = true; else dir = a;
    }
    fs::create_directories(dir);
    std::cout << "Seam carving benchmark" << (g_quick ? " (quick)" : "") << "  ->  " << dir.string() << "\n";
    b7_counting(dir);
    b4_memory(dir);
    b1_scaling(dir);
    b8_maxflow(dir);
    b2_breakdown(dir);
    b3_quality(dir);
    b5_threads(dir);
    b6_convolution(dir);
    std::cout << "\nDone. CSV files in " << dir.string() << "\n";
    return 0;
}
