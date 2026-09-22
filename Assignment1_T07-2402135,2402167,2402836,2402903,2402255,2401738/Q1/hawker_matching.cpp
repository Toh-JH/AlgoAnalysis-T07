/* =====================================================================================
 *  CSD3130 / CSD3131  Algorithm Analysis - Assignment (Part 1)
 *  QUESTION 1 : Stable Matching in a Singapore Context
 *               "NEA Hawker Stall Allocation Exercise"
 *
 *  Student(s) : [STUDENT NAME - STUDENT ID]
 *  Team       : Txx
 *  File       : hawker_matching.cpp        (C++17, single file, console only)
 *
 *  PROBLEM
 *  -------
 *  The National Environment Agency (NEA) periodically releases vacant cooked-food
 *  stalls across the public hawker centres it manages.  Far more hawkers apply than
 *  there are stalls, so this is exactly the "surplus of applicants" situation of the
 *  National Resident Matching Program described in the assignment brief:
 *
 *        hospitals  ->  hawker centres    (each with a quota of vacant stalls)
 *        students   ->  hawker applicants (each wants at most one stall)
 *
 *  Both sides rank each other from measurable criteria (see buildPreferences()).
 *  The program computes a STABLE allocation using the Gale-Shapley algorithm
 *  generalised to capacities (the Hospital-Residents problem), then independently
 *  verifies stability by brute force.
 *
 *  BUILD (Developer Command Prompt for VS 2022)
 *        cl /nologo /std:c++17 /EHsc /O2 /W4 hawker_matching.cpp /Fe:hawker_matching.exe
 *  BUILD (g++)
 *        g++ -std=c++17 -O2 -Wall -Wextra -o hawker_matching hawker_matching.cpp
 *  RUN   hawker_matching.exe            interactive menu
 *        hawker_matching.exe --all      run every demo and every test, then exit
 * ===================================================================================== */

#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <random>
#include <chrono>
#include <cmath>
#include <limits>

/* ==================================================================================== *
 *  SECTION 1 : DOMAIN MODEL                                                            *
 * ==================================================================================== */

/* One hawker applying for a stall. */
struct Applicant {
    std::string name;
    std::string cuisine;       /* cuisine this hawker intends to sell                  */
    double      x = 0, y = 0;  /* home location on a simplified km grid of Singapore   */
    int         bid = 0;       /* monthly tender bid offered, in SGD                   */
    char        hygiene = 'A'; /* NEA food-hygiene grade of the hawker's track record  */
    int         years = 0;     /* years of hawking / F&B experience                    */
};

/* One hawker centre releasing stalls. */
struct Centre {
    std::string name;
    std::string town;
    int         quota = 1;     /* number of vacant stalls = capacity                   */
    int         rent = 0;      /* assessed monthly rent, in SGD                        */
    int         footfall = 0;  /* average daily patron count                           */
    double      x = 0, y = 0;  /* centre location on the same km grid                  */
    std::vector<std::string> gaps; /* cuisines currently MISSING from the stall mix    */
};

/* Sentinel rank meaning "this party is not on my list at all". */
static const int UNACCEPTABLE = std::numeric_limits<int>::max();

/* A complete problem instance. */
struct Instance {
    std::vector<Applicant> applicants;
    std::vector<Centre>    centres;

    /* prefA[a] = centre indices, most preferred first.
       prefC[c] = applicant indices, most preferred first.
       A party omitted from a list is UNACCEPTABLE to the owner of that list. */
    std::vector<std::vector<int>> prefA, prefC;

    /* rankC[c][a] = position of applicant a inside prefC[c]   (UNACCEPTABLE if absent)
       rankA[a][c] = position of centre    c inside prefA[a]   (UNACCEPTABLE if absent)
       These two tables are what make every preference question a single int compare. */
    std::vector<std::vector<int>> rankC, rankA;

    int n() const { return static_cast<int>(applicants.size()); }
    int m() const { return static_cast<int>(centres.size()); }
    int totalStalls() const {
        int t = 0;
        for (const Centre& c : centres) t += c.quota;
        return t;
    }
};

/* Result of one matching run. */
struct Matching {
    std::vector<int>              assignedCentre; /* per applicant: centre index or -1 */
    std::vector<std::vector<int>> hired;          /* per centre  : applicant indices   */
    long long proposals   = 0;                    /* instrumentation                   */
    long long comparisons = 0;                    /* instrumentation                   */
    double    milliseconds = 0.0;

    int matchedCount() const {
        int k = 0;
        for (int c : assignedCentre) if (c >= 0) ++k;
        return k;
    }
};

/* A pair that would break the allocation, classified as in the assignment brief. */
struct BlockingPair {
    int         applicant;
    int         centre;
    std::string type;
};

/* ==================================================================================== *
 *  SECTION 2 : BUILDING PREFERENCE LISTS FROM REAL CRITERIA                            *
 * ==================================================================================== */

/* Min-max normalisation to [0,1].  A degenerate range maps everything to 0.5 so the
   criterion simply stops discriminating instead of dividing by zero. */
static double normalise(double v, double lo, double hi) {
    if (hi - lo < 1e-12) return 0.5;
    return (v - lo) / (hi - lo);
}

static double distanceKm(double ax, double ay, double bx, double by) {
    const double dx = ax - bx, dy = ay - by;
    return std::sqrt(dx * dx + dy * dy);
}

static double hygieneScore(char grade) {
    switch (grade) {
        case 'A': return 1.0;
        case 'B': return 0.6;
        default:  return 0.2;            /* grade 'C' */
    }
}

/*  APPLICANT'S CRITERIA - a hawker wants many customers, cheap rent, short commute:
 *      score = 0.50*footfall_n - 0.30*rent_n - 0.20*distance_n      (higher = better)
 *
 *  CENTRE'S CRITERIA - a centre wants a cuisine that fills a gap in its stall mix, a
 *  strong tender bid, a clean hygiene record and proven experience:
 *      score = 0.35*gapFit + 0.30*bid_n + 0.20*hygiene + 0.15*years_n
 *
 *  Equal scores are broken by the smaller index so that every run is reproducible.
 *  Cost: O(n*m) score evaluations + O(n*m log m + m*n log n) for the sorts.
 */
