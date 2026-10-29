# Seam Carving: CSD3130 / CSD3131 Assignment 2 (Team T07)

Content-aware image resizing (Avidan & Shamir, 2007) in C++17, implemented three ways:

| Algorithm | Seam found by | Per seam | Optimal? |
|---|---|---|---|
| **Dynamic programming** | `M[i][j] = e[i][j] + min(M[i-1][j-1], M[i-1][j], M[i-1][j+1])` | Θ(mn) | yes |
| **Greedy** | cheapest top pixel, then cheapest of the 3 below, row by row | Θ(m + n) | no (≈1.6× DP energy) |
| **Graph cut** | minimum s–t cut via max-flow (Edmonds–Karp or Dinic) | super-linear (~P^1.6) | yes (cost = DP, verified) |

Image libraries are used only to read/write pixels. Energy maps, convolution, every seam
finder, removal, insertion and visualisation are our own code.

## Build

**Windows (Visual Studio 2022):** run `build.bat` (finds MSVC by itself).

```bat
build.bat          :: builds bin\seamcarve_gui.exe, seamcarve.exe, seamcarve_tests.exe, seamcarve_bench.exe
build.bat test     :: + runs the test suite  -> results\test_output.txt
build.bat demo     :: + regenerates every report figure -> results\
build.bat bench    :: + runs the full benchmark -> results\benchmark\*.csv
```

**Linux / macOS / CI (CMake):**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On Windows, PNG/JPEG/BMP/GIF/TIFF are read through WIC, which is built into Windows.
Elsewhere BMP and PPM are supported (hand-written codecs).

## Run

```bat
bin\seamcarve_gui.exe [photo.jpg]
bin\seamcarve.exe in.jpg out.png --width 70% --algo dp --seams seams.png
bin\seamcarve.exe in.jpg out.png --width 120% --height 80% --forward
bin\seamcarve.exe in.jpg out.png --width 300 --algo graphcut --flow dinic
bin\seamcarve.exe in.jpg out.png --width 70% --frames frames --every 5
bin\seamcarve.exe --demo results\figures [--input photo.jpg]
```

**GUI** (bonus 7a, 7b): open an image, choose the algorithm and energy, drag the
width/height sliders, press **Carve**.

| Control | What it does |
|---|---|
| **Animate seams** | Draws every seam in red before it is removed. |
| **−1 vertical / horizontal seam** | Single-steps the algorithm. |
| **Removed seams** view | Shows all removed seams on the original. |
| **Energy map** view | Shows the energy map. |
| **Live width** | Precomputes a multi-size map once; after that the width slider is real-time. |

## Layout

```
src/image.*          RGB image, Grid<T>, BMP/PPM/WIC I/O, bilinear scale & crop baselines
src/energy.*         gradient & Sobel energy, O(m) incremental update after a seam removal
src/convolution.*    direct / separable / divide-and-conquer / FFT convolution (item 2b)
src/seam.*           Seam type, validation, removal
src/seam_dp.*        DP: full table, compact (rolling rows + int8 parents), OpenMP row-parallel,
                     native horizontal, forward energy, seam counting
src/seam_greedy.*    greedy and best-start greedy
src/seam_graphcut.*  MaxFlow (Edmonds-Karp, Dinic) + seam-as-min-cut graph
src/carver.*         SeamCarver (remove & repeat, both directions), seam insertion, multi-size map
src/visualize.*      seam overlays, step frames, synthetic test scenes
apps/                CLI (+ figure generator), Win32 GUI, benchmark
tests/tests.cpp      101 correctness tests
tools/make_charts.py charts for the report from the benchmark CSVs
results/             figures, charts, benchmark CSVs, test transcript
```

## Correctness evidence (`tests/tests.cpp`, 101 tests)

- **DP vs brute force:** DP equals exhaustive enumeration of every seam on all grids up to 7×6 (41,588 seams).
- **Graph cut vs DP:** Edmonds–Karp and Dinic give exactly the DP seam cost on 300 random maps, and the max-flow value equals the cut capacity.
- **DP variants agree:** full table, compact, 4-thread parallel and native-horizontal DP return identical seams.
- **Incremental energy:** the incremental update equals a full recompute after 30 removals, including border seams.
- **Multi-size map:** reproduces sequential carving and seam insertion pixel-for-pixel.
- **Seam counting:** the counting DP matches enumeration, and N(m, n) ≥ 2^m for n ≥ 2 (item 2a(i)).
- **Graph-construction fix:** the brief's construction with an infinite-capacity sink edge on the last column cannot remove a last-column pixel. We use a finite sink edge `e[i][n-1]` instead, and a test demonstrates the difference.
- **Edge cases:** 1×1, single row/column, carve to 1 pixel, more than 2× enlargement, invalid input.

## CI/CD (bonus 7c)

`.github/workflows/seam-carving.yml` runs on every push that touches this folder:

1. Builds with MSVC (Windows, including the GUI) and GCC (Ubuntu).
2. Runs `ctest` (test suite and a CLI smoke test), the quick benchmark and the demo.
3. Uploads binaries, figures and CSVs as artifacts.

Pushing a tag such as `a2-v1.0` also publishes a zipped Windows build as a GitHub Release.
