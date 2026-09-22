/* =====================================================================================
 *  CSD3130 / CSD3131  Algorithm Analysis - Assignment (Part 1)
 *  QUESTION 2 : Orders of Growth, and a Recurrence of the form T(n) = 3T(n/3) + lg n
 *
 *  Student(s) : [STUDENT NAME - STUDENT ID]
 *  Team       : Txx
 *  File       : growth_and_recurrence.cpp     (C++17, single file, console only)
 *
 *  PART (a)  Characteristics of the seven orders of growth, demonstrated by two
 *            algorithms that do NOT appear in the lectures or tutorials:
 *
 *      LINEARITHMIC  O(n lg n)  "Longest run-up in COE premiums"
 *                               Longest Increasing Subsequence via patience piles
 *                               and binary search.  Cross-checked against the
 *                               classical O(n^2) dynamic-programming formulation.
 *
 *      FACTORIAL     O(n * n!)  "Shortest one-day tour of Singapore attractions"
 *                               Exact Travelling Salesman by enumerating all n!
 *                               permutations.  Cross-checked against the Held-Karp
 *                               O(n^2 * 2^n) dynamic program.
 *
 *  PART (b)  An algorithm whose running time satisfies EXACTLY  T(n) = 3T(n/3) + lg n:
 *
 *      "LTA distance-fare band audit of an MRT line".  The station list carries the
 *      cumulative distance from the terminus, so it is sorted by construction.  Each
 *      recursive call splits its stretch of the line into three equal thirds (3 sub-
 *      problems of size n/3) and then performs ONE binary search over its own stretch
 *      to find the last station reachable inside a single fare band - a combine step
 *      that costs exactly floor(lg n) + 1 probes.
 *
 *  BUILD (Developer Command Prompt for VS 2022)
 *        cl /nologo /std:c++17 /EHsc /O2 /W4 growth_and_recurrence.cpp /Fe:q2.exe
 *  BUILD (g++)
 *        g++ -std=c++17 -O2 -Wall -Wextra -o q2 growth_and_recurrence.cpp
 *  RUN   q2.exe              interactive menu
 *        q2.exe --all        run every demonstration and every test, then exit
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
 *  SHARED HELPERS                                                                      *
 * ==================================================================================== */

static double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(high_resolution_clock::now().time_since_epoch()).count();
}

/* Print a magnitude readably: plain when small, scientific once it stops fitting. */
static std::string mag(long double v) {
    std::ostringstream os;
    if (v < 1e7L) os << std::fixed << std::setprecision(0) << v;
    else          os << std::scientific << std::setprecision(2) << static_cast<double>(v);
    return os.str();
}

static int g_pass = 0, g_fail = 0;
static void check(const std::string& label, bool ok, const std::string& detail = "") {
    std::cout << (ok ? "    [PASS] " : "    [FAIL] ") << label;
    if (!detail.empty()) std::cout << "  (" << detail << ")";
    std::cout << "\n";
    if (ok) ++g_pass; else ++g_fail;
}

/* ==================================================================================== *
 *  PART (a) SECTION 1 : THE SEVEN ORDERS OF GROWTH, SIDE BY SIDE                       *
 * ==================================================================================== */

static void growthTable() {
    std::cout << "\n============ THE SEVEN ORDERS OF GROWTH ============\n"
              << "  Basic operations performed by an algorithm of each class, for input size n.\n"
              << "  This is the whole argument for asymptotic analysis in one table: the classes\n"
              << "  separate so violently that constant factors stop mattering.\n\n";
    std::cout << std::right
              << std::setw(6)  << "n"
              << std::setw(10) << "O(1)"
              << std::setw(12) << "O(lg n)"
              << std::setw(12) << "O(n)"
              << std::setw(14) << "O(n lg n)"
              << std::setw(14) << "O(n^2)"
              << std::setw(14) << "O(2^n)"
              << std::setw(14) << "O(n!)" << "\n";
    const int sizes[] = { 1, 2, 4, 8, 16, 32, 64, 128 };
    for (int n : sizes) {
        const long double L  = (n > 1) ? std::log2(static_cast<long double>(n)) : 0.0L;
        long double fact = 1.0L, pow2 = 1.0L;
        for (int i = 2; i <= n; ++i) fact *= i;
        for (int i = 0; i < n; ++i)  pow2 *= 2.0L;
        std::cout << std::setw(6)  << n
                  << std::setw(10) << 1
                  << std::setw(12) << mag(L)
                  << std::setw(12) << mag(static_cast<long double>(n))
                  << std::setw(14) << mag(n * L)
                  << std::setw(14) << mag(static_cast<long double>(n) * n)
                  << std::setw(14) << mag(pow2)
                  << std::setw(14) << mag(fact) << "\n";
    }
    std::cout <<
        "\n  Reading the table:\n"
        "    constant      doubling n changes nothing at all.\n"
        "    logarithmic   doubling n adds ONE step; 128 items cost 7 probes, not 128.\n"
        "    linear        doubling n doubles the work - the natural cost of reading input.\n"
        "    linearithmic  doubling n slightly more than doubles it (+n extra); this is the\n"
        "                  ceiling for any sorting algorithm that only compares keys.\n"
        "    polynomial    doubling n multiplies the work by 2^k for n^k; still tractable.\n"
        "    exponential   ADDING ONE to n doubles the work; tractable only for tiny n.\n"
        "    factorial     adding one to n multiplies the work by n; 20! already exceeds the\n"
        "                  number of seconds since the Big Bang at one operation per ns.\n";
}

/* ==================================================================================== *
 *  PART (a) SECTION 2 : LINEARITHMIC - LONGEST INCREASING SUBSEQUENCE, O(n lg n)       *
 * ==================================================================================== */

/*  "Longest run-up in COE premiums"
 *
 *  Given the monthly Certificate of Entitlement quota premium for Category A, an
 *  analyst wants the longest chain of months (not necessarily consecutive) over which
 *  the premium kept rising.  That is exactly the Longest Increasing Subsequence.
 *
 *  METHOD (patience piles).  Sweep left to right holding tails[k] = the smallest value
 *  that can end an increasing subsequence of length k+1.  tails[] is always sorted, so
 *  each new premium is placed by BINARY SEARCH: it either extends the array (a longer
 *  chain) or overwrites the first tail that is not smaller than it (a better chain of
 *  the same length).  n insertions, each costing lg n probes  =>  Theta(n lg n).
 *
 *  Space O(n) for tails[], the tail indices and the predecessor links used to rebuild
 *  the actual subsequence.
 */
