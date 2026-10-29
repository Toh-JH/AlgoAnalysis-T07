"""
Charts for the Assignment 2 report, drawn from the benchmark CSV files.

    python tools/make_charts.py [results/benchmark] [results/charts]

Needs matplotlib.  Colours follow one fixed mapping per algorithm across every chart
(DP blue, greedy orange, graph cut Dinic aqua, graph cut Edmonds-Karp violet).
"""
import csv
import os
import sys
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

SRC = sys.argv[1] if len(sys.argv) > 1 else "results/benchmark"
OUT = sys.argv[2] if len(sys.argv) > 2 else "results/charts"
os.makedirs(OUT, exist_ok=True)

BLUE, ORANGE, AQUA, YELLOW, MAGENTA, GREEN, VIOLET = (
    "#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6")
INK, INK2, GRID, SURFACE = "#0b0b0b", "#52514e", "#e4e3df", "#fcfcfb"

plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
    "axes.edgecolor": GRID, "axes.labelcolor": INK2, "xtick.color": INK2, "ytick.color": INK2,
    "text.color": INK, "font.size": 10, "axes.titlesize": 12, "axes.titleweight": "bold",
    "axes.titlelocation": "left", "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.8,
    "axes.spines.top": False, "axes.spines.right": False, "lines.linewidth": 2,
    "lines.markersize": 6, "legend.frameon": False,
})


def read(name):
    with open(os.path.join(SRC, name), newline="") as f:
        return list(csv.DictReader(f))


def end_label(ax, x, y, text, color, dy=0):
    """Direct label at the end of a line, in text ink with a colour swatch dot."""
    ax.annotate(text, (x, y), xytext=(6, dy), textcoords="offset points", va="center",
                color=INK2, fontsize=9)
    ax.plot([x], [y], "o", color=color, markersize=7, markeredgecolor=SURFACE, markeredgewidth=2)


def save(fig, name):
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, name), dpi=160)
    plt.close(fig)
    print("  wrote", os.path.join(OUT, name))


# 1. seam search time vs pixels (log-log) --------------------------------------------
rows = read("b1_seam_time.csv")
series = [("dp_compact", "DP", BLUE), ("greedy", "Greedy", ORANGE),
          ("graphcut_dinic", "Graph cut (Dinic)", AQUA), ("graphcut_ek", "Graph cut (Edmonds-Karp)", VIOLET)]
fig, ax = plt.subplots(figsize=(7.2, 4.4))
for key, label, color in series:
    pts = [(int(r["pixels"]), float(r["ms"])) for r in rows if r["algorithm"] == key and float(r["ms"]) > 0]
    if not pts:
        continue
    xs, ys = zip(*pts)
    ax.plot(xs, ys, "-o", color=color, label=label, markeredgecolor=SURFACE, markeredgewidth=1.5)
    end_label(ax, xs[-1], ys[-1], f"{ys[-1]:,.2f} ms", color, dy=-8 if key == "graphcut_ek" else 0)
ax.set_xscale("log"); ax.set_yscale("log")
ax.set_xlabel("pixels (m x n)"); ax.set_ylabel("time to find one seam (ms)")
ax.set_title("One seam: DP is linear, graph cut is super-linear")
ax.legend(loc="upper right")
ax.set_xlim(right=max(int(r["pixels"]) for r in rows) * 8)
save(fig, "chart_seam_time.png")

# 2. where the time goes (stacked horizontal bars, 960x640) ---------------------------
rows = [r for r in read("b2_breakdown.csv") if r["width"] == "960"] or read("b2_breakdown.csv")
labels, parts = [], {"energy_ms": [], "find_ms": [], "remove_ms": []}
for r in rows:
    labels.append(f'{"DP" if r["algorithm"] == "dp" else "Greedy"}, {"incremental" if r["incremental"] == "1" else "full"} energy')
    for k in parts:
        parts[k].append(float(r[k]))
fig, ax = plt.subplots(figsize=(7.2, 3.6))
left = [0.0] * len(labels)
for key, name, color in [("energy_ms", "energy map", BLUE), ("find_ms", "seam search", ORANGE), ("remove_ms", "seam removal", AQUA)]:
    ax.barh(labels, parts[key], left=left, color=color, label=name, height=0.6, edgecolor=SURFACE, linewidth=2)
    left = [a + b for a, b in zip(left, parts[key])]
for i, total in enumerate(left):
    ax.annotate(f"{total:,.0f} ms", (total, i), xytext=(6, 0), textcoords="offset points", va="center", color=INK2)
ax.invert_yaxis()
ax.set_xlabel("total time to remove 25% of the width (ms)")
ax.set_title(f'Carving {rows[0]["width"]}x{rows[0]["height"]}: incremental energy halves it')
ax.legend(loc="upper center", bbox_to_anchor=(0.4, -0.28), ncol=3)
ax.grid(axis="y", visible=False)
ax.set_xlim(right=max(left) * 1.18)
save(fig, "chart_time_breakdown.png")

