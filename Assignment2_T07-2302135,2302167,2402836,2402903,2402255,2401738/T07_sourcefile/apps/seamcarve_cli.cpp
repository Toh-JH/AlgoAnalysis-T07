/* =====================================================================================
 *  CSD3130 / CSD3131  Algorithm Analysis - Assignment 2 : Seam Carving
 *  File : seamcarve_cli.cpp     Command-line front end + report-figure generator
 *
 *  USAGE
 *    seamcarve IN OUT [options]          resize one image
 *      --width W | --width 75%           target width  (pixels or percent)
 *      --height H | --height 80%         target height
 *      --algo dp|greedy|graphcut         seam finder                       (default dp)
 *      --energy sobel|gradient           energy map                        (default sobel)
 *      --forward                         forward energy (DP only)
 *      --flow dinic|ek                   max-flow algorithm for graphcut   (default dinic)
 *      --order width|height|alternate    seam order when shrinking both    (default width)
 *      --threads N                       OpenMP threads for energy + DP
 *      --no-incremental                  recompute the full energy map every seam
 *      --seams FILE                      save all removed seams drawn on the original
 *      --energy-map FILE                 save the energy map of the input
 *      --frames DIR [--every N]          save a frame (seam in red) every N seams
 *      --protect MASK                    mask image, same size: white = keep (never carved)
 *      --auto                            choose the width/height automatically (whichever
 *                                        of --width/--height is not given): stop when the
 *                                        next seam would cut the mask or its cost jumps
 *      --auto-floor F                    never go below F x the original size (default 0.6)
 *      --cost-curve FILE                 with --auto: write each seam's energy per pixel (CSV)
 *
 *    seamcarve --demo OUTDIR [--input IMG] [--quick]
 *      Generates every figure used in the report, plus demo_summary.txt.
 * ===================================================================================== */
#include "../src/carver.h"
#include "../src/visualize.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace sc;
namespace fs = std::filesystem;

/* PNG through WIC on Windows; BMP elsewhere (the Linux CI runner). */
static std::string EXT() { return hasNativeCodecs() ? ".png" : ".bmp"; }

static int parseSize(const std::string& s, int base) {
    if (!s.empty() && s.back() == '%') return std::max(1, static_cast<int>(base * std::stod(s.substr(0, s.size() - 1)) / 100.0 + 0.5));
    return std::stoi(s);
}

static void usage() {
    std::cout <<
        "seamcarve IN OUT [--width W|P%] [--height H|P%] [--algo dp|greedy|graphcut]\n"
        "                 [--energy sobel|gradient] [--forward] [--flow dinic|ek]\n"
        "                 [--order width|height|alternate] [--threads N] [--no-incremental]\n"
        "                 [--seams FILE] [--energy-map FILE] [--frames DIR [--every N]]\n"
        "                 [--protect MASK] [--auto [--auto-floor F] [--cost-curve FILE]]\n"
        "seamcarve --demo OUTDIR [--input IMG] [--quick]\n";
}

/* ==================================================================================== *
 *  --demo : all report figures                                                         *
 * ==================================================================================== */

struct Row { std::string name; int seams; double ms; int64_t cost; };

static Row runAndSave(const std::string& name, const Image& img, int tw, int th, CarveOptions o,
                      const fs::path& dir, std::vector<SeamRecord>* seamsOut = nullptr) {
    CarveStats st;
    std::vector<SeamRecord> seams;
    Image out = carveTo(img, tw, th, o, nullptr, &st, &seams);
    saveImage(out, (dir / (name + ("" + EXT()))).string());
    saveImage(overlaySeams(img, seams), (dir / (name + ("_seams" + EXT()))).string());
    if (seamsOut) *seamsOut = seams;
    std::cout << "  " << std::left << std::setw(30) << name << std::right << std::setw(5) << st.seams
              << " seams " << std::setw(10) << std::fixed << std::setprecision(1) << st.totalMs << " ms   total energy "
              << st.totalCost << "\n";
    return {name, st.seams, st.totalMs, st.totalCost};
}

