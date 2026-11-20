/* =====================================================================================
 *  File : seam_graphcut.h  Item 4 - GRAPH CUT (minimum s-t cut via maximum flow)
 *
 *  Graph for a VERTICAL seam on an m x n energy map (node (i,j) = pixel row i, col j):
 *
 *     s -> (i, 0)            capacity INF      column 0 is always on the source side
 *     (i,j) -> (i,j+1)       capacity e[i][j]  cutting it = removing pixel (i, j)
 *     (i,j+1) -> (i,j)       capacity INF      each row's source side is a PREFIX of
 *                                              columns, so each row is cut exactly once
 *     (i,j) -> (i-1,j-1)     capacity INF      the cut positions of adjacent rows differ
 *     (i,j) -> (i+1,j-1)     capacity INF      by at most one column: a connected seam
 *     (i,n-1) -> t           capacity e[i][n-1]   <-- see note
 *
 *  If S is the source side of a finite cut and c_i is the rightmost column of row i in
 *  S, the cut edges are exactly (i,c_i)->(i,c_i+1) [or (i,n-1)->t], so
 *      cut capacity = sum_i e[i][c_i] = energy of the seam {c_i}.
 *  Max-flow = min-cut (Ford-Fulkerson theorem) => the seam is OPTIMAL, same cost as DP.
 *
 *  NOTE (deviation from the brief, on purpose): the brief connects the last column to
 *  the sink with INF capacity.  That forces column n-1 onto the sink side, so a seam
 *  can never remove a last-column pixel and the answer can be worse than DP's.  Giving
 *  (i,n-1) -> t capacity e[i][n-1] (equivalently, a virtual all-sink column n) fixes
 *  it.  GraphCutOptions::briefConstruction = true reproduces the original for tests.
 *
 *  The INF backward edge is stored as the residual twin of the forward edge (capacity
 *  INF instead of 0), halving the arc count.  Nodes m n + 2, arcs about 6 m n.
 *
 *  Max-flow algorithms:
 *    Edmonds-Karp   BFS shortest augmenting paths,   O(V E^2)
 *    Dinic          BFS level graph + blocking flows, O(V^2 E); far faster in practice
 *  Min cut: BFS from s in the final residual graph (reachable set = S).
 * ===================================================================================== */
#pragma once

#include "seam.h"

namespace sc {

/* ------------------------------------------------------------------------------------ *
 *  Generic max-flow on a directed graph (forward-star adjacency, paired residual arcs).
 * ------------------------------------------------------------------------------------ */
class MaxFlow {
public:
    static constexpr int64_t INF = int64_t(1) << 50;

    explicit MaxFlow(int nodes);
    /* Adds u->v with capacity cap and its residual twin v->u with capacity revCap. */
    void    addEdge(int u, int v, int64_t cap, int64_t revCap = 0);
    int64_t edmondsKarp(int s, int t);
    int64_t dinic(int s, int t);
    /* After a max-flow: nodes reachable from s in the residual graph (= min-cut side S). */
    std::vector<char> sourceSide(int s) const;

    int     nodeCount() const { return n_; }
    int64_t arcCount()  const { return static_cast<int64_t>(to_.size()); }
    int64_t augmentations = 0;   /* augmenting paths pushed       */
    int64_t phases        = 0;   /* BFS passes (EK: = paths + 1)  */

private:
    bool    bfsLevels(int s, int t);
    int64_t dfsAugment(int s, int t);

    int n_;
    std::vector<int>     head_, next_, to_;
    std::vector<int64_t> cap_;                 /* residual capacity; arc a^1 is a's twin */
    std::vector<int>     level_, iter_, parentArc_;
};

enum class MaxFlowAlgo { EdmondsKarp, Dinic };
const char* maxFlowName(MaxFlowAlgo a);

struct GraphCutOptions {
    MaxFlowAlgo algo = MaxFlowAlgo::Dinic;
    bool briefConstruction = false;            /* INF sink edges, exactly as in the brief */
};

struct GraphCutStats {
    int     nodes = 0;
    int64_t arcs = 0, augmentations = 0, phases = 0, flow = 0;
    double  buildMs = 0, flowMs = 0, cutMs = 0;
};

Seam findVerticalSeamGraphCut(const EnergyMap& e, const GraphCutOptions& opt = {},
                              GraphCutStats* stats = nullptr);

}  // namespace sc