static void buildPreferences(Instance& inst) {
    const int n = inst.n(), m = inst.m();

    double rentLo = 1e18, rentHi = -1e18, footLo = 1e18, footHi = -1e18;
    for (const Centre& c : inst.centres) {
        rentLo = std::min(rentLo, static_cast<double>(c.rent));
        rentHi = std::max(rentHi, static_cast<double>(c.rent));
        footLo = std::min(footLo, static_cast<double>(c.footfall));
        footHi = std::max(footHi, static_cast<double>(c.footfall));
    }
    double bidLo = 1e18, bidHi = -1e18, yrLo = 1e18, yrHi = -1e18;
    for (const Applicant& a : inst.applicants) {
        bidLo = std::min(bidLo, static_cast<double>(a.bid));
        bidHi = std::max(bidHi, static_cast<double>(a.bid));
        yrLo  = std::min(yrLo,  static_cast<double>(a.years));
        yrHi  = std::max(yrHi,  static_cast<double>(a.years));
    }
    double distLo = 1e18, distHi = -1e18;
    for (int a = 0; a < n; ++a)
        for (int c = 0; c < m; ++c) {
            const double d = distanceKm(inst.applicants[a].x, inst.applicants[a].y,
                                        inst.centres[c].x,    inst.centres[c].y);
            distLo = std::min(distLo, d);
            distHi = std::max(distHi, d);
        }

    inst.prefA.assign(n, {});
    inst.prefC.assign(m, {});

    for (int a = 0; a < n; ++a) {
        std::vector<std::pair<double, int>> scored;
        scored.reserve(m);
        for (int c = 0; c < m; ++c) {
            const Centre& ct = inst.centres[c];
            const double d = distanceKm(inst.applicants[a].x, inst.applicants[a].y, ct.x, ct.y);
            const double s = 0.50 * normalise(ct.footfall, footLo, footHi)
                           - 0.30 * normalise(ct.rent,     rentLo, rentHi)
                           - 0.20 * normalise(d,           distLo, distHi);
            scored.push_back(std::make_pair(s, c));
        }
        std::sort(scored.begin(), scored.end(),
                  [](const std::pair<double,int>& p, const std::pair<double,int>& q) {
                      if (p.first != q.first) return p.first > q.first;
                      return p.second < q.second;
                  });
        for (const auto& p : scored) inst.prefA[a].push_back(p.second);
    }

    for (int c = 0; c < m; ++c) {
        const Centre& ct = inst.centres[c];
        std::vector<std::pair<double, int>> scored;
        scored.reserve(n);
        for (int a = 0; a < n; ++a) {
            const Applicant& ap = inst.applicants[a];
            const bool fillsGap =
                std::find(ct.gaps.begin(), ct.gaps.end(), ap.cuisine) != ct.gaps.end();
            const double s = 0.35 * (fillsGap ? 1.0 : 0.30)
                           + 0.30 * normalise(ap.bid,   bidLo, bidHi)
                           + 0.20 * hygieneScore(ap.hygiene)
                           + 0.15 * normalise(ap.years, yrLo,  yrHi);
            scored.push_back(std::make_pair(s, a));
        }
        std::sort(scored.begin(), scored.end(),
                  [](const std::pair<double,int>& p, const std::pair<double,int>& q) {
                      if (p.first != q.first) return p.first > q.first;
                      return p.second < q.second;
                  });
        for (const auto& p : scored) inst.prefC[c].push_back(p.second);
    }
}

/* Build the O(1) rank lookup tables from the preference lists.  Cost O(n*m). */
static void buildRankTables(Instance& inst) {
    const int n = inst.n(), m = inst.m();
    inst.rankC.assign(m, std::vector<int>(n, UNACCEPTABLE));
    inst.rankA.assign(n, std::vector<int>(m, UNACCEPTABLE));
    for (int c = 0; c < m; ++c)
        for (int i = 0; i < static_cast<int>(inst.prefC[c].size()); ++i)
            inst.rankC[c][inst.prefC[c][i]] = i;
    for (int a = 0; a < n; ++a)
        for (int i = 0; i < static_cast<int>(inst.prefA[a].size()); ++i)
            inst.rankA[a][inst.prefA[a][i]] = i;
}

/* ==================================================================================== *
 *  SECTION 3 : THE ALGORITHM - GALE-SHAPLEY WITH CAPACITIES                            *
 * ==================================================================================== */

/*  Applicant-proposing Gale-Shapley (Hospital-Residents form).
 *
 *  Every free applicant proposes down its own list.  A centre provisionally accepts
 *  while it is under quota; once full it compares the proposer against the WORST
 *  applicant it currently holds and swaps if the proposer is better.
 *
 *  Finding "the worst applicant currently held" is the only non-trivial step.  We use
 *  the fact that a centre's held set can only ever IMPROVE: once the centre is full,
 *  every applicant it later admits is strictly better than the one it evicts, so the
 *  rank of its worst holder is monotonically non-increasing.  Keeping one downward
 *  pointer per centre (worstPtr) therefore costs O(|prefC[c]|) over the WHOLE run
 *  instead of O(log q) per proposal - the eviction bookkeeping becomes O(1) amortised.
 *
 *  Time  : O(n*m)  - each applicant proposes to each centre at most once, each
 *                    proposal is O(1) amortised, pointer scans total O(n*m).
 *  Space : O(n*m)  - dominated by the two rank tables.
 */
static Matching galeShapleyApplicantProposing(const Instance& inst,
                                              bool traceOn = false,
                                              int  traceLimit = 40) {
    const int n = inst.n(), m = inst.m();
    const auto t0 = std::chrono::high_resolution_clock::now();

    Matching res;
    res.assignedCentre.assign(n, -1);
    res.hired.assign(m, {});

    /* heldByRank[c][r] : is the applicant at rank r of centre c's list held by c? */
    std::vector<std::vector<char>> heldByRank(m);
    std::vector<int> worstPtr(m, 0), filled(m, 0);
    for (int c = 0; c < m; ++c) {
        heldByRank[c].assign(inst.prefC[c].size(), 0);
        worstPtr[c] = static_cast<int>(inst.prefC[c].size()) - 1;
    }

    std::vector<int> nextIdx(n, 0);              /* next centre each applicant will try */
    std::vector<int> freeStack(n);
    for (int a = 0; a < n; ++a) freeStack[a] = n - 1 - a;   /* process A0 first */

    int traced = 0;
    while (!freeStack.empty()) {
        const int a = freeStack.back();
        freeStack.pop_back();

        while (nextIdx[a] < static_cast<int>(inst.prefA[a].size())) {
            const int c = inst.prefA[a][nextIdx[a]++];
            if (inst.rankC[c][a] == UNACCEPTABLE) continue;  /* centre would never take a */

            ++res.proposals;
            const int quota = inst.centres[c].quota;

            if (quota == 0) continue;                        /* nothing to offer */

            if (filled[c] < quota) {                         /* free stall -> accept */
                heldByRank[c][inst.rankC[c][a]] = 1;
                ++filled[c];
                res.assignedCentre[a] = c;
                if (traceOn && traced < traceLimit) {
                    std::cout << "    A" << std::setw(2) << std::setfill('0') << a
                              << std::setfill(' ') << " proposes to C" << c
                              << "  -> accepted (stall " << filled[c] << "/" << quota << ")\n";
                    ++traced;
                }
                break;
            }

            /* Centre is full: locate its current worst holder. */
            while (worstPtr[c] >= 0 && !heldByRank[c][worstPtr[c]]) --worstPtr[c];
            const int worstRank = worstPtr[c];
            ++res.comparisons;

            if (inst.rankC[c][a] < worstRank) {               /* proposer is better */
                const int victim = inst.prefC[c][worstRank];
                heldByRank[c][worstRank] = 0;
                res.assignedCentre[victim] = -1;
                heldByRank[c][inst.rankC[c][a]] = 1;
                res.assignedCentre[a] = c;
                freeStack.push_back(victim);                  /* victim becomes free */
                if (traceOn && traced < traceLimit) {
                    std::cout << "    A" << std::setw(2) << std::setfill('0') << a
                              << std::setfill(' ') << " proposes to C" << c
                              << "  -> accepted, evicts A" << std::setw(2)
                              << std::setfill('0') << victim << std::setfill(' ') << "\n";
                    ++traced;
                }
                break;
            }

            if (traceOn && traced < traceLimit) {
                std::cout << "    A" << std::setw(2) << std::setfill('0') << a
                          << std::setfill(' ') << " proposes to C" << c
                          << "  -> rejected (full, all holders rank better)\n";
                ++traced;
            }
        }
    }
    if (traceOn && traced >= traceLimit)
        std::cout << "    ... trace truncated after " << traceLimit << " events ...\n";

    for (int c = 0; c < m; ++c)
        for (int r = 0; r < static_cast<int>(heldByRank[c].size()); ++r)
            if (heldByRank[c][r]) res.hired[c].push_back(inst.prefC[c][r]);

    const auto t1 = std::chrono::high_resolution_clock::now();
    res.milliseconds = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return res;
}