static int lisLinearithmic(const std::vector<int>& v,
                           std::vector<int>* reconstructed,
                           long long* probes) {
    const int n = static_cast<int>(v.size());
    if (n == 0) { if (reconstructed) reconstructed->clear(); return 0; }

    std::vector<int> tailsVal, tailsIdx, parent(n, -1);
    tailsVal.reserve(n);
    tailsIdx.reserve(n);

    for (int i = 0; i < n; ++i) {
        /* lower_bound written out so that every probe can be counted. */
        int lo = 0, hi = static_cast<int>(tailsVal.size());
        while (lo < hi) {
            const int mid = lo + (hi - lo) / 2;
            if (probes) ++(*probes);
            if (tailsVal[mid] < v[i]) lo = mid + 1;
            else                      hi = mid;
        }
        const int pos = lo;
        if (pos > 0) parent[i] = tailsIdx[pos - 1];
        if (pos == static_cast<int>(tailsVal.size())) {
            tailsVal.push_back(v[i]);
            tailsIdx.push_back(i);
        } else {
            tailsVal[pos] = v[i];
            tailsIdx[pos] = i;
        }
    }

    if (reconstructed) {
        reconstructed->clear();
        for (int i = tailsIdx.back(); i != -1; i = parent[i]) reconstructed->push_back(v[i]);
        std::reverse(reconstructed->begin(), reconstructed->end());
    }
    return static_cast<int>(tailsVal.size());
}

/*  Classical O(n^2) dynamic programme.  Used only as a correctness oracle and as the
 *  comparative baseline in the report. */
static int lisQuadratic(const std::vector<int>& v, long long* probes) {
    const int n = static_cast<int>(v.size());
    if (n == 0) return 0;
    std::vector<int> best(n, 1);
    int ans = 1;
    for (int i = 1; i < n; ++i) {
        for (int j = 0; j < i; ++j) {
            if (probes) ++(*probes);
            if (v[j] < v[i] && best[j] + 1 > best[i]) best[i] = best[j] + 1;
        }
        ans = std::max(ans, best[i]);
    }
    return ans;
}

/* 24 months of illustrative Category A COE quota premiums, in SGD. */
static std::vector<int> coePremiums() {
    return { 78000, 82000, 76000, 85000, 91000, 88000, 94000, 90000,
             97000, 93000,102000,106000, 99000,104000,110000,107000,
            113000,108000,115000,119000,112000,121000,118000,125000 };
}

static void lisDemo() {
    std::cout << "\n===== LINEARITHMIC O(n lg n) : LONGEST RUN-UP IN COE PREMIUMS =====\n";
    const std::vector<int> v = coePremiums();
    std::cout << "\n  24 months of Category A quota premiums (SGD, illustrative):\n   ";
    for (size_t i = 0; i < v.size(); ++i) {
        std::cout << std::setw(7) << v[i];
        if ((i + 1) % 8 == 0) std::cout << "\n   ";
    }
    std::vector<int> seq;
    long long probesFast = 0, probesSlow = 0;
    const int lenFast = lisLinearithmic(v, &seq, &probesFast);
    const int lenSlow = lisQuadratic(v, &probesSlow);

    std::cout << "\n  Longest non-consecutive rising chain has length " << lenFast << ":\n   ";
    for (size_t i = 0; i < seq.size(); ++i) {
        std::cout << std::setw(7) << seq[i];
        if ((i + 1) % 8 == 0) std::cout << "\n   ";
    }
    std::cout << "\n\n  Cross-check with the O(n^2) dynamic programme: length " << lenSlow
              << (lenFast == lenSlow ? "  (agrees)" : "  (DISAGREES)") << "\n";
    std::cout << "  Binary-search probes used by the O(n lg n) method : " << probesFast << "\n";
    std::cout << "  Inner-loop tests used by the O(n^2) method        : " << probesSlow << "\n";
    std::cout << "  n = " << v.size() << ", n lg n = "
              << static_cast<long long>(v.size() * std::log2(static_cast<double>(v.size())))
              << ", n^2 = " << v.size() * v.size() << "\n";
}

static void lisScaling() {
    std::cout << "\n===== EVIDENCE THAT THE LIS ALGORITHM IS LINEARITHMIC =====\n"
              << "  If the cost were linear, probes/n would be constant.\n"
              << "  If it is linearithmic, probes/n grows like lg n while probes/(n lg n) settles.\n\n";
    std::cout << std::right << std::setw(10) << "n" << std::setw(14) << "probes"
              << std::setw(12) << "probes/n" << std::setw(16) << "probes/(n lg n)"
              << std::setw(12) << "time(ms)" << std::setw(18) << "time/(n lg n) ns" << "\n";
    std::mt19937 rng(20260919u);
    for (int n = 10000; n <= 1280000; n *= 2) {
        std::vector<int> v(n);
        std::uniform_int_distribution<int> dist(0, 1000000000);
        for (int& x : v) x = dist(rng);
        long long probes = 0;
        const double t0 = nowMs();
        lisLinearithmic(v, nullptr, &probes);
        const double ms = nowMs() - t0;
        const double nlgn = n * std::log2(static_cast<double>(n));
        std::cout << std::right << std::setw(10) << n << std::setw(14) << probes
                  << std::setw(12) << std::fixed << std::setprecision(3)
                  << (static_cast<double>(probes) / n)
                  << std::setw(16) << std::setprecision(4) << (probes / nlgn)
                  << std::setw(12) << std::setprecision(2) << ms
                  << std::setw(18) << std::setprecision(4) << (ms * 1e6 / nlgn)
                  << std::defaultfloat << "\n";
    }
    std::cout << "\n  probes/n climbs steadily (it is tracking lg n) while probes/(n lg n)\n"
              << "  flattens out: the cost is Theta(n lg n), not Theta(n).\n";

    std::cout << "\n  --- COMPARATIVE STUDY : the same answer at O(n^2) ---\n";
    std::cout << std::right << std::setw(10) << "n" << std::setw(16) << "O(n lg n) ms"
              << std::setw(16) << "O(n^2) ms" << std::setw(14) << "speed-up" << "\n";
    for (int n = 2000; n <= 32000; n *= 2) {
        std::vector<int> v(n);
        std::uniform_int_distribution<int> dist(0, 1000000000);
        for (int& x : v) x = dist(rng);
        double t0 = nowMs(); lisLinearithmic(v, nullptr, nullptr); const double fast = nowMs() - t0;
        t0 = nowMs();        lisQuadratic(v, nullptr);             const double slow = nowMs() - t0;
        std::cout << std::right << std::setw(10) << n
                  << std::setw(16) << std::fixed << std::setprecision(2) << fast
                  << std::setw(16) << slow
                  << std::setw(13) << std::setprecision(1) << (fast > 0 ? slow / fast : 0.0)
                  << "x" << std::defaultfloat << "\n";
    }
}

/* ==================================================================================== *
 *  PART (a) SECTION 3 : FACTORIAL - EXACT TRAVELLING SALESMAN, O(n * n!)               *
 * ==================================================================================== */

struct Attraction { std::string name; double x, y; };