static int demo(const fs::path& dir, const std::string& input, bool quick) {
    fs::create_directories(dir);
    Image img = input.empty() ? makeTowerScene(quick ? 240 : 480, quick ? 160 : 320) : loadImage(input);
    std::cout << "Demo on " << (input.empty() ? "synthetic tower scene" : input) << "  (" << img.w << "x" << img.h << ")\n";
    saveImage(img, (dir / ("00_input" + EXT())).string());
    std::vector<Row> rows;

    /* 1. energy maps */
    saveImage(energyToImage(computeEnergy(img, EnergyKind::Sobel)),    (dir / ("01_energy_sobel" + EXT())).string());
    saveImage(energyToImage(computeEnergy(img, EnergyKind::Gradient)), (dir / ("01_energy_gradient" + EXT())).string());

    /* 2. scaling vs cropping vs seam carving (the brief's motivating figure), 55% width */
    const int tw = img.w * 7 / 10, cw = img.w * 55 / 100;
    CarveOptions dp;
    Image carved = carveTo(img, cw, img.h, dp);
    saveImage(hconcat({resizeBilinear(img, cw, img.h), cropCenter(img, cw, img.h), carved}),
              (dir / ("02_scale_crop_carve" + EXT())).string());

    /* 3. DP vs greedy (full size), first seam of each on the same energy map */
    std::cout << "\nRemoving " << img.w - tw << " vertical seams:\n";
    rows.push_back(runAndSave("03_dp", img, tw, img.h, dp, dir));
    CarveOptions gr; gr.algo = Algorithm::Greedy;
    rows.push_back(runAndSave("03_greedy", img, tw, img.h, gr, dir));
    CarveOptions fw; fw.forwardEnergy = true;
    rows.push_back(runAndSave("03_dp_forward", img, tw, img.h, fw, dir));
    CarveOptions grad; grad.energy = EnergyKind::Gradient;
    rows.push_back(runAndSave("03_dp_gradient", img, tw, img.h, grad, dir));

    EnergyMap e = computeEnergy(img, EnergyKind::Sobel);
    Seam sDP = findVerticalSeamDP(e), sGR = findVerticalSeamGreedy(e);
    Image first = highlightSeam(highlightSeam(img, sGR, true, {255, 200, 0}), sDP, true, {255, 0, 0});
    saveImage(first, (dir / ("04_first_seam_dp_red_greedy_yellow" + EXT())).string());

    /* 4. graph cut: same task on a smaller copy (max-flow per seam is far slower) */
    const int gw = quick ? 120 : 160, gh = gw * img.h / img.w;
    Image small = resizeBilinear(img, gw, gh);
    saveImage(small, (dir / ("05_small_input" + EXT())).string());
    std::cout << "\nSmall copy " << gw << "x" << gh << ", removing " << gw - gw * 7 / 10 << " vertical seams:\n";
    rows.push_back(runAndSave("05_small_dp", small, gw * 7 / 10, gh, dp, dir));
    rows.push_back(runAndSave("05_small_greedy", small, gw * 7 / 10, gh, gr, dir));
    CarveOptions gc; gc.algo = Algorithm::GraphCut;
    rows.push_back(runAndSave("05_small_graphcut_dinic", small, gw * 7 / 10, gh, gc, dir));
    if (!quick) {
        CarveOptions ek = gc; ek.flow = MaxFlowAlgo::EdmondsKarp;
        rows.push_back(runAndSave("05_small_graphcut_ek", small, gw * 7 / 10, gh, ek, dir));
    }

    /* 5. both dimensions, enlargement */
    std::cout << "\nOther operations:\n";
    rows.push_back(runAndSave("06_shrink_both_70x80", img, tw, img.h * 8 / 10, dp, dir));
    CarveOptions alt; alt.order = Order::Alternate;
    rows.push_back(runAndSave("06_shrink_both_alternate", img, tw, img.h * 8 / 10, alt, dir));
    rows.push_back(runAndSave("07_enlarge_130pct", img, img.w * 13 / 10, img.h, dp, dir));
    saveImage(resizeBilinear(img, img.w * 13 / 10, img.h), (dir / ("07_enlarge_130pct_scaled" + EXT())).string());

    /* 6. step-by-step frames (seam about to be removed drawn in red) */
    fs::path frames = dir / "frames";
    fs::create_directories(frames);
    int every = std::max(1, (img.w - tw) / 12), saved = 0;
    carveTo(img, tw, img.h, dp, [&](const Image& cur, const Seam& s, bool v, int step, int) {
        if (step % every == 0) {
            std::ostringstream n; n << "frame_" << std::setw(4) << std::setfill('0') << step << ("" + EXT());
            saveImage(highlightSeam(cur, s, v), (frames / n.str()).string());
            ++saved;
        }
        return true;
    });
    std::cout << "  " << saved << " step-by-step frames in " << frames.string() << "\n";

    std::ofstream sum(dir / "demo_summary.txt");
    sum << "name,seams,total_ms,total_seam_energy\n";
    for (const Row& r : rows) sum << r.name << "," << r.seams << "," << r.ms << "," << r.cost << "\n";
    std::cout << "\nFigures written to " << dir.string() << "\n";
    return 0;
}

/* ==================================================================================== *
 *  main                                                                                *
 * ==================================================================================== */