/*  Reference implementation used purely as a testing oracle.  Identical logic but the
 *  worst holder is found by a full linear scan, giving O(n*m*q).  Any disagreement
 *  between this and the optimised version is a bug in the optimisation. */
static Matching galeShapleyNaive(const Instance& inst) {
    const int n = inst.n(), m = inst.m();
    Matching res;
    res.assignedCentre.assign(n, -1);
    res.hired.assign(m, {});

    std::vector<int> nextIdx(n, 0);
    std::vector<int> freeStack(n);
    for (int a = 0; a < n; ++a) freeStack[a] = n - 1 - a;

    while (!freeStack.empty()) {
        const int a = freeStack.back();
        freeStack.pop_back();
        while (nextIdx[a] < static_cast<int>(inst.prefA[a].size())) {
            const int c = inst.prefA[a][nextIdx[a]++];
            if (inst.rankC[c][a] == UNACCEPTABLE) continue;
            ++res.proposals;
            const int quota = inst.centres[c].quota;
            if (quota == 0) continue;

            if (static_cast<int>(res.hired[c].size()) < quota) {
                res.hired[c].push_back(a);
                res.assignedCentre[a] = c;
                break;
            }
            int worstPos = 0;
            for (int i = 1; i < static_cast<int>(res.hired[c].size()); ++i)
                if (inst.rankC[c][res.hired[c][i]] > inst.rankC[c][res.hired[c][worstPos]])
                    worstPos = i;
            const int worst = res.hired[c][worstPos];
            if (inst.rankC[c][a] < inst.rankC[c][worst]) {
                res.hired[c][worstPos] = a;
                res.assignedCentre[a] = c;
                res.assignedCentre[worst] = -1;
                freeStack.push_back(worst);
                break;
            }
        }
    }
    for (int c = 0; c < m; ++c)
        std::sort(res.hired[c].begin(), res.hired[c].end(),
                  [&](int p, int q) { return inst.rankC[c][p] < inst.rankC[c][q]; });
    return res;
}

/*  Centre-proposing variant.  Produces the CENTRE-optimal / applicant-pessimal stable
 *  matching, used in the report to show that the proposing side gains.  O(n*m). */
static Matching galeShapleyCentreProposing(const Instance& inst) {
    const int n = inst.n(), m = inst.m();
    const auto t0 = std::chrono::high_resolution_clock::now();

    Matching res;
    res.assignedCentre.assign(n, -1);
    res.hired.assign(m, {});

    std::vector<int> nextIdx(m, 0), filled(m, 0);
    std::vector<int> queueC;
    for (int c = m - 1; c >= 0; --c) if (inst.centres[c].quota > 0) queueC.push_back(c);

    while (!queueC.empty()) {
        const int c = queueC.back();
        queueC.pop_back();
        while (filled[c] < inst.centres[c].quota &&
               nextIdx[c] < static_cast<int>(inst.prefC[c].size())) {
            const int a = inst.prefC[c][nextIdx[c]++];
            if (inst.rankA[a][c] == UNACCEPTABLE) continue;
            ++res.proposals;
            const int cur = res.assignedCentre[a];
            if (cur == -1) {
                res.assignedCentre[a] = c;
                ++filled[c];
            } else {
                ++res.comparisons;
                if (inst.rankA[a][c] < inst.rankA[a][cur]) {  /* applicant upgrades */
                    res.assignedCentre[a] = c;
                    ++filled[c];
                    --filled[cur];
                    queueC.push_back(cur);                    /* cur has a free stall now */
                }
            }
        }
    }
    for (int a = 0; a < n; ++a)
        if (res.assignedCentre[a] >= 0) res.hired[res.assignedCentre[a]].push_back(a);
    for (int c = 0; c < m; ++c)
        std::sort(res.hired[c].begin(), res.hired[c].end(),
                  [&](int p, int q) { return inst.rankC[c][p] < inst.rankC[c][q]; });

    const auto t1 = std::chrono::high_resolution_clock::now();
    res.milliseconds = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return res;
}

/*  Classic 1-to-1 Gale-Shapley, used only to cross-check the capacitated version on
 *  instances where every quota is 1. */
static std::vector<int> classicGaleShapley(const Instance& inst) {
    const int n = inst.n(), m = inst.m();
    std::vector<int> assigned(n, -1), holder(m, -1), nextIdx(n, 0);
    std::vector<int> freeStack(n);
    for (int a = 0; a < n; ++a) freeStack[a] = n - 1 - a;
    while (!freeStack.empty()) {
        const int a = freeStack.back();
        freeStack.pop_back();
        while (nextIdx[a] < static_cast<int>(inst.prefA[a].size())) {
            const int c = inst.prefA[a][nextIdx[a]++];
            if (inst.rankC[c][a] == UNACCEPTABLE) continue;
            if (holder[c] == -1) { holder[c] = a; assigned[a] = c; break; }
            if (inst.rankC[c][a] < inst.rankC[c][holder[c]]) {
                const int victim = holder[c];
                holder[c] = a; assigned[a] = c; assigned[victim] = -1;
                freeStack.push_back(victim);
                break;
            }
        }
    }
    return assigned;
}

/* ==================================================================================== *
 *  SECTION 4 : INDEPENDENT STABILITY VERIFICATION (BRUTE FORCE)                        *
 * ==================================================================================== */

/*  Scans every (applicant, centre) pair and reports each blocking pair, classified
 *  exactly as the assignment brief defines instability:
 *
 *    Type-1  applicant a is assigned nowhere, centre c holds someone it ranks below a.
 *    Type-2  a is at c', c holds someone it ranks below a, and a prefers c to c'.
 *    Free    centre c still has an empty stall and a (acceptable to it) would rather
 *            be there than where it is - only possible with incomplete lists.
 *
 *  Time O(n*m), independent of the algorithm above, so it is a genuine oracle.
 */
static std::vector<BlockingPair> findBlockingPairs(const Instance& inst, const Matching& mt) {
    std::vector<BlockingPair> bad;
    const int n = inst.n(), m = inst.m();
    for (int a = 0; a < n; ++a) {
        const int cur = mt.assignedCentre[a];
        for (int c = 0; c < m; ++c) {
            if (c == cur) continue;
            if (inst.rankC[c][a] == UNACCEPTABLE) continue;  /* c will not take a      */
            if (inst.rankA[a][c] == UNACCEPTABLE) continue;  /* a will not go to c     */
            const bool aWantsSwap = (cur == -1) || (inst.rankA[a][c] < inst.rankA[a][cur]);
            if (!aWantsSwap) continue;

            if (static_cast<int>(mt.hired[c].size()) < inst.centres[c].quota) {
                bad.push_back({ a, c, "Free-stall" });
                continue;
            }
            int worst = -1;
            for (int h : mt.hired[c])
                if (worst == -1 || inst.rankC[c][h] > inst.rankC[c][worst]) worst = h;
            if (worst != -1 && inst.rankC[c][a] < inst.rankC[c][worst])
                bad.push_back({ a, c, (cur == -1) ? "Type-1" : "Type-2" });
        }
    }
    return bad;
}