/* Approximate relative positions on a local kilometre grid of central Singapore. */
static std::vector<Attraction> attractions() {
    return {
        { "Merlion Park",        11.0, 12.6 },
        { "Marina Bay Sands",    12.0, 12.4 },
        { "Gardens by the Bay",  12.4, 12.2 },
        { "Esplanade",           11.2, 12.9 },
        { "Singapore Flyer",     11.8, 12.8 },
        { "Clarke Quay",         10.2, 12.9 },
        { "Chinatown",           10.1, 12.3 },
        { "Bugis",               11.0, 13.4 },
        { "Kampong Glam",        11.2, 13.6 },
        { "Little India",        10.6, 13.8 },
        { "Orchard Road",         9.4, 13.6 }
    };
}

static std::vector<std::vector<double>> distanceMatrix(const std::vector<Attraction>& a) {
    const int n = static_cast<int>(a.size());
    std::vector<std::vector<double>> d(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            d[i][j] = std::sqrt((a[i].x - a[j].x) * (a[i].x - a[j].x) +
                                (a[i].y - a[j].y) * (a[i].y - a[j].y));
    return d;
}

/*  Exact TSP by exhaustive enumeration.
 *
 *  Every ordering of the n attractions is a candidate closed tour, so the algorithm
 *  generates all n! permutations and adds up n edge lengths for each one:
 *        T(n) = Theta(n * n!)
 *  Adding a single attraction multiplies the work by roughly n - the defining
 *  behaviour of factorial growth, and the reason exact TSP is hopeless beyond ~12
 *  cities by this method.
 */
static double tspBruteForce(const std::vector<std::vector<double>>& d,
                            std::vector<int>* bestTour,
                            long long* toursEvaluated) {
    const int n = static_cast<int>(d.size());
    std::vector<int> perm(n);
    std::iota(perm.begin(), perm.end(), 0);
    double best = std::numeric_limits<double>::infinity();
    long long tours = 0;
    do {
        double len = 0.0;
        for (int i = 0; i + 1 < n; ++i) len += d[perm[i]][perm[i + 1]];
        len += d[perm[n - 1]][perm[0]];              /* close the loop */
        ++tours;
        if (len < best) { best = len; if (bestTour) *bestTour = perm; }
    } while (std::next_permutation(perm.begin(), perm.end()));
    if (toursEvaluated) *toursEvaluated = tours;
    return best;
}

/*  Held-Karp exact dynamic programme, O(n^2 * 2^n) time and O(n * 2^n) space.
 *  Still exponential, but it turns an impossible problem into an easy one for the
 *  sizes that matter here - the comparative study in the report. */
static double tspHeldKarp(const std::vector<std::vector<double>>& d) {
    const int n = static_cast<int>(d.size());
    if (n <= 1) return 0.0;
    const int full = 1 << n;
    const double INF = std::numeric_limits<double>::infinity();
    std::vector<std::vector<double>> dp(full, std::vector<double>(n, INF));
    dp[1][0] = 0.0;                                   /* start at city 0 */
    for (int mask = 1; mask < full; ++mask) {
        if (!(mask & 1)) continue;
        for (int last = 0; last < n; ++last) {
            if (!(mask & (1 << last)) || dp[mask][last] == INF) continue;
            for (int nxt = 0; nxt < n; ++nxt) {
                if (mask & (1 << nxt)) continue;
                const int nm = mask | (1 << nxt);
                const double cand = dp[mask][last] + d[last][nxt];
                if (cand < dp[nm][nxt]) dp[nm][nxt] = cand;
            }
        }
    }
    double best = INF;
    for (int last = 1; last < n; ++last)
        if (dp[full - 1][last] != INF) best = std::min(best, dp[full - 1][last] + d[last][0]);
    return (n == 1) ? 0.0 : best;
}

static void tspDemo() {
    std::cout << "\n===== FACTORIAL O(n * n!) : SHORTEST ONE-DAY WALKING TOUR =====\n";
    std::vector<Attraction> all = attractions();
    all.resize(9);                                    /* 9! = 362880 tours */
    const std::vector<std::vector<double>> d = distanceMatrix(all);

    std::cout << "\n  Attractions (" << all.size() << "):\n";
    for (size_t i = 0; i < all.size(); ++i)
        std::cout << "    " << i << "  " << all[i].name << "\n";

    std::vector<int> tour;
    long long tours = 0;
    const double t0 = nowMs();
    const double best = tspBruteForce(d, &tour, &tours);
    const double ms = nowMs() - t0;

    std::cout << "\n  Tours examined : " << tours << "   (9! = 362880)\n";
    std::cout << "  Optimal closed tour length : " << std::fixed << std::setprecision(4)
              << best << " km" << std::defaultfloat << "\n  Route: ";
    for (size_t i = 0; i < tour.size(); ++i)
        std::cout << all[tour[i]].name << (i + 1 < tour.size() ? " -> " : "");
    std::cout << " -> " << all[tour[0]].name << "\n";
    std::cout << "  Time: " << std::fixed << std::setprecision(2) << ms << " ms"
              << std::defaultfloat << "\n";
    std::cout << "  Held-Karp O(n^2 2^n) cross-check: " << std::fixed << std::setprecision(4)
              << tspHeldKarp(d) << " km" << std::defaultfloat << "\n";
}

static void tspScaling() {
    std::cout << "\n===== EVIDENCE THAT BRUTE-FORCE TSP IS FACTORIAL =====\n"
              << "  Factorial growth has a signature: t(n)/t(n-1) tends to n itself.\n\n";
    std::cout << std::right << std::setw(5) << "n" << std::setw(14) << "n!"
              << std::setw(14) << "tours" << std::setw(14) << "time(ms)"
              << std::setw(16) << "t(n)/t(n-1)" << std::setw(10) << "vs n" << "\n";
    std::vector<Attraction> all = attractions();
    double prev = 0.0;
    for (int n = 5; n <= 11; ++n) {
        std::vector<Attraction> sub(all.begin(), all.begin() + n);
        const std::vector<std::vector<double>> d = distanceMatrix(sub);
        long long tours = 0;
        const double t0 = nowMs();
        tspBruteForce(d, nullptr, &tours);
        const double ms = nowMs() - t0;
        long double fact = 1.0L;
        for (int i = 2; i <= n; ++i) fact *= i;
        std::cout << std::right << std::setw(5) << n << std::setw(14) << mag(fact)
                  << std::setw(14) << tours
                  << std::setw(14) << std::fixed << std::setprecision(2) << ms;
        if (prev > 0.0) std::cout << std::setw(16) << std::setprecision(2) << (ms / prev);
        else            std::cout << std::setw(16) << "-";
        std::cout << std::setw(10) << n << std::defaultfloat << "\n";
        prev = ms;
    }
    std::cout << "\n  The ratio column tracks n, so each extra attraction multiplies the work\n"
              << "  by about n. Extrapolating, n = 20 would need roughly 2.4e18 tours: at one\n"
              << "  billion tours per second that is about 77 years.\n";

    std::cout << "\n  --- COMPARATIVE STUDY : Held-Karp O(n^2 2^n) on the same data ---\n";
    std::cout << std::right << std::setw(5) << "n" << std::setw(18) << "brute force(ms)"
              << std::setw(18) << "Held-Karp(ms)" << std::setw(16) << "same answer?" << "\n";
    for (int n = 8; n <= 11; ++n) {
        std::vector<Attraction> sub(all.begin(), all.begin() + n);
        const std::vector<std::vector<double>> d = distanceMatrix(sub);
        double t0 = nowMs(); const double bf = tspBruteForce(d, nullptr, nullptr);
        const double bfms = nowMs() - t0;
        t0 = nowMs();        const double hk = tspHeldKarp(d);
        const double hkms = nowMs() - t0;
        std::cout << std::right << std::setw(5) << n
                  << std::setw(18) << std::fixed << std::setprecision(2) << bfms
                  << std::setw(18) << hkms
                  << std::setw(16) << (std::fabs(bf - hk) < 1e-9 ? "yes" : "NO")
                  << std::defaultfloat << "\n";
    }
    std::cout << "\n  Held-Karp is still exponential, but 2^n grows far more slowly than n!,\n"
              << "  which is why it remains usable where enumeration has already collapsed.\n";
}

/* ==================================================================================== *
 *  PART (b) : T(n) = 3T(n/3) + lg n   -   LTA DISTANCE-FARE BAND AUDIT                 *
 * ==================================================================================== */

/*  BACKGROUND
 *  Singapore bus and train fares are charged by total distance travelled, in bands.
 *  Planners auditing a line ask, for every stretch of it: starting from the first
 *  station of the stretch, how far along can a commuter ride before crossing out of
 *  one fare band?  A stretch with very few stations inside the band is a stretch where
 *  short hops become disproportionately expensive, and is flagged for review.
 *
 *  WHY THE RECURRENCE COMES OUT EXACTLY RIGHT
 *  The station table stores the CUMULATIVE distance from the terminus, so it is sorted
 *  by construction - no preprocessing, no sorting cost.  Each call therefore:
 *      - splits its stretch into three equal thirds and audits each   -> 3 * T(n/3)
 *      - runs ONE binary search over its own stretch of size n to find
 *        the last station within BAND_CAP km of the stretch's start   -> lg n probes
 *      - combines three sub-results in constant time                  -> O(1)
 *  giving      T(n) = 3T(n/3) + lg n      node for node, not merely asymptotically.
 *
 *  Master method: a = 3, b = 3, n^(log_b a) = n^1 = n; f(n) = lg n = O(n^(1-eps)),
 *  so Case 1 applies and T(n) = Theta(n).
 */

static const double BAND_CAP_KM = 10.0;      /* illustrative single-fare-band distance */

struct AuditStats {
    long long probes = 0;                    /* binary-search probes: the lg n combine */
    long long nodes  = 0;
    std::vector<long long> nodesAtLevel, probesAtLevel;
    void note(int depth, long long p) {
        if (static_cast<int>(nodesAtLevel.size()) <= depth) {
            nodesAtLevel.resize(depth + 1, 0);
            probesAtLevel.resize(depth + 1, 0);
        }
        ++nodesAtLevel[depth];
        probesAtLevel[depth] += p;
        ++nodes;
        probes += p;
    }
};

struct AuditResult {
    int       minWithin = std::numeric_limits<int>::max();
    int       maxWithin = 0;
    long long totalWithin = 0;
    int       worstStart = -1;               /* index of the stretch start that was worst */
};

static AuditResult mergeAudit(const AuditResult& a, const AuditResult& b) {
    AuditResult r;
    r.minWithin = std::min(a.minWithin, b.minWithin);
    r.maxWithin = std::max(a.maxWithin, b.maxWithin);
    r.totalWithin = a.totalWithin + b.totalWithin;
    r.worstStart = (a.minWithin <= b.minWithin) ? a.worstStart : b.worstStart;
    return r;
}

/*  The combine step: the last index j in d[lo..hi] with d[j] <= limit.
 *
 *  This is a UNIFORM (binary-lifting) search rather than the textbook lo/hi form.
 *  Both are Theta(lg n), but the textbook form performs floor(lg s)+1 or floor(lg s)
 *  probes depending on which way the data sends it, whereas this form always performs
 *  exactly floor(lg s) + 1 probes whatever the data contains.  A data-independent
 *  combine cost is what lets the measured running time match T(n) = 3T(n/3) + lg n
 *  exactly rather than merely on average - see test case TC09.
 *
 *  Correctness: d[lo] always satisfies the predicate (d[lo] - d[lo] = 0 <= cap), and
 *  step starts at the largest power of two <= s, so the reachable span
 *  step + step/2 + ... + 1 = 2*step - 1 >= s - 1 covers every index up to hi.
 */
static int lastWithinLimit(const std::vector<double>& d, int lo, int hi,
                           double limit, long long& probes) {
    const int s = hi - lo + 1;
    int step = 1;
    while ((step << 1) <= s) step <<= 1;          /* largest power of two <= s */
    int pos = lo;
    for (; step >= 1; step >>= 1) {
        ++probes;                                 /* exactly floor(lg s) + 1 probes */
        const int cand = pos + step;
        if (cand <= hi && d[cand] <= limit) pos = cand;
    }
    return pos;
}

/*  THE ALGORITHM.  T(n) = 3T(n/3) + lg n. */
static AuditResult fareAudit(const std::vector<double>& d, int lo, int hi,
                             int depth, AuditStats& st) {
    const int s = hi - lo + 1;

    if (s <= 3) {                                     /* base case: constant work */
        st.note(depth, 1);
        int within = 1;
        for (int i = lo + 1; i <= hi; ++i)
            if (d[i] - d[lo] <= BAND_CAP_KM) ++within; else break;
        AuditResult r;
        r.minWithin = r.maxWithin = within;
        r.totalWithin = within;
        r.worstStart = lo;
        return r;
    }

    const int third = s / 3;
    const int m1 = lo + third;
    const int m2 = lo + 2 * third;

    AuditResult r1 = fareAudit(d, lo, m1 - 1, depth + 1, st);   /* T(n/3) */
    AuditResult r2 = fareAudit(d, m1, m2 - 1, depth + 1, st);   /* T(n/3) */
    AuditResult r3 = fareAudit(d, m2, hi,     depth + 1, st);   /* T(n/3) */

    long long p = 0;
    const int j = lastWithinLimit(d, lo, hi, d[lo] + BAND_CAP_KM, p);  /* lg n probes */
    st.note(depth, p);

    AuditResult self;
    const int within = j - lo + 1;
    self.minWithin = self.maxWithin = within;
    self.totalWithin = within;
    self.worstStart = lo;

    return mergeAudit(mergeAudit(r1, r2), mergeAudit(r3, self));       /* O(1) */
}

/*  Reference implementation: identical recursion and identical results, but the
 *  combine is an exhaustive LINEAR scan of the whole stretch instead of a binary
 *  search - every station in the stretch is inspected, with no early exit, so the
 *  combine costs exactly s.  Its recurrence is therefore T(n) = 3T(n/3) + n, which
 *  is Master case 2 => Theta(n lg n).  It serves as both a correctness oracle and
 *  the comparative baseline for the report. */
static AuditResult fareAuditLinearCombine(const std::vector<double>& d, int lo, int hi,
                                          long long& work) {
    const int s = hi - lo + 1;
    if (s <= 3) {
        work += s;
        int within = 1;
        for (int i = lo + 1; i <= hi; ++i)
            if (d[i] - d[lo] <= BAND_CAP_KM) ++within; else break;
        AuditResult r;
        r.minWithin = r.maxWithin = within;
        r.totalWithin = within;
        r.worstStart = lo;
        return r;
    }
    const int third = s / 3, m1 = lo + third, m2 = lo + 2 * third;
    AuditResult r1 = fareAuditLinearCombine(d, lo, m1 - 1, work);
    AuditResult r2 = fareAuditLinearCombine(d, m1, m2 - 1, work);
    AuditResult r3 = fareAuditLinearCombine(d, m2, hi, work);
    int j = lo;
    for (int i = lo; i <= hi; ++i) { ++work; if (d[i] <= d[lo] + BAND_CAP_KM) j = i; }
    AuditResult self;
    const int within = j - lo + 1;
    self.minWithin = self.maxWithin = within;
    self.totalWithin = within;
    self.worstStart = lo;
    return mergeAudit(mergeAudit(r1, r2), mergeAudit(r3, self));
}

/*  The recurrence evaluated symbolically, with the SAME splitting rule and the same
 *  cost model (floor(lg s) + 1 probes per internal node, 1 per base case).  If the
 *  instrumented program agrees with this for every n, the implementation provably
 *  realises T(n) = 3T(n/3) + lg n. */
static long long recurrenceValue(int s) {
    if (s <= 3) return 1;
    const int third = s / 3;
    const int s1 = third, s2 = third, s3 = s - 2 * third;
    const long long combine = static_cast<long long>(std::floor(std::log2(static_cast<double>(s)))) + 1;
    return recurrenceValue(s1) + recurrenceValue(s2) + recurrenceValue(s3) + combine;
}

/* Twenty-seven North-South Line stations with illustrative inter-station distances. */
static void northSouthLine(std::vector<std::string>& names, std::vector<double>& cumKm) {
    const char* st[] = {
        "Jurong East","Bukit Batok","Bukit Gombak","Choa Chu Kang","Yew Tee","Kranji",
        "Marsiling","Woodlands","Admiralty","Sembawang","Canberra","Yishun","Khatib",
        "Yio Chu Kang","Ang Mo Kio","Bishan","Braddell","Toa Payoh","Novena","Newton",
        "Orchard","Somerset","Dhoby Ghaut","City Hall","Raffles Place","Marina Bay",
        "Marina South Pier" };
    const double gap[] = {
        0.0, 1.9, 1.3, 2.4, 2.0, 2.6, 1.5, 1.3, 1.9, 2.1, 1.7, 2.0, 1.2,
        2.3, 2.0, 1.8, 1.1, 1.2, 1.4, 1.3, 1.1, 0.7, 0.9, 1.0, 0.7, 1.0, 1.1 };
    names.assign(std::begin(st), std::end(st));
    cumKm.assign(names.size(), 0.0);
    for (size_t i = 1; i < names.size(); ++i) cumKm[i] = cumKm[i - 1] + gap[i];
}

static void fareAuditDemo() {
    std::cout << "\n===== PART (b) : LTA DISTANCE-FARE BAND AUDIT, T(n) = 3T(n/3) + lg n =====\n";
    std::vector<std::string> names;
    std::vector<double> cum;
    northSouthLine(names, cum);
    const int n = static_cast<int>(cum.size());

    std::cout << "\n  North-South Line, cumulative distance from Jurong East (illustrative):\n";
    for (int i = 0; i < n; ++i) {
        std::cout << "    " << std::setw(2) << i << "  " << std::left << std::setw(20) << names[i]
                  << std::right << std::fixed << std::setprecision(1) << std::setw(6)
                  << cum[i] << " km" << std::defaultfloat << "\n";
    }
    std::cout << "\n  Fare band cap under audit: " << std::fixed << std::setprecision(1)
              << BAND_CAP_KM << " km" << std::defaultfloat << std::setprecision(6) << "\n";

    AuditStats st;
    const double t0 = nowMs();
    const AuditResult r = fareAudit(cum, 0, n - 1, 0, st);
    const double ms = nowMs() - t0;

    std::cout << "\n  RESULT\n"
              << "    stretches audited (recursion nodes) : " << st.nodes << "\n"
              << "    fewest stations inside one band     : " << r.minWithin
              << "  (stretch beginning at " << names[r.worstStart] << ")\n"
              << "    most stations inside one band       : " << r.maxWithin << "\n"
              << "    total stations-within-band counted  : " << r.totalWithin << "\n"
              << "    binary-search probes (the lg n term): " << st.probes << "\n"
              << "    time: " << std::fixed << std::setprecision(4) << ms << " ms"
              << std::defaultfloat << "\n";

    std::cout << "\n  THE RECURSION TREE, MEASURED\n"
              << "    Each level holds 3x as many stretches as the one above, and each\n"
              << "    stretch is 1/3 the size, so the per-level cost barely falls while the\n"
              << "    node count explodes - which is why the leaves dominate and T(n)=Theta(n).\n\n";
    std::cout << std::right << std::setw(8) << "level" << std::setw(10) << "nodes"
              << std::setw(14) << "3^level" << std::setw(14) << "probes"
              << std::setw(18) << "probes/node" << "\n";
    for (size_t i = 0; i < st.nodesAtLevel.size(); ++i) {
        long long p3 = 1;
        for (size_t k = 0; k < i; ++k) p3 *= 3;
        std::cout << std::right << std::setw(8) << i << std::setw(10) << st.nodesAtLevel[i]
                  << std::setw(14) << p3 << std::setw(14) << st.probesAtLevel[i]
                  << std::setw(18) << std::fixed << std::setprecision(2)
                  << (static_cast<double>(st.probesAtLevel[i]) / st.nodesAtLevel[i])
                  << std::defaultfloat << "\n";
    }
}

static void fareAuditEvidence() {
    std::cout << "\n===== PART (b) : DOES THE PROGRAM REALLY OBEY T(n) = 3T(n/3) + lg n? =====\n";

    std::cout << "\n  (1) MEASURED COST vs THE RECURRENCE EVALUATED SYMBOLICALLY\n"
              << "      The recurrence is unrolled independently with the same split rule and\n"
              << "      cost model. Equality for every n is a direct proof that the program\n"
              << "      implements the intended recurrence.\n\n";
    std::cout << std::right << std::setw(12) << "n" << std::setw(16) << "measured"
              << std::setw(18) << "3T(n/3)+lg n" << std::setw(12) << "match" << "\n";
    std::mt19937 rng(77u);
    std::vector<int> ns = { 3, 9, 27, 81, 243, 729, 2187, 6561, 19683, 59049, 100, 1000, 12345 };
    std::sort(ns.begin(), ns.end());
    for (int n : ns) {
        std::vector<double> d(n);
        double acc = 0.0;
        std::uniform_real_distribution<double> gap(0.6, 2.6);
        for (int i = 0; i < n; ++i) { d[i] = acc; acc += gap(rng); }
        AuditStats st;
        fareAudit(d, 0, n - 1, 0, st);
        const long long expect = recurrenceValue(n);
        std::cout << std::right << std::setw(12) << n << std::setw(16) << st.probes
                  << std::setw(18) << expect << std::setw(12)
                  << (st.probes == expect ? "yes" : "NO") << "\n";
    }

    std::cout << "\n  (2) THE MASTER METHOD SAYS Theta(n).  IS THE MEASURED COST LINEAR?\n"
              << "      If T(n) = Theta(n) then cost/n is constant and tripling n triples cost.\n\n";
    std::cout << std::right << std::setw(12) << "n = 3^k" << std::setw(16) << "probes"
              << std::setw(14) << "probes/n" << std::setw(21) << "cost(n)/cost(n/3)"
              << std::setw(14) << "time(ms)" << std::setw(14) << "time/n (ns)" << "\n";
    long long prevCost = 0;
    for (int k = 1; k <= 13; ++k) {
        int n = 1;
        for (int i = 0; i < k; ++i) n *= 3;
        std::vector<double> d(n);
        double acc = 0.0;
        std::uniform_real_distribution<double> gap(0.6, 2.6);
        for (int i = 0; i < n; ++i) { d[i] = acc; acc += gap(rng); }
        AuditStats st;
        const double t0 = nowMs();
        fareAudit(d, 0, n - 1, 0, st);
        const double ms = nowMs() - t0;
        std::cout << std::right << std::setw(12) << n << std::setw(16) << st.probes
                  << std::setw(14) << std::fixed << std::setprecision(4)
                  << (static_cast<double>(st.probes) / n);
        if (prevCost > 0) std::cout << std::setw(21) << std::setprecision(4)
                                    << (static_cast<double>(st.probes) / prevCost);
        else              std::cout << std::setw(21) << "-";
        std::cout << std::setw(14) << std::setprecision(3) << ms
                  << std::setw(14) << std::setprecision(1) << (ms * 1e6 / n)
                  << std::defaultfloat << "\n";
        prevCost = st.probes;
    }
    std::cout << "\n      probes/n settles on a constant and the ratio column settles on 3:\n"
              << "      tripling the input triples the work. That is Theta(n), exactly as\n"
              << "      Case 1 of the Master Theorem predicts.\n";

    std::cout << "\n  (3) THE SUBSTITUTION METHOD, CHECKED NUMERICALLY\n"
              << "      The induction needs the lower-order slack terms. The claim proved in\n"
              << "      the report is  T(n) <= c*n - d*lg n - e  with c = 4, d = 1, e = 2.4.\n"
              << "      c = 4 (rather than 2) is what covers the base cases n = 1 and n = 2,\n"
              << "      which the split rule really does reach: n = 4 splits into 1, 1, 2.\n\n";
    std::cout << std::right << std::setw(12) << "n" << std::setw(16) << "T(n) measured"
              << std::setw(22) << "4n - lg n - 2.4" << std::setw(12) << "holds?" << "\n";
    std::vector<int> subs = { 1, 2, 3, 4, 5 };
    for (int k = 2; k <= 10; ++k) {
        int n = 1;
        for (int i = 0; i < k; ++i) n *= 3;
        subs.push_back(n);
    }
    for (int n : subs) {
        const long long T = recurrenceValue(n);
        const double bound = 4.0 * n - std::log2(static_cast<double>(n)) - 2.4;
        std::cout << std::right << std::setw(12) << n << std::setw(16) << T
                  << std::setw(22) << std::fixed << std::setprecision(2) << bound
                  << std::setw(12) << (T <= bound ? "yes" : "NO") << std::defaultfloat << "\n";
    }

    std::cout << "\n  (4) COMPARATIVE STUDY : WHAT IF THE COMBINE WERE LINEAR INSTEAD?\n"
              << "      Replacing the binary search with a scan gives T(n) = 3T(n/3) + n,\n"
              << "      which is Master Case 2 => Theta(n lg n). The extra lg n factor is\n"
              << "      visible in the ratio column below.\n\n";
    std::cout << std::right << std::setw(12) << "n" << std::setw(18) << "lg-combine"
              << std::setw(18) << "linear-combine" << std::setw(14) << "ratio" << "\n";
    for (int k = 4; k <= 12; ++k) {
        int n = 1;
        for (int i = 0; i < k; ++i) n *= 3;
        std::vector<double> d(n);
        double acc = 0.0;
        std::uniform_real_distribution<double> gap(0.6, 2.6);
        for (int i = 0; i < n; ++i) { d[i] = acc; acc += gap(rng); }
        AuditStats st;
        fareAudit(d, 0, n - 1, 0, st);
        long long linWork = 0;
        fareAuditLinearCombine(d, 0, n - 1, linWork);
        std::cout << std::right << std::setw(12) << n << std::setw(18) << st.probes
                  << std::setw(18) << linWork << std::setw(14) << std::fixed
                  << std::setprecision(2) << (static_cast<double>(linWork) / st.probes)
                  << std::defaultfloat << "\n";
    }
}

/* ==================================================================================== *
 *  TEST SUITE                                                                          *
 * ==================================================================================== */

static bool isIncreasingSubsequenceOf(const std::vector<int>& sub, const std::vector<int>& v) {
    size_t j = 0;
    for (size_t i = 0; i < v.size() && j < sub.size(); ++i) if (v[i] == sub[j]) ++j;
    if (j != sub.size()) return false;
    for (size_t i = 1; i < sub.size(); ++i) if (sub[i - 1] >= sub[i]) return false;
    return true;
}

static void runTestSuite() {
    g_pass = g_fail = 0;
    std::cout << "\n==================== TEST SUITE (Question 2) ====================\n";
    std::mt19937 rng(424242u);

    std::cout << "\n  TC01  LIS: O(n lg n) result equals the O(n^2) dynamic programme\n";
    {
        bool ok = true;
        for (int t = 0; t < 200 && ok; ++t) {
            const int n = 1 + static_cast<int>(rng() % 120);
            std::vector<int> v(n);
            for (int& x : v) x = static_cast<int>(rng() % 50);
            if (lisLinearithmic(v, nullptr, nullptr) != lisQuadratic(v, nullptr)) ok = false;
        }
        check("agreement on 200 random arrays (n up to 120, many duplicates)", ok);
    }

    std::cout << "\n  TC02  LIS: boundary inputs\n";
    {
        std::vector<int> empty;
        check("empty input gives length 0", lisLinearithmic(empty, nullptr, nullptr) == 0);
        check("single element gives length 1",
              lisLinearithmic(std::vector<int>{ 7 }, nullptr, nullptr) == 1);
        check("all values equal gives length 1 (strictly increasing required)",
              lisLinearithmic(std::vector<int>(50, 4), nullptr, nullptr) == 1);
        std::vector<int> up(200), down(200);
        std::iota(up.begin(), up.end(), 0);
        for (int i = 0; i < 200; ++i) down[i] = 200 - i;
        check("already increasing gives length n", lisLinearithmic(up, nullptr, nullptr) == 200);
        check("strictly decreasing gives length 1", lisLinearithmic(down, nullptr, nullptr) == 1);
    }

    std::cout << "\n  TC03  LIS: the reconstructed chain is genuinely a rising subsequence\n";
    {
        bool ok = true;
        for (int t = 0; t < 100 && ok; ++t) {
            const int n = 1 + static_cast<int>(rng() % 200);
            std::vector<int> v(n), seq;
            for (int& x : v) x = static_cast<int>(rng() % 1000);
            const int len = lisLinearithmic(v, &seq, nullptr);
            if (static_cast<int>(seq.size()) != len || !isIncreasingSubsequenceOf(seq, v)) ok = false;
        }
        check("100 random reconstructions are valid subsequences of the reported length", ok);
        std::vector<int> seq;
        const std::vector<int> coe = coePremiums();
        const int len = lisLinearithmic(coe, &seq, nullptr);
        check("COE data set: chain of length 14 recovered and valid",
              len == 14 && isIncreasingSubsequenceOf(seq, coe), "length " + std::to_string(len));
    }

    std::cout << "\n  TC04  LIS: the cost is linearithmic, not linear\n";
    {
        long long p1 = 0, p2 = 0;
        std::vector<int> a(100000), b(800000);
        for (int& x : a) x = static_cast<int>(rng() % 1000000);
        for (int& x : b) x = static_cast<int>(rng() % 1000000);
        lisLinearithmic(a, nullptr, &p1);
        lisLinearithmic(b, nullptr, &p2);
        const double perN1 = static_cast<double>(p1) / 100000.0;
        const double perN2 = static_cast<double>(p2) / 800000.0;
        std::ostringstream os;
        os << std::fixed << std::setprecision(3) << perN1 << " -> " << perN2;
        check("probes/n RISES with n (so it is not Theta(n))", perN2 > perN1 + 0.2, os.str());
        const double r1 = p1 / (100000.0 * std::log2(100000.0));
        const double r2 = p2 / (800000.0 * std::log2(800000.0));
        std::ostringstream os2;
        os2 << std::fixed << std::setprecision(4) << r1 << " vs " << r2;
        check("probes/(n lg n) stays within 10% across an 8x size increase",
              std::fabs(r1 - r2) / r1 < 0.10, os2.str());
    }

    std::cout << "\n  TC05  TSP: brute force agrees with Held-Karp\n";
    {
        bool ok = true;
        std::uniform_real_distribution<double> co(0.0, 20.0);
        for (int t = 0; t < 12 && ok; ++t) {
            const int n = 4 + static_cast<int>(rng() % 6);          /* 4..9 cities */
            std::vector<Attraction> a(n);
            for (int i = 0; i < n; ++i) { a[i].x = co(rng); a[i].y = co(rng); }
            const std::vector<std::vector<double>> d = distanceMatrix(a);
            if (std::fabs(tspBruteForce(d, nullptr, nullptr) - tspHeldKarp(d)) > 1e-9) ok = false;
        }
        check("12 random instances of 4..9 cities give identical optimal length", ok);
    }

    std::cout << "\n  TC06  TSP: the enumeration really visits n! tours\n";
    {
        bool ok = true;
        long long fact = 1;
        const std::vector<Attraction> all = attractions();
        for (int n = 1; n <= 9; ++n) {
            fact *= n;
            std::vector<Attraction> a(all.begin(), all.begin() + n);
            long long tours = 0;
            tspBruteForce(distanceMatrix(a), nullptr, &tours);
            if (tours != fact) ok = false;
        }
        check("tours evaluated == n! for n = 1..9 (9! = 362880)", ok);
    }

    std::cout << "\n  TC07  TSP: growth ratio approaches n (factorial signature)\n";
    {
        std::vector<Attraction> all = attractions();
        double t9 = 0, t10 = 0, t11 = 0;
        for (int n = 9; n <= 11; ++n) {
            std::vector<Attraction> sub(all.begin(), all.begin() + n);
            const std::vector<std::vector<double>> d = distanceMatrix(sub);
            const double t0 = nowMs();
            tspBruteForce(d, nullptr, nullptr);
            const double ms = nowMs() - t0;
            if (n == 9) t9 = ms; else if (n == 10) t10 = ms; else t11 = ms;
        }
        std::ostringstream os;
        os << std::fixed << std::setprecision(2) << "t10/t9 = " << (t10 / t9)
           << ", t11/t10 = " << (t11 / t10);
        check("both ratios lie in [5,18], bracketing n = 10 and n = 11",
              t10 / t9 >= 5.0 && t10 / t9 <= 18.0 && t11 / t10 >= 5.0 && t11 / t10 <= 18.0, os.str());
    }

    std::cout << "\n  TC08  Fare audit: binary-search version equals the linear-scan oracle\n";
    {
        bool ok = true;
        std::uniform_real_distribution<double> gap(0.2, 4.0);
        for (int t = 0; t < 200 && ok; ++t) {
            const int n = 1 + static_cast<int>(rng() % 400);
            std::vector<double> d(n);
            double acc = 0;
            for (int i = 0; i < n; ++i) { d[i] = acc; acc += gap(rng); }
            AuditStats st;
            const AuditResult a = fareAudit(d, 0, n - 1, 0, st);
            long long w = 0;
            const AuditResult b = fareAuditLinearCombine(d, 0, n - 1, w);
            if (a.minWithin != b.minWithin || a.maxWithin != b.maxWithin ||
                a.totalWithin != b.totalWithin) ok = false;
        }
        check("identical results on 200 random lines of up to 400 stations", ok);
    }

    std::cout << "\n  TC09  Fare audit: measured cost == T(n) = 3T(n/3) + lg n, exactly\n";
    {
        bool ok = true;
        std::string firstBad;
        std::uniform_real_distribution<double> gap(0.6, 2.6);
        for (int n = 1; n <= 1500; ++n) {
            std::vector<double> d(n);
            double acc = 0;
            for (int i = 0; i < n; ++i) { d[i] = acc; acc += gap(rng); }
            AuditStats st;
            fareAudit(d, 0, n - 1, 0, st);
            if (st.probes != recurrenceValue(n)) {
                ok = false;
                if (firstBad.empty()) firstBad = "first mismatch at n = " + std::to_string(n);
                break;
            }
        }
        check("exact match for EVERY n from 1 to 1500, not just powers of 3", ok, firstBad);
    }

    std::cout << "\n  TC10  Fare audit: the Master Theorem prediction Theta(n) holds\n";
    {
        std::vector<double> perN;
        std::uniform_real_distribution<double> gap(0.6, 2.6);
        for (int k = 6; k <= 12; ++k) {
            int n = 1;
            for (int i = 0; i < k; ++i) n *= 3;
            std::vector<double> d(n);
            double acc = 0;
            for (int i = 0; i < n; ++i) { d[i] = acc; acc += gap(rng); }
            AuditStats st;
            fareAudit(d, 0, n - 1, 0, st);
            perN.push_back(static_cast<double>(st.probes) / n);
        }
        const double lo = *std::min_element(perN.begin(), perN.end());
        const double hi = *std::max_element(perN.begin(), perN.end());
        std::ostringstream os;
        os << std::fixed << std::setprecision(4) << "cost/n in [" << lo << ", " << hi << "]";
        check("cost/n stays within 2% over a 729x range of n", (hi - lo) / lo < 0.02, os.str());

        /* Tripling n must triple the cost. */
        std::vector<double> d1(59049), d2(177147);
        double acc = 0; for (double& x : d1) { x = acc; acc += 1.3; }
        acc = 0;        for (double& x : d2) { x = acc; acc += 1.3; }
        AuditStats s1, s2;
        fareAudit(d1, 0, 59048, 0, s1);
        fareAudit(d2, 0, 177146, 0, s2);
        const double ratio = static_cast<double>(s2.probes) / s1.probes;
        std::ostringstream os2;
        os2 << std::fixed << std::setprecision(4) << "ratio = " << ratio;
        check("tripling n multiplies the cost by ~3", std::fabs(ratio - 3.0) < 0.05, os2.str());
    }

    std::cout << "\n  TC11  Fare audit: boundary inputs\n";
    {
        std::vector<double> one = { 0.0 };
        AuditStats s;
        AuditResult r = fareAudit(one, 0, 0, 0, s);
        check("single station: 1 station within band, 1 node visited",
              r.minWithin == 1 && r.totalWithin == 1 && s.nodes == 1);

        std::vector<double> flat(81, 0.0);              /* all stations co-located */
        AuditStats s2;
        r = fareAudit(flat, 0, 80, 0, s2);
        check("zero-length line: every stretch is entirely inside one band",
              r.minWithin >= 1 && r.maxWithin == 81);

        std::vector<double> farApart(81);
        for (int i = 0; i < 81; ++i) farApart[i] = i * 50.0;   /* every hop exceeds the band */
        AuditStats s3;
        r = fareAudit(farApart, 0, 80, 0, s3);
        check("every hop exceeds the band cap: no stretch reaches past its first station",
              r.minWithin == 1 && r.maxWithin == 1);

        AuditStats s4;
        std::vector<double> three = { 0.0, 1.0, 2.0 };
        r = fareAudit(three, 0, 2, 0, s4);
        check("n = 3 hits the base case exactly once", s4.nodes == 1 && s4.probes == 1);
    }

    std::cout << "\n  TC12  Fare audit: the lg-combine really is cheaper than a linear combine\n";
    {
        std::vector<double> d(59049);
        double acc = 0; for (double& x : d) { x = acc; acc += 1.3; }
        AuditStats st;
        fareAudit(d, 0, 59048, 0, st);
        long long linWork = 0;
        fareAuditLinearCombine(d, 0, 59048, linWork);
        const double ratio = static_cast<double>(linWork) / st.probes;
        std::ostringstream os;
        os << std::fixed << std::setprecision(2) << "linear/lg = " << ratio
           << "x at n = 59049, log3(n) = 10";
        check("linear combine costs ~log3(n) times more, as Theta(n lg n) vs Theta(n) predicts",
              ratio > 8.0 && ratio < 13.0, os.str());
    }

    std::cout << "\n  ------------------------------------------------------------\n";
    std::cout << "  TOTAL: " << g_pass << " passed, " << g_fail << " failed.\n";
    std::cout << "  ------------------------------------------------------------\n";
}

/* ==================================================================================== *
 *  MENU                                                                                *
 * ==================================================================================== */

static void runEverything() {
    growthTable();
    lisDemo();
    lisScaling();
    tspDemo();
    tspScaling();
    fareAuditDemo();
    fareAuditEvidence();
    runTestSuite();
}

int main(int argc, char** argv) {
    std::cout <<
        "\n============================================================================\n"
        "  CSD3130/CSD3131 Assignment Part 1 - QUESTION 2\n"
        "  (a) Orders of growth: LIS at O(n lg n) and exact TSP at O(n * n!)\n"
        "  (b) LTA fare-band audit satisfying T(n) = 3T(n/3) + lg n\n"
        "============================================================================\n";

    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--all") { runEverything(); return g_fail == 0 ? 0 : 1; }

    for (;;) {
        std::cout <<
            "\n  MENU\n"
            "   1. (a) The seven orders of growth, side by side\n"
            "   2. (a) Linearithmic demo  : longest run-up in COE premiums\n"
            "   3. (a) Linearithmic proof : scaling evidence + O(n^2) comparison\n"
            "   4. (a) Factorial demo     : shortest one-day tour of Singapore\n"
            "   5. (a) Factorial proof    : n! scaling + Held-Karp comparison\n"
            "   6. (b) Fare-band audit demo + the measured recursion tree\n"
            "   7. (b) Fare-band audit proofs: recurrence, master, substitution\n"
            "   8. Run the full test suite (12 test cases)\n"
            "   9. Run everything\n"
            "   0. Exit\n"
            "  Choice: ";
        int choice = -1;
        if (!(std::cin >> choice)) break;
        switch (choice) {
            case 1: growthTable();       break;
            case 2: lisDemo();           break;
            case 3: lisScaling();        break;
            case 4: tspDemo();           break;
            case 5: tspScaling();        break;
            case 6: fareAuditDemo();     break;
            case 7: fareAuditEvidence(); break;
            case 8: runTestSuite();      break;
            case 9: runEverything();     break;
            case 0: std::cout << "  Bye.\n"; return 0;
            default: std::cout << "  Unknown option.\n";
        }
    }
    return 0;
}