# 3. multithreading speed-up (two small multiples, shared scale) ---------------------
rows = read("b5_threads.csv")
ns = sorted({int(r["n"]) for r in rows})
colors = dict(zip(ns, [BLUE, ORANGE, AQUA]))
fig, axes = plt.subplots(1, 2, figsize=(8.4, 3.6), sharey=True)
for ax, key, title in [(axes[0], "energy_speedup", "Energy map (rows independent)"),
                       (axes[1], "dp_speedup", "DP (barrier after every row)")]:
    for n in ns:
        pts = [(int(r["threads"]), float(r[key])) for r in rows if int(r["n"]) == n]
        xs, ys = zip(*pts)
        ax.plot(xs, ys, "-o", color=colors[n], label=f"{n} x {n}", markeredgecolor=SURFACE, markeredgewidth=1.5)
    ax.axhline(1.0, color=INK2, linewidth=1, linestyle="--")
    ax.set_title(title); ax.set_xlabel("threads")
axes[0].set_ylabel("speed-up vs 1 thread")
axes[1].legend(loc="upper left", title="image")
save(fig, "chart_threads.png")

# 4. convolution methods vs kernel size (n = 1024) -----------------------------------
rows = read("b6_convolution.csv")
nmax = max(int(r["n"]) for r in rows)
fig, ax = plt.subplots(figsize=(7.2, 4.2))
for key, label, color in [("direct", "Direct", BLUE), ("separable", "Separable", ORANGE),
                          ("divide_conquer_parallel", "Divide & conquer (parallel)", AQUA), ("fft", "FFT (D&C in frequency)", VIOLET)]:
    pts = [(int(r["k"]), float(r["ms"])) for r in rows if r["method"] == key and int(r["n"]) == nmax]
    xs, ys = zip(*pts)
    ax.plot(xs, ys, "-o", color=color, label=label, markeredgecolor=SURFACE, markeredgewidth=1.5)
    end_label(ax, xs[-1], ys[-1], label, color, dy={"direct": 8, "fft": -8}.get(key, 0))
ax.set_yscale("log")
ax.set_xlabel("kernel size k (k x k)"); ax.set_ylabel("time (ms)")
ax.set_title(f"Convolving {nmax}x{nmax}: FFT only pays off for large k")
ax.axvline(3, color=INK2, linewidth=1, linestyle=":")
ax.annotate("Sobel, k = 3", (3, ax.get_ylim()[0]), xytext=(4, 8), textcoords="offset points", color=INK2, fontsize=9)
ax.set_xlim(right=max(int(r["k"]) for r in rows) * 1.6)
ax.legend(loc="lower right")
save(fig, "chart_convolution.png")

# 5. number of seams vs m -------------------------------------------------------------
rows = read("b7_seam_count.csv")
fig, ax = plt.subplots(figsize=(7.2, 4.2))
ms = [int(r["m"]) for r in rows]
for key, label, color in [("n100", "n = 100", BLUE), ("n10", "n = 10", ORANGE), ("n3", "n = 3", AQUA), ("pow2m", "2^m  (= n = 2)", VIOLET)]:
    ys = [float(r[key]) for r in rows]
    ax.plot(ms, ys, "-o" if key != "pow2m" else "--", color=color, label=label, markeredgecolor=SURFACE, markeredgewidth=1.5)
    end_label(ax, ms[-1], ys[-1], label, color)
ax.set_yscale("log")
ax.set_xlabel("rows m"); ax.set_ylabel("number of vertical seams")
ax.set_title("Seams grow exponentially: always at least 2^m")
ax.set_xlim(right=max(ms) * 1.2)
ax.legend(loc="upper left")
save(fig, "chart_seam_count.png")

# 6. quality: cost relative to DP -----------------------------------------------------
rows = read("b3_quality.csv")
def mean_ratio(num, den):
    return sum(float(r[num]) / max(1.0, float(r[den])) for r in rows) / len(rows)
bars = [("Graph cut, one seam", mean_ratio("graphcut_first", "dp_first")),
        ("Graph cut, 20% carve", mean_ratio("graphcut_total", "dp_total")),
        ("Greedy (best start), one seam", mean_ratio("greedy_best_start_first", "dp_first")),
        ("Greedy, 20% carve", mean_ratio("greedy_total", "dp_total")),
        ("Greedy, one seam", mean_ratio("greedy_first", "dp_first"))]
fig, ax = plt.subplots(figsize=(7.2, 3.4))
names = [b[0] for b in bars]; vals = [b[1] for b in bars]
cols = [AQUA if n.startswith("Graph") else ORANGE for n in names]
ax.barh(names, vals, color=cols, height=0.6, edgecolor=SURFACE, linewidth=2)
for i, v in enumerate(vals):
    ax.annotate(f"{v:.2f}x", (v, i), xytext=(6, 0), textcoords="offset points", va="center", color=INK2)
ax.axvline(1.0, color=INK2, linewidth=1, linestyle="--")
ax.invert_yaxis(); ax.grid(axis="y", visible=False)
ax.set_xlabel(f"seam energy relative to DP (mean of {len(rows)} images, lower is better)")
ax.set_title("Quality vs DP (dashed line = DP, optimal)")
ax.set_xlim(0, max(vals) * 1.2)
save(fig, "chart_quality.png")