/*  Basic structural checks: nobody holds two stalls, no centre exceeds its quota,
 *  nobody is placed somewhere unacceptable, and the two views agree. */
static bool isStructurallyValid(const Instance& inst, const Matching& mt, std::string& why) {
    const int n = inst.n(), m = inst.m();
    for (int c = 0; c < m; ++c) {
        if (static_cast<int>(mt.hired[c].size()) > inst.centres[c].quota) {
            why = "centre C" + std::to_string(c) + " exceeds its quota";
            return false;
        }
        for (int a : mt.hired[c]) {
            if (mt.assignedCentre[a] != c) { why = "hired[] and assignedCentre[] disagree"; return false; }
            if (inst.rankC[c][a] == UNACCEPTABLE) { why = "centre hired an unacceptable applicant"; return false; }
        }
    }
    for (int a = 0; a < n; ++a) {
        const int c = mt.assignedCentre[a];
        if (c == -1) continue;
        if (c < 0 || c >= m) { why = "assignment out of range"; return false; }
        if (inst.rankA[a][c] == UNACCEPTABLE) { why = "applicant placed at an unacceptable centre"; return false; }
        if (std::find(mt.hired[c].begin(), mt.hired[c].end(), a) == mt.hired[c].end()) {
            why = "applicant not listed by its centre"; return false;
        }
    }
    why.clear();
    return true;
}

/* Mean 1-based preference position obtained by the matched applicants. */
static double averageApplicantRank(const Instance& inst, const Matching& mt) {
    double sum = 0; int k = 0;
    for (int a = 0; a < inst.n(); ++a)
        if (mt.assignedCentre[a] >= 0) { sum += inst.rankA[a][mt.assignedCentre[a]] + 1; ++k; }
    return k ? sum / k : 0.0;
}

/* ==================================================================================== *
 *  SECTION 5 : THE DEFAULT SINGAPORE DATA SET                                          *
 * ==================================================================================== */

static Instance buildDefaultInstance() {
    Instance in;
    /*            name                        town            quota rent  foot    x     y    gaps */
    in.centres = {
        { "Old Airport Road Food Centre", "Mountbatten",   3, 1800, 4200, 12.0,  8.0, { "Nasi Padang", "Dessert"        } },
        { "Chomp Chomp Food Centre",      "Serangoon",     2, 1500, 3000, 14.0, 14.0, { "BBQ Seafood", "Indian"         } },
        { "Maxwell Food Centre",          "Tanjong Pagar", 2, 2400, 5200,  9.5,  6.0, { "Western",     "Dessert"        } },
        { "Tekka Centre",                 "Little India",  2, 1600, 3600, 10.5,  9.5, { "Chinese Zi Char", "Dessert"    } },
        { "Ghim Moh Market",              "Queenstown",    2, 1300, 2800,  6.0,  9.0, { "Halal Muslim", "Noodles"       } },
        { "Bedok Interchange Food Centre","Bedok",         1, 1700, 3900, 17.0,  9.0, { "Vegetarian",  "Western"        } }
    };
    /*               name              cuisine             x     y    bid   hyg yrs */
    in.applicants = {
        { "Tan Wei Ming",    "Chicken Rice",     11.0,  8.0, 1500, 'A', 12 },
        { "Siti Nurhaliza",  "Nasi Padang",      13.0, 10.0, 1400, 'A',  8 },
        { "Raj Kumar",       "Indian",           10.0,  9.0, 1250, 'B', 15 },
        { "Lim Hui Ling",    "Dessert",           7.0,  9.0, 1600, 'A',  5 },
        { "Chan Kok Wai",    "Chinese Zi Char",  12.0, 12.0, 1350, 'B', 20 },
        { "Nurul Aisyah",    "Halal Muslim",     16.0,  9.0, 1200, 'A',  6 },
        { "Goh Jia Hui",     "Noodles",           6.0, 10.0, 1450, 'A',  9 },
        { "Muthu Samy",      "Indian",           10.0, 10.0, 1300, 'C', 18 },
        { "Wong Mei Fang",   "Dessert",           9.0,  7.0, 1700, 'A',  4 },
        { "Ahmad Faizal",    "BBQ Seafood",      15.0, 13.0, 1550, 'B', 11 },
        { "Ong Boon Keng",   "Western",           8.0,  6.0, 1800, 'A',  3 },
        { "Kavitha Rani",    "Vegetarian",       11.0, 10.0, 1150, 'B',  7 },
        { "Teo Seng Huat",   "Chicken Rice",     17.0, 10.0, 1250, 'B', 14 },
        { "Yusof Bin Ali",   "Nasi Padang",      14.0,  9.0, 1300, 'A', 10 },
        { "Chua Li Ting",    "Noodles",           5.0,  9.0, 1400, 'A',  2 },
        { "Sharifah Aziz",   "Halal Muslim",     16.0,  8.0, 1350, 'B', 13 },
        { "Lee Chee Meng",   "Chinese Zi Char",  13.0, 13.0, 1500, 'A', 16 },
        { "Devi Lakshmi",    "Western",           9.0,  6.0, 1650, 'C',  5 }
    };
    buildPreferences(in);
    buildRankTables(in);
    return in;
}

/*  A deliberately cyclic three-hawker / three-centre exercise.  Preferences rotate, so
 *  the instance has THREE stable matchings and the applicant-optimal one differs from
 *  the centre-optimal one.  It is the smallest data set that exposes the proposer's
 *  advantage, which the large derived data set above cannot show (see demoProposerAdvantage). */
static Instance buildCyclicInstance() {
    Instance in;
    in.applicants.resize(3);
    in.centres.resize(3);
    in.applicants[0].name = "Kamala Devi";   in.applicants[0].cuisine = "Indian Rojak";
    in.applicants[1].name = "Ng Ah Huat";    in.applicants[1].cuisine = "Carrot Cake";
    in.applicants[2].name = "Hafiz Rahman";  in.applicants[2].cuisine = "Mee Rebus";
    in.centres[0].name = "Adam Road FC";     in.centres[0].quota = 1;
    in.centres[1].name = "Amoy Street FC";   in.centres[1].quota = 1;
    in.centres[2].name = "Zion Riverside FC";in.centres[2].quota = 1;
    in.prefA = { {0,1,2}, {1,2,0}, {2,0,1} };
    in.prefC = { {1,2,0}, {2,0,1}, {0,1,2} };
    buildRankTables(in);
    return in;
}

