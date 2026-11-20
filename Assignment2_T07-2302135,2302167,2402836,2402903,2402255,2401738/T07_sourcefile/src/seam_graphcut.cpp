/* =====================================================================================
 *  File : seam_graphcut.cpp   Max-flow (Edmonds-Karp, Dinic) and graph-cut seams
 * ===================================================================================== */
#include "seam_graphcut.h"

#include <algorithm>
#include <chrono>
#include <limits>

namespace sc {

/* ==================================================================================== *
 *  MAX-FLOW                                                                            *
 * ==================================================================================== */

MaxFlow::MaxFlow(int nodes) : n_(nodes), head_(static_cast<size_t>(nodes), -1) {}

void MaxFlow::addEdge(int u, int v, int64_t cap, int64_t revCap) {
    int a = static_cast<int>(to_.size());
    to_.push_back(v); cap_.push_back(cap);    next_.push_back(head_[static_cast<size_t>(u)]); head_[static_cast<size_t>(u)] = a;
    to_.push_back(u); cap_.push_back(revCap); next_.push_back(head_[static_cast<size_t>(v)]); head_[static_cast<size_t>(v)] = a + 1;
}

/* ------------------------------------------------------------------------------------ *
 *  Edmonds-Karp: Ford-Fulkerson where every augmenting path is a SHORTEST path (BFS).
 *  Each BFS is O(E); there are O(V E) augmentations  ->  O(V E^2).
 * ------------------------------------------------------------------------------------ */
int64_t MaxFlow::edmondsKarp(int s, int t) {
    int64_t flow = 0;
    std::vector<int> queue(static_cast<size_t>(n_));
    parentArc_.assign(static_cast<size_t>(n_), -1);
    for (;;) {
        ++phases;
        std::fill(parentArc_.begin(), parentArc_.end(), -1);
        parentArc_[static_cast<size_t>(s)] = -2;
        size_t qh = 0, qt = 0;
        queue[qt++] = s;
        while (qh < qt && parentArc_[static_cast<size_t>(t)] == -1) {          /* BFS */
            int u = queue[qh++];
            for (int a = head_[static_cast<size_t>(u)]; a != -1; a = next_[static_cast<size_t>(a)]) {
                int v = to_[static_cast<size_t>(a)];
                if (cap_[static_cast<size_t>(a)] > 0 && parentArc_[static_cast<size_t>(v)] == -1) {
                    parentArc_[static_cast<size_t>(v)] = a;
                    queue[qt++] = v;
                }
            }
        }
        if (parentArc_[static_cast<size_t>(t)] == -1) break;                     /* no path: done */
        int64_t b = std::numeric_limits<int64_t>::max();                         /* bottleneck */
        for (int v = t; v != s; v = to_[static_cast<size_t>(parentArc_[static_cast<size_t>(v)] ^ 1)])
            b = std::min(b, cap_[static_cast<size_t>(parentArc_[static_cast<size_t>(v)])]);
        for (int v = t; v != s; v = to_[static_cast<size_t>(parentArc_[static_cast<size_t>(v)] ^ 1)]) {
            int a = parentArc_[static_cast<size_t>(v)];
            cap_[static_cast<size_t>(a)] -= b;                                   /* push along   */
            cap_[static_cast<size_t>(a ^ 1)] += b;                               /* allow undo   */
        }
        flow += b;
        ++augmentations;
    }
    return flow;
}

/* ------------------------------------------------------------------------------------ *
 *  Dinic: BFS assigns levels (distance from s); then repeatedly push along paths that
 *  only go level -> level+1 (a "blocking flow").  Each phase strictly increases the
 *  s-t distance, so there are at most V phases.
 * ------------------------------------------------------------------------------------ */
bool MaxFlow::bfsLevels(int s, int t) {
    level_.assign(static_cast<size_t>(n_), -1);
    std::vector<int> queue;
    queue.reserve(static_cast<size_t>(n_));
    level_[static_cast<size_t>(s)] = 0;
    queue.push_back(s);
    for (size_t qh = 0; qh < queue.size(); ++qh) {
        int u = queue[qh];
        for (int a = head_[static_cast<size_t>(u)]; a != -1; a = next_[static_cast<size_t>(a)]) {
            int v = to_[static_cast<size_t>(a)];
            if (cap_[static_cast<size_t>(a)] > 0 && level_[static_cast<size_t>(v)] < 0) {
                level_[static_cast<size_t>(v)] = level_[static_cast<size_t>(u)] + 1;
                queue.push_back(v);
            }
        }
    }
    return level_[static_cast<size_t>(t)] >= 0;
}

/* One augmenting path in the level graph, found with an explicit stack (paths can be
 * tens of thousands of nodes long - recursion would overflow the call stack).
 * iter_[u] is the "current arc": arcs before it are known to be useless this phase. */
int64_t MaxFlow::dfsAugment(int s, int t) {
    std::vector<int> path;
    int u = s;
    for (;;) {
        if (u == t) {
            int64_t b = std::numeric_limits<int64_t>::max();
            for (int a : path) b = std::min(b, cap_[static_cast<size_t>(a)]);
            for (int a : path) { cap_[static_cast<size_t>(a)] -= b; cap_[static_cast<size_t>(a ^ 1)] += b; }
            return b;
        }
        int& a = iter_[static_cast<size_t>(u)];
        while (a != -1 && !(cap_[static_cast<size_t>(a)] > 0 &&
                            level_[static_cast<size_t>(to_[static_cast<size_t>(a)])] == level_[static_cast<size_t>(u)] + 1))
            a = next_[static_cast<size_t>(a)];
        if (a != -1) {                                     /* advance */
            path.push_back(a);
            u = to_[static_cast<size_t>(a)];
        } else {                                           /* retreat: u is a dead end */
            level_[static_cast<size_t>(u)] = -1;
            if (path.empty()) return 0;
            int back = path.back();
            path.pop_back();
            u = to_[static_cast<size_t>(back ^ 1)];
            iter_[static_cast<size_t>(u)] = next_[static_cast<size_t>(iter_[static_cast<size_t>(u)])];
        }
    }
}

int64_t MaxFlow::dinic(int s, int t) {
    int64_t flow = 0;
    while (bfsLevels(s, t)) {
        ++phases;
        iter_ = head_;
        while (int64_t f = dfsAugment(s, t)) { flow += f; ++augmentations; }
    }
    return flow;
}

std::vector<char> MaxFlow::sourceSide(int s) const {
    std::vector<char> seen(static_cast<size_t>(n_), 0);
    std::vector<int> stack{s};
    seen[static_cast<size_t>(s)] = 1;
    while (!stack.empty()) {
        int u = stack.back(); stack.pop_back();
        for (int a = head_[static_cast<size_t>(u)]; a != -1; a = next_[static_cast<size_t>(a)]) {
            int v = to_[static_cast<size_t>(a)];
            if (cap_[static_cast<size_t>(a)] > 0 && !seen[static_cast<size_t>(v)]) { seen[static_cast<size_t>(v)] = 1; stack.push_back(v); }
        }
    }
    return seen;
}

const char* maxFlowName(MaxFlowAlgo a) { return a == MaxFlowAlgo::Dinic ? "dinic" : "edmonds-karp"; }

/* ==================================================================================== *
 *  SEAM AS A MINIMUM CUT                                                               *
 * ==================================================================================== */

static double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

Seam findVerticalSeamGraphCut(const EnergyMap& e, const GraphCutOptions& opt, GraphCutStats* stats) {
    const int m = e.h, n = e.w;
    if (opt.briefConstruction && n < 2)
        throw std::runtime_error("brief's graph construction needs at least 2 columns");
    const int64_t INF = MaxFlow::INF;
    auto node = [n](int i, int j) { return i * n + j; };
    const int s = m * n, t = m * n + 1;

    auto t0 = std::chrono::steady_clock::now();
    MaxFlow g(m * n + 2);
    for (int i = 0; i < m; ++i) {
        g.addEdge(s, node(i, 0), INF);                                   /* source edges */
        for (int j = 0; j + 1 < n; ++j)                                  /* e[i][j] fwd, INF back */
            g.addEdge(node(i, j), node(i, j + 1), e.at(j, i), INF);
        g.addEdge(node(i, n - 1), t, opt.briefConstruction ? INF : e.at(n - 1, i));
        for (int j = 1; j < n; ++j) {                                    /* connectivity */
            if (i > 0)     g.addEdge(node(i, j), node(i - 1, j - 1), INF);
            if (i < m - 1) g.addEdge(node(i, j), node(i + 1, j - 1), INF);
        }
    }
    double buildMs = msSince(t0);

    t0 = std::chrono::steady_clock::now();
    int64_t flow = opt.algo == MaxFlowAlgo::Dinic ? g.dinic(s, t) : g.edmondsKarp(s, t);
    double flowMs = msSince(t0);

    t0 = std::chrono::steady_clock::now();
    std::vector<char> S = g.sourceSide(s);
    Seam seam;
    seam.idx.resize(static_cast<size_t>(m));
    for (int i = 0; i < m; ++i) {
        int c = -1;                                       /* rightmost pixel of row i in S */
        for (int j = 0; j < n; ++j) if (S[static_cast<size_t>(node(i, j))]) c = j;
        seam.idx[static_cast<size_t>(i)] = c;
        seam.cost += e.at(c, i);
    }
    double cutMs = msSince(t0);

    if (stats) {
        stats->nodes = g.nodeCount(); stats->arcs = g.arcCount();
        stats->augmentations = g.augmentations; stats->phases = g.phases; stats->flow = flow;
        stats->buildMs = buildMs; stats->flowMs = flowMs; stats->cutMs = cutMs;
    }
    return seam;
}

}  // namespace sc