int main(int argc, char** argv) {
    std::vector<std::string> a(argv + 1, argv + argc);
    try {
        if (a.empty() || a[0] == "--help" || a[0] == "-h") { usage(); return a.empty() ? 1 : 0; }
        if (a[0] == "--demo") {
            std::string input; bool quick = false;
            fs::path dir = a.size() > 1 ? a[1] : "results";
            for (size_t i = 2; i < a.size(); ++i) {
                if (a[i] == "--input" && i + 1 < a.size()) input = a[++i];
                else if (a[i] == "--quick") quick = true;
            }
            return demo(dir, input, quick);
        }
        if (a.size() < 2) { usage(); return 1; }

        Image img = loadImage(a[0]);
        std::string out = a[1], wS, hS, seamsFile, energyFile, framesDir, protectFile, curveFile;
        int every = 1;
        bool autoMode = false;
        AutoSizeParams ap;
        CarveOptions o;
        for (size_t i = 2; i < a.size(); ++i) {
            auto next = [&]() -> std::string {
                if (i + 1 >= a.size()) throw std::invalid_argument("missing value after " + a[i]);
                return a[++i];
            };
            if      (a[i] == "--width")          wS = next();
            else if (a[i] == "--height")         hS = next();
            else if (a[i] == "--algo")         { if (!parseAlgorithm(next(), o.algo)) throw std::invalid_argument("unknown --algo"); }
            else if (a[i] == "--energy")         o.energy = next() == "gradient" ? EnergyKind::Gradient : EnergyKind::Sobel;
            else if (a[i] == "--forward")        o.forwardEnergy = true;
            else if (a[i] == "--flow")           o.flow = next() == "ek" ? MaxFlowAlgo::EdmondsKarp : MaxFlowAlgo::Dinic;
            else if (a[i] == "--order")        { std::string v = next(); o.order = v == "height" ? Order::HeightFirst : v == "alternate" ? Order::Alternate : Order::WidthFirst; }
            else if (a[i] == "--threads")        o.threads = std::max(1, std::stoi(next()));
            else if (a[i] == "--no-incremental") o.incrementalEnergy = false;
            else if (a[i] == "--seams")          seamsFile = next();
            else if (a[i] == "--energy-map")     energyFile = next();
            else if (a[i] == "--frames")         framesDir = next();
            else if (a[i] == "--every")          every = std::max(1, std::stoi(next()));
            else if (a[i] == "--protect")        protectFile = next();
            else if (a[i] == "--auto")           autoMode = true;
            else if (a[i] == "--auto-floor")     ap.minFraction = std::clamp(std::stod(next()), 0.0, 1.0);
            else if (a[i] == "--cost-curve")     curveFile = next();
            else throw std::invalid_argument("unknown option " + a[i]);
        }
        ProtectMask mask;
        if (!protectFile.empty()) {
            mask = maskFromImage(loadImage(protectFile));
            if (mask.w != img.w || mask.h != img.h) throw std::invalid_argument("--protect mask must be the same size as the image");
        }
        const ProtectMask* maskPtr = mask.empty() ? nullptr : &mask;

        int tw = wS.empty() ? img.w : parseSize(wS, img.w);
        int th = hS.empty() ? img.h : parseSize(hS, img.h);
        if (autoMode) {
            ap.width = wS.empty(); ap.height = hS.empty();
            AutoSizeResult r = autoSize(img, o, maskPtr, ap);
            if (ap.width) {
                tw = r.width;
                std::cout << "auto width  " << tw << " px (" << 100 * tw / img.w << "%): " << stopReasonText(r.widthReason) << "\n";
            }
            if (ap.height) {
                th = r.height;
                std::cout << "auto height " << th << " px (" << 100 * th / img.h << "%): " << stopReasonText(r.heightReason) << "\n";
            }
            if (!curveFile.empty()) {
                std::ofstream f(curveFile);
                f << "direction,seam,energy_per_pixel\n";
                for (size_t i = 0; i < r.widthCosts.size(); ++i)  f << "width," << i << "," << r.widthCosts[i] << "\n";
                for (size_t i = 0; i < r.heightCosts.size(); ++i) f << "height," << i << "," << r.heightCosts[i] << "\n";
            }
        }
        if (!energyFile.empty()) saveImage(energyToImage(computeEnergy(img, o.energy)), energyFile);
        if (!framesDir.empty()) fs::create_directories(framesDir);

        StepCallback cb;
        if (!framesDir.empty())
            cb = [&](const Image& cur, const Seam& s, bool v, int step, int) {
                if (step % every == 0) {
                    std::ostringstream n; n << "frame_" << std::setw(5) << std::setfill('0') << step << ("" + EXT());
                    saveImage(highlightSeam(cur, s, v), (fs::path(framesDir) / n.str()).string());
                }
                return true;
            };
        CarveStats st;
        std::vector<SeamRecord> seams;
        Image res = carveTo(img, tw, th, o, cb, &st, &seams, maskPtr);
        saveImage(res, out);
        if (!seamsFile.empty()) saveImage(overlaySeams(img, seams), seamsFile);
        std::cout << img.w << "x" << img.h << " -> " << res.w << "x" << res.h << "  [" << algorithmName(o.algo)
                  << (o.forwardEnergy ? ", forward" : "") << ", " << energyName(o.energy) << "]  "
                  << st.seams << " seams, " << std::fixed << std::setprecision(1) << st.totalMs << " ms"
                  << "  (energy " << st.energyMs << ", find " << st.findMs << ", remove " << st.removeMs
                  << ")  total seam energy " << st.totalCost << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 2;
    }
}