/* A reproducible random instance for stress and scalability testing. */
static Instance buildRandomInstance(int n, int m, int quotaLo, int quotaHi,
                                    unsigned seed, int listLen = -1) {
    std::mt19937 rng(seed);
    Instance in;
    in.applicants.resize(n);
    in.centres.resize(m);
    for (int a = 0; a < n; ++a) in.applicants[a].name = "A" + std::to_string(a);
    std::uniform_int_distribution<int> qd(quotaLo, quotaHi);
    for (int c = 0; c < m; ++c) {
        in.centres[c].name  = "C" + std::to_string(c);
        in.centres[c].quota = qd(rng);
    }
    const int lenA = (listLen < 0 || listLen > m) ? m : listLen;
    in.prefA.assign(n, {});
    in.prefC.assign(m, {});
    std::vector<int> base;
    for (int a = 0; a < n; ++a) {
        base.resize(m);
        std::iota(base.begin(), base.end(), 0);
        std::shuffle(base.begin(), base.end(), rng);
        in.prefA[a].assign(base.begin(), base.begin() + lenA);
    }
    for (int c = 0; c < m; ++c) {
        base.resize(n);
        std::iota(base.begin(), base.end(), 0);
        std::shuffle(base.begin(), base.end(), rng);
        in.prefC[c] = base;
    }
    /* With truncated applicant lists, a centre must not rank someone who cannot come. */
    if (lenA < m) {
        std::vector<std::vector<char>> ok(n, std::vector<char>(m, 0));
        for (int a = 0; a < n; ++a) for (int c : in.prefA[a]) ok[a][c] = 1;
        for (int c = 0; c < m; ++c) {
            std::vector<int> keep;
            for (int a : in.prefC[c]) if (ok[a][c]) keep.push_back(a);
            in.prefC[c] = keep;
        }
    }
    buildRankTables(in);
    return in;
}

/* ==================================================================================== *
 *  SECTION 6 : REPORTING                                                               *
 * ==================================================================================== */

static std::string aid(int a) {
    std::ostringstream os;
    os << "A" << std::setw(2) << std::setfill('0') << a;
    return os.str();
}

static void printDataset(const Instance& in) {
    std::cout << "\n--- HAWKER CENTRES (" << in.m() << ", total vacant stalls = "
              << in.totalStalls() << ") ---\n";
    std::cout << std::left << std::setw(4) << "ID" << std::setw(32) << "Centre"
              << std::setw(15) << "Town" << std::right << std::setw(7) << "Stalls"
              << std::setw(8) << "Rent" << std::setw(10) << "Footfall" << "  Cuisine gaps\n";
    for (int c = 0; c < in.m(); ++c) {
        const Centre& ct = in.centres[c];
        std::cout << std::left << std::setw(4) << ("C" + std::to_string(c))
                  << std::setw(32) << ct.name << std::setw(15) << ct.town
                  << std::right << std::setw(7) << ct.quota
                  << std::setw(8) << ct.rent << std::setw(10) << ct.footfall << "  ";
        for (size_t g = 0; g < ct.gaps.size(); ++g)
            std::cout << (g ? ", " : "") << ct.gaps[g];
        std::cout << "\n";
    }

    std::cout << "\n--- HAWKER APPLICANTS (" << in.n() << ") ---\n";
    std::cout << std::left << std::setw(5) << "ID" << std::setw(18) << "Name"
              << std::setw(18) << "Cuisine" << std::right << std::setw(7) << "Bid"
              << std::setw(9) << "Hygiene" << std::setw(7) << "Years" << "\n";
    for (int a = 0; a < in.n(); ++a) {
        const Applicant& ap = in.applicants[a];
        std::cout << std::left << std::setw(5) << aid(a) << std::setw(18) << ap.name
                  << std::setw(18) << ap.cuisine << std::right << std::setw(7) << ap.bid
                  << std::setw(9) << ap.hygiene << std::setw(7) << ap.years << "\n";
    }
}

static void printPreferences(const Instance& in) {
    std::cout << "\n--- APPLICANT PREFERENCE LISTS (best first) ---\n";
    std::cout << "    criteria: 0.50*footfall - 0.30*rent - 0.20*distance-from-home\n";
    for (int a = 0; a < in.n(); ++a) {
        std::cout << "  " << aid(a) << " " << std::left << std::setw(16)
                  << in.applicants[a].name << std::right << ": ";
        for (int c : in.prefA[a]) std::cout << "C" << c << " ";
        std::cout << "\n";
    }
    std::cout << "\n--- CENTRE PREFERENCE LISTS (best first) ---\n";
    std::cout << "    criteria: 0.35*cuisine-gap-fit + 0.30*bid + 0.20*hygiene + 0.15*experience\n";
    for (int c = 0; c < in.m(); ++c) {
        std::cout << "  C" << c << " " << std::left << std::setw(30)
                  << in.centres[c].name << std::right << ": ";
        for (int a : in.prefC[c]) std::cout << aid(a) << " ";
        std::cout << "\n";
    }
}

static void printMatching(const Instance& in, const Matching& mt, const std::string& title) {
    std::cout << "\n--- " << title << " ---\n";
    for (int c = 0; c < in.m(); ++c) {
        std::cout << "  C" << c << " " << std::left << std::setw(32) << in.centres[c].name
                  << std::right << "[" << mt.hired[c].size() << "/"
                  << in.centres[c].quota << "] ";
        if (mt.hired[c].empty()) std::cout << "(no stall filled)";
        for (int a : mt.hired[c])
            std::cout << aid(a) << ":" << in.applicants[a].name
                      << "(" << in.applicants[a].cuisine << ")  ";
        std::cout << "\n";
    }
    std::cout << "  Unplaced applicants: ";
    int un = 0;
    for (int a = 0; a < in.n(); ++a)
        if (mt.assignedCentre[a] == -1) { std::cout << aid(a) << " "; ++un; }
    if (un == 0) std::cout << "(none)";
    std::cout << "\n  Placed " << mt.matchedCount() << " of " << in.n()
              << " applicants into " << in.totalStalls() << " stalls; "
              << un << " unplaced.\n"
              << "  Proposals made = " << mt.proposals
              << ", rank comparisons = " << mt.comparisons
              << ", time = " << std::fixed << std::setprecision(3)
              << mt.milliseconds << " ms\n" << std::defaultfloat;
}

static void printStabilityAudit(const Instance& in, const Matching& mt) {
    const std::vector<BlockingPair> bad = findBlockingPairs(in, mt);
    std::cout << "\n--- STABILITY AUDIT (brute force over all " << in.n() * in.m()
              << " applicant-centre pairs) ---\n";
    std::cout << "  Type-1 instability: applicant unplaced, yet a centre prefers it to a hire.\n";
    std::cout << "  Type-2 instability: applicant and centre would both rather swap.\n";
    if (bad.empty()) {
        std::cout << "  Blocking pairs found: 0   ==> THE ALLOCATION IS STABLE.\n";
    } else {
        std::cout << "  Blocking pairs found: " << bad.size() << "  ==> NOT STABLE\n";
        for (const BlockingPair& b : bad)
            std::cout << "    [" << b.type << "] " << aid(b.applicant) << " + C" << b.centre << "\n";
    }
}

/* ==================================================================================== *
 *  SECTION 7 : DEMONSTRATIONS                                                          *
 * ==================================================================================== */

static void demoDefaultScenario() {
    std::cout << "\n================ DEMO 1 : THE NEA HAWKER STALL EXERCISE ================\n";
    Instance in = buildDefaultInstance();
    printDataset(in);
    printPreferences(in);
}

static void demoSolveDefault() {
    std::cout << "\n================ DEMO 2 : GALE-SHAPLEY WITH CAPACITIES ================\n";
    Instance in = buildDefaultInstance();
    std::cout << "\n--- PROPOSAL TRACE (applicant-proposing) ---\n";
    Matching mt = galeShapleyApplicantProposing(in, true, 40);
    printMatching(in, mt, "FINAL STALL ALLOCATION");
    printStabilityAudit(in, mt);
    std::cout << "  Average preference position obtained by a placed applicant: "
              << std::fixed << std::setprecision(3) << averageApplicantRank(in, mt)
              << std::defaultfloat << "\n";
}

static void demoProposerAdvantage() {
    std::cout << "\n========= DEMO 3 : WHO PROPOSES MATTERS (applicant vs centre) =========\n";
    Instance in = buildDefaultInstance();
    Matching ap = galeShapleyApplicantProposing(in);
    Matching cp = galeShapleyCentreProposing(in);
    printMatching(in, ap, "APPLICANT-PROPOSING (applicant-optimal)");
    printMatching(in, cp, "CENTRE-PROPOSING   (centre-optimal)");

    const std::vector<BlockingPair> b1 = findBlockingPairs(in, ap);
    const std::vector<BlockingPair> b2 = findBlockingPairs(in, cp);
    const bool identical = (ap.assignedCentre == cp.assignedCentre);
    std::cout << "\n  Blocking pairs, applicant-proposing : " << b1.size() << "\n";
    std::cout << "  Blocking pairs, centre-proposing    : " << b2.size() << "\n";
    std::cout << std::fixed << std::setprecision(3)
              << "  Mean applicant preference position, applicant-proposing : "
              << averageApplicantRank(in, ap) << "  (1.000 = everyone got 1st choice)\n"
              << "  Mean applicant preference position, centre-proposing    : "
              << averageApplicantRank(in, cp) << "\n" << std::defaultfloat;

    if (identical) {
        std::cout <<
            "\n  The two runs returned THE SAME allocation. That is not a coincidence and it\n"
            "  is a genuinely useful finding: the applicant-optimal and the centre-optimal\n"
            "  stable matchings are the extreme points of the lattice of stable matchings,\n"
            "  so when they coincide the instance has EXACTLY ONE stable matching. Our\n"
            "  criteria-derived preferences are strongly correlated (nearly every hawker\n"
            "  ranks the high-footfall Maxwell and Old Airport Road first), and correlated\n"
            "  preferences collapse the lattice to a single point.\n"
            "  A data set therefore has to be built deliberately to show the proposer's\n"
            "  advantage - which is what the next exercise does.\n";
    } else {
        std::cout << "\n  The two runs returned DIFFERENT but equally stable allocations.\n";
    }

    std::cout <<
        "\n  --- SMALLEST EXERCISE THAT EXPOSES THE PROPOSER'S ADVANTAGE ---\n"
        "  Three hawkers, three single-stall centres, cyclic preferences:\n"
        "    Kamala  : Adam > Amoy > Zion       Adam : Ng     > Hafiz  > Kamala\n"
        "    Ng      : Amoy > Zion > Adam       Amoy : Hafiz  > Kamala > Ng\n"
        "    Hafiz   : Zion > Adam > Amoy       Zion : Kamala > Ng     > Hafiz\n";
    Instance sm = buildCyclicInstance();
    Matching sap = galeShapleyApplicantProposing(sm);
    Matching scp = galeShapleyCentreProposing(sm);
    for (int a = 0; a < sm.n(); ++a)
        std::cout << "    " << std::left << std::setw(14) << sm.applicants[a].name
                  << " hawker-proposing -> " << std::setw(20)
                  << sm.centres[sap.assignedCentre[a]].name
                  << " centre-proposing -> "
                  << sm.centres[scp.assignedCentre[a]].name << std::right << "\n";
    std::cout << std::fixed << std::setprecision(3)
              << "    Mean preference position: hawker-proposing " << averageApplicantRank(sm, sap)
              << " (every hawker gets its 1st choice)\n"
              << "                              centre-proposing " << averageApplicantRank(sm, scp)
              << " (every hawker gets its LAST choice)\n" << std::defaultfloat
              << "    Both allocations are stable: blocking pairs = "
              << findBlockingPairs(sm, sap).size() << " and "
              << findBlockingPairs(sm, scp).size() << ".\n"
              << "  => the proposing side is never worse off. If hawker welfare is the\n"
              << "     objective, the NEA should run the exercise hawker-proposing.\n";
}

/* ==================================================================================== *
 *  SECTION 8 : TEST SUITE                                                              *
 * ==================================================================================== */

static int g_pass = 0, g_fail = 0;

static void check(const std::string& label, bool ok, const std::string& detail = "") {
    std::cout << (ok ? "    [PASS] " : "    [FAIL] ") << label;
    if (!detail.empty()) std::cout << "  (" << detail << ")";
    std::cout << "\n";
    if (ok) ++g_pass; else ++g_fail;
}

/* Every stable matching of a 3x3 unit-capacity instance, found by brute force, so that
   applicant-optimality can be asserted rather than assumed. */
static std::vector<std::vector<int>> allStableMatchings3(const Instance& in) {
    std::vector<std::vector<int>> out;
    std::vector<int> perm = { 0, 1, 2 };
    do {
        Matching mt;
        mt.assignedCentre = perm;
        mt.hired.assign(3, {});
        for (int a = 0; a < 3; ++a) mt.hired[perm[a]].push_back(a);
        if (findBlockingPairs(in, mt).empty()) out.push_back(perm);
    } while (std::next_permutation(perm.begin(), perm.end()));
    return out;
}

static void runTestSuite() {
    g_pass = g_fail = 0;
    std::cout << "\n==================== TEST SUITE (Question 1) ====================\n";
    std::string why;

    /* ---- TC01 : the default 6-centre / 18-applicant exercise solves correctly ---- */
    std::cout << "\n  TC01  Default NEA exercise: structurally valid allocation\n";
    {
        Instance in = buildDefaultInstance();
        Matching mt = galeShapleyApplicantProposing(in);
        check("no applicant holds 2 stalls, no centre exceeds quota",
              isStructurallyValid(in, mt, why), why);
        check("all 12 stalls filled (complete lists + surplus of applicants)",
              mt.matchedCount() == in.totalStalls(),
              std::to_string(mt.matchedCount()) + " of " + std::to_string(in.totalStalls()));
    }

    /* ---- TC02 : stability of the default exercise ---- */
    std::cout << "\n  TC02  Default NEA exercise: zero blocking pairs\n";
    {
        Instance in = buildDefaultInstance();
        Matching mt = galeShapleyApplicantProposing(in);
        const size_t nb = findBlockingPairs(in, mt).size();
        check("brute-force audit of all 108 pairs finds no instability", nb == 0,
              std::to_string(nb) + " blocking pairs");
    }

    /* ---- TC03 : minimum size demanded by the brief (5 and 5) ---- */
    std::cout << "\n  TC03  Minimum specification: 5 centres x 5 applicants\n";
    {
        Instance in = buildRandomInstance(5, 5, 1, 1, 20260919u);
        Matching mt = galeShapleyApplicantProposing(in);
        check("valid", isStructurallyValid(in, mt, why), why);
        check("stable", findBlockingPairs(in, mt).empty());
        check("every applicant placed (5 applicants, 5 unit-quota centres)",
              mt.matchedCount() == 5, std::to_string(mt.matchedCount()));
    }

    /* ---- TC04 : worst-case contention, every applicant wants the same centre first -- */
    std::cout << "\n  TC04  Identical applicant preference lists (maximum contention)\n";
    {
        Instance in = buildRandomInstance(30, 6, 2, 2, 7u);
        for (int a = 0; a < in.n(); ++a) {
            in.prefA[a].resize(in.m());
            std::iota(in.prefA[a].begin(), in.prefA[a].end(), 0);
        }
        buildRankTables(in);
        Matching mt = galeShapleyApplicantProposing(in);
        check("valid", isStructurallyValid(in, mt, why), why);
        check("stable", findBlockingPairs(in, mt).empty());
        check("all 12 stalls still filled", mt.matchedCount() == in.totalStalls(),
              std::to_string(mt.matchedCount()) + "/" + std::to_string(in.totalStalls()));
    }

    /* ---- TC05 : every centre ranks applicants identically ---- */
    std::cout << "\n  TC05  Identical centre rankings: the globally top applicants win\n";
    {
        Instance in = buildRandomInstance(20, 5, 2, 2, 11u);
        std::vector<int> common(in.n());
        std::iota(common.begin(), common.end(), 0);      /* A0 best, A1 next, ... */
        for (int c = 0; c < in.m(); ++c) in.prefC[c] = common;
        buildRankTables(in);
        Matching mt = galeShapleyApplicantProposing(in);
        check("stable", findBlockingPairs(in, mt).empty());
        bool topKMatched = true;
        for (int a = 0; a < in.totalStalls(); ++a)
            if (mt.assignedCentre[a] == -1) topKMatched = false;
        for (int a = in.totalStalls(); a < in.n(); ++a)
            if (mt.assignedCentre[a] != -1) topKMatched = false;
        check("exactly the top-10 applicants of the common ranking are placed", topKMatched);
    }

    /* ---- TC06 : unit capacities reduce to the classic stable marriage problem ---- */
    std::cout << "\n  TC06  All quotas = 1 agrees with classic 1-to-1 Gale-Shapley\n";
    {
        bool same = true;
        for (unsigned s = 1; s <= 50 && same; ++s) {
            Instance in = buildRandomInstance(12, 12, 1, 1, 500u + s);
            Matching mt = galeShapleyApplicantProposing(in);
            if (mt.assignedCentre != classicGaleShapley(in)) same = false;
        }
        check("capacitated version == classic version on 50 random 12x12 instances", same);
    }

    /* ---- TC07 : the surplus condition from the brief ---- */
    std::cout << "\n  TC07  Surplus of applicants: exactly n - stalls go unplaced\n";
    {
        Instance in = buildDefaultInstance();
        Matching mt = galeShapleyApplicantProposing(in);
        const int unplaced = in.n() - mt.matchedCount();
        check("unplaced count == 18 - 12 == 6", unplaced == in.n() - in.totalStalls(),
              std::to_string(unplaced));
        bool noneBlocks = true;
        for (const BlockingPair& b : findBlockingPairs(in, mt))
            if (mt.assignedCentre[b.applicant] == -1) noneBlocks = false;
        check("no unplaced applicant forms a Type-1 blocking pair", noneBlocks);
    }

    /* ---- TC08 : a single centre must take its q best acceptable applicants ---- */
    std::cout << "\n  TC08  Single centre, quota 4: hires exactly its 4 best-ranked applicants\n";
    {
        Instance in = buildRandomInstance(15, 1, 4, 4, 99u);
        Matching mt = galeShapleyApplicantProposing(in);
        std::vector<int> expect(in.prefC[0].begin(), in.prefC[0].begin() + 4);
        std::vector<int> got = mt.hired[0];
        std::sort(expect.begin(), expect.end());
        std::sort(got.begin(), got.end());
        check("hired set == top 4 of the centre's preference list", expect == got);
        check("stable", findBlockingPairs(in, mt).empty());
    }

    /* ---- TC09 : applicant-optimality, proved against every stable matching ---- */
    std::cout << "\n  TC09  Applicant-optimality on the classic 3x3 instance\n";
    {
        Instance in = buildCyclicInstance();
        Matching mt = galeShapleyApplicantProposing(in);
        const std::vector<std::vector<int>> stables = allStableMatchings3(in);
        check("brute force finds 3 stable matchings for this instance",
              stables.size() == 3, std::to_string(stables.size()) + " found");
        bool optimal = true;
        for (const std::vector<int>& s : stables)
            for (int a = 0; a < 3; ++a)
                if (in.rankA[a][mt.assignedCentre[a]] > in.rankA[a][s[a]]) optimal = false;
        check("GS result gives EVERY applicant its best achievable centre", optimal);
        bool isStable = findBlockingPairs(in, mt).empty();
        check("GS result is itself one of the stable matchings", isStable);
    }

    /* ---- TC10 : the proposing side is never worse off ---- */
    std::cout << "\n  TC10  Centre-proposing variant is stable but worse for applicants\n";
    {
        Instance in = buildDefaultInstance();
        Matching ap = galeShapleyApplicantProposing(in);
        Matching cp = galeShapleyCentreProposing(in);
        check("centre-proposing result is valid", isStructurallyValid(in, cp, why), why);
        check("centre-proposing result is stable", findBlockingPairs(in, cp).empty());
        const double ra = averageApplicantRank(in, ap), rc = averageApplicantRank(in, cp);
        std::ostringstream os;
        os << std::fixed << std::setprecision(3) << "applicant-proposing " << ra
           << " vs centre-proposing " << rc;
        check("applicants do no worse when they propose", ra <= rc + 1e-9, os.str());

        /* On the derived data set the two coincide, which by the lattice theorem means
           the instance has a unique stable matching.  The cyclic instance separates them. */
        Instance cy = buildCyclicInstance();
        Matching cyA = galeShapleyApplicantProposing(cy);
        Matching cyC = galeShapleyCentreProposing(cy);
        const double cra = averageApplicantRank(cy, cyA), crc = averageApplicantRank(cy, cyC);
        std::ostringstream os2;
        os2 << std::fixed << std::setprecision(3) << "hawker-proposing " << cra
            << " vs centre-proposing " << crc;
        check("cyclic instance: hawker-proposing is STRICTLY better for hawkers",
              cra < crc, os2.str());
        check("cyclic instance: the centre-proposing result is still stable",
              findBlockingPairs(cy, cyC).empty());
        check("derived data set has a unique stable matching (the two optima coincide)",
              ap.assignedCentre == cp.assignedCentre);
    }

    /* ---- TC11 : incomplete preference lists ---- */
    std::cout << "\n  TC11  Incomplete preference lists (each applicant shortlists 3 of 8)\n";
    {
        bool allOk = true, noBad = true;
        for (unsigned s = 1; s <= 30; ++s) {
            Instance in = buildRandomInstance(25, 8, 1, 3, 900u + s, 3);
            Matching mt = galeShapleyApplicantProposing(in);
            if (!isStructurallyValid(in, mt, why)) allOk = false;
            if (!findBlockingPairs(in, mt).empty()) noBad = false;
        }
        check("nobody is ever placed at a centre outside their shortlist", allOk, why);
        check("still stable on 30 random shortlisted instances (stalls may stay empty)", noBad);
    }

    /* ---- TC12 : optimised vs reference implementation, then scalability ---- */
    std::cout << "\n  TC12  Optimised O(nm) version vs O(nmq) reference oracle\n";
    {
        bool identical = true;
        for (unsigned s = 1; s <= 200 && identical; ++s) {
            Instance in = buildRandomInstance(40, 9, 1, 6, 3000u + s);
            Matching f = galeShapleyApplicantProposing(in);
            Matching r = galeShapleyNaive(in);
            if (f.assignedCentre != r.assignedCentre) identical = false;
        }
        check("both implementations agree on 200 random instances", identical);

        Instance big = buildRandomInstance(2000, 200, 1, 12, 4242u);
        Matching mt = galeShapleyApplicantProposing(big);
        check("2000 applicants x 200 centres: valid", isStructurallyValid(big, mt, why), why);
        check("2000 applicants x 200 centres: stable", findBlockingPairs(big, mt).empty());
        std::ostringstream os;
        os << std::fixed << std::setprecision(3) << mt.proposals << " proposals in "
           << mt.milliseconds << " ms; n*m = " << big.n() * big.m();
        check("solved well inside the O(n*m) proposal bound", mt.proposals <= 1LL * big.n() * big.m(),
              os.str());
    }

    std::cout << "\n  ------------------------------------------------------------\n";
    std::cout << "  TOTAL: " << g_pass << " passed, " << g_fail << " failed.\n";
    std::cout << "  ------------------------------------------------------------\n";
}

/* ==================================================================================== *
 *  SECTION 9 : SCALABILITY STUDY                                                       *
 * ==================================================================================== */

static void scalabilityStudy() {
    std::cout << "\n================ SCALABILITY : IS IT REALLY O(n*m)? ================\n";
    std::cout << "  m is held at n/10, quotas in [1,10], complete preference lists.\n\n";
    std::cout << std::right << std::setw(8) << "n" << std::setw(7) << "m"
              << std::setw(12) << "n*m" << std::setw(14) << "proposals"
              << std::setw(14) << "proposals/nm" << std::setw(12) << "time(ms)"
              << std::setw(14) << "time/nm(us)" << "\n";
    for (int n = 500; n <= 8000; n *= 2) {
        const int m = n / 10;
        Instance in = buildRandomInstance(n, m, 1, 10, static_cast<unsigned>(n) + 17u);
        Matching mt = galeShapleyApplicantProposing(in);
        const double nm = static_cast<double>(n) * m;
        std::cout << std::right << std::setw(8) << n << std::setw(7) << m
                  << std::setw(12) << static_cast<long long>(nm)
                  << std::setw(14) << mt.proposals
                  << std::setw(14) << std::fixed << std::setprecision(4) << (mt.proposals / nm)
                  << std::setw(12) << std::setprecision(3) << mt.milliseconds
                  << std::setw(14) << std::setprecision(4) << (mt.milliseconds * 1000.0 / nm)
                  << std::defaultfloat << "\n";
    }
    std::cout << "\n  proposals/nm stays bounded by 1: every applicant proposes to every\n"
              << "  centre at most once, which is the O(n*m) worst case of the algorithm.\n";
}

/* ==================================================================================== *
 *  SECTION 10 : CUSTOM INSTANCE ENTRY                                                  *
 * ==================================================================================== */

static bool readInt(const std::string& prompt, int lo, int hi, int& out) {
    std::cout << prompt;
    if (!(std::cin >> out)) { std::cin.clear(); std::cin.ignore(10000, '\n'); return false; }
    if (out < lo || out > hi) { std::cout << "    value out of range\n"; return false; }
    return true;
}

static void customInstance() {
    std::cout << "\n================ CUSTOM INSTANCE ================\n";
    int m = 0, n = 0;
    if (!readInt("  Number of hawker centres  (1-20) : ", 1, 20, m)) return;
    if (!readInt("  Number of applicants      (1-50) : ", 1, 50, n)) return;

    Instance in;
    in.centres.resize(m);
    in.applicants.resize(n);
    for (int c = 0; c < m; ++c) {
        in.centres[c].name = "C" + std::to_string(c);
        if (!readInt("  Vacant stalls at C" + std::to_string(c) + " (0-" +
                     std::to_string(n) + ") : ", 0, n, in.centres[c].quota)) return;
    }
    for (int a = 0; a < n; ++a) in.applicants[a].name = aid(a);

    in.prefA.assign(n, {});
    in.prefC.assign(m, {});
    std::cout << "\n  Enter each applicant's ranking as " << m
              << " distinct centre indices 0.." << m - 1 << " (best first).\n";
    for (int a = 0; a < n; ++a) {
        std::cout << "  " << aid(a) << " : ";
        std::vector<char> seen(m, 0);
        for (int i = 0; i < m; ++i) {
            int v;
            if (!(std::cin >> v) || v < 0 || v >= m || seen[v]) {
                std::cin.clear(); std::cin.ignore(10000, '\n');
                std::cout << "    invalid ranking, aborting\n"; return;
            }
            seen[v] = 1;
            in.prefA[a].push_back(v);
        }
    }
    std::cout << "\n  Enter each centre's ranking as " << n
              << " distinct applicant indices 0.." << n - 1 << " (best first).\n";
    for (int c = 0; c < m; ++c) {
        std::cout << "  C" << c << " : ";
        std::vector<char> seen(n, 0);
        for (int i = 0; i < n; ++i) {
            int v;
            if (!(std::cin >> v) || v < 0 || v >= n || seen[v]) {
                std::cin.clear(); std::cin.ignore(10000, '\n');
                std::cout << "    invalid ranking, aborting\n"; return;
            }
            seen[v] = 1;
            in.prefC[c].push_back(v);
        }
    }
    buildRankTables(in);

    Matching mt = galeShapleyApplicantProposing(in, true, 200);
    printMatching(in, mt, "CUSTOM INSTANCE ALLOCATION");
    printStabilityAudit(in, mt);
}

/* ==================================================================================== *
 *  SECTION 11 : MENU                                                                   *
 * ==================================================================================== */

static void runEverything() {
    demoDefaultScenario();
    demoSolveDefault();
    demoProposerAdvantage();
    runTestSuite();
    scalabilityStudy();
}

static void banner() {
    std::cout <<
        "\n============================================================================\n"
        "  CSD3130/CSD3131 Assignment Part 1 - QUESTION 1\n"
        "  Stable Matching for the NEA Hawker Stall Allocation Exercise\n"
        "  Gale-Shapley generalised to centre capacities (Hospital-Residents)\n"
        "============================================================================\n";
}

int main(int argc, char** argv) {
    banner();

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--all") { runEverything(); return g_fail == 0 ? 0 : 1; }
    }

    for (;;) {
        std::cout <<
            "\n  MENU\n"
            "   1. Show the hawker-centre exercise and the derived preference lists\n"
            "   2. Solve it with Gale-Shapley (trace + allocation + stability audit)\n"
            "   3. Compare applicant-proposing against centre-proposing\n"
            "   4. Run the full test suite (12 test cases)\n"
            "   5. Scalability and timing study\n"
            "   6. Enter and solve your own instance\n"
            "   7. Run everything (1-5)\n"
            "   0. Exit\n"
            "  Choice: ";
        int choice = -1;
        if (!(std::cin >> choice)) break;
        switch (choice) {
            case 1: demoDefaultScenario();  break;
            case 2: demoSolveDefault();     break;
            case 3: demoProposerAdvantage();break;
            case 4: runTestSuite();         break;
            case 5: scalabilityStudy();     break;
            case 6: customInstance();       break;
            case 7: runEverything();        break;
            case 0: std::cout << "  Bye.\n"; return 0;
            default: std::cout << "  Unknown option.\n";
        }
    }
    return 0;
}
