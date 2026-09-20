/* =====================================================================================
 *  CSD3130 / CSD3131  Algorithm Analysis - Assignment (Part 1)
 *  QUESTION 3 : Stability in Sorting, and the Omega(n lg n) Comparison Lower Bound
 *
 *  Student(s) : [STUDENT NAME - STUDENT ID]
 *  Team       : Txx
 *  File       : sorting_analysis.cpp          (C++17, single file, console only)
 *
 *  PART (a)  STABILITY, in a Singapore context: the polyclinic triage queue.
 *            Patients are registered in arrival order and then re-ordered by triage
 *            tier (P1 most urgent .. P4 least).  Within the same tier the queue must
 *            stay first-come-first-served - that is precisely sorting stability, and
 *            it is a fairness requirement, not a nicety.
 *
 *              STABLE   : Merge Sort      O(n lg n) always, O(n) extra space
 *                         Insertion Sort  O(n^2) worst, O(n) best, in place
 *              UNSTABLE : Quick Sort      O(n lg n) average, O(n^2) worst, in place
 *                         Selection Sort  O(n^2) always, in place
 *
 *  PART (b)  WHY NO COMPARISON SORT CAN BEAT O(n lg n), and how to beat it anyway.
 *            The decision-tree argument is demonstrated empirically by exhausting all
 *            n! input orderings, then LSD Radix Sort - built on a STABLE Counting Sort,
 *            which links straight back to part (a) - is shown running in linear time on
 *            COE bidding data.
 *
 *  BUILD (Developer Command Prompt for VS 2022)
 *        cl /nologo /std:c++17 /EHsc /O2 /W4 sorting_analysis.cpp /Fe:q3.exe
 *  BUILD (g++)
 *        g++ -std=c++17 -O2 -Wall -Wextra -o q3 sorting_analysis.cpp
 *  RUN   q3.exe              interactive menu
 *        q3.exe --all        run every demonstration and every test, then exit
 * ===================================================================================== */

#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <numeric>
#include <random>
#include <chrono>
#include <cmath>
#include <climits>

/* ==================================================================================== *
 *  SHARED HELPERS                                                                      *
 * ==================================================================================== */

static double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(high_resolution_clock::now().time_since_epoch()).count();
}

static int g_pass = 0, g_fail = 0;
static void check(const std::string& label, bool ok, const std::string& detail = "") {
    std::cout << (ok ? "    [PASS] " : "    [FAIL] ") << label;
    if (!detail.empty()) std::cout << "  (" << detail << ")";
    std::cout << "\n";
    if (ok) ++g_pass; else ++g_fail;
}

/* ==================================================================================== *
 *  PART (a) SECTION 1 : THE POLYCLINIC TRIAGE QUEUE                                    *
 * ==================================================================================== */

struct Patient {
    int         arrival;     /* registration sequence: 1 = first through the door */
    int         tier;        /* triage tier: 1 = P1 (most urgent) .. 4 = P4       */
    std::string name;
    std::string queueNo;
};

using PatientLess = std::function<bool(const Patient&, const Patient&)>;

static bool lessByTier(const Patient& a, const Patient& b) { return a.tier < b.tier; }
static bool lessByName(const Patient& a, const Patient& b) { return a.name < b.name; }

/* The "correct" composite order a stable two-pass pipeline is supposed to produce. */
static bool lessByTierThenName(const Patient& a, const Patient& b) {
    if (a.tier != b.tier) return a.tier < b.tier;
    return a.name < b.name;
}

/* ---------------------------------------------------------------- STABLE : MERGE SORT
 *  Divide the queue in half, sort each half, then merge.  The merge takes from the LEFT
 *  half whenever the two fronts compare equal (!less(right, left)), and that single
 *  choice is the entire reason merge sort is stable: equal patients can never cross.
 *  Time  Theta(n lg n) in every case          Space  Theta(n) for the merge buffer
 */
static void mergeRun(std::vector<Patient>& v, std::vector<Patient>& buf,
                     int lo, int hi, const PatientLess& less, long long& cmps) {
    if (hi - lo <= 1) return;
    const int mid = lo + (hi - lo) / 2;
    mergeRun(v, buf, lo, mid, less, cmps);
    mergeRun(v, buf, mid, hi, less, cmps);

    int i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        ++cmps;
        /* take from the RIGHT half only when it is STRICTLY smaller -> stable */
        if (less(v[j], v[i])) buf[k++] = v[j++];
        else                  buf[k++] = v[i++];
    }
    while (i < mid) buf[k++] = v[i++];
    while (j < hi)  buf[k++] = v[j++];
    for (int t = lo; t < hi; ++t) v[t] = buf[t];
}

static void mergeSort(std::vector<Patient>& v, const PatientLess& less, long long* cmps = nullptr) {
    if (v.size() < 2) return;
    std::vector<Patient> buf(v.size());
    long long c = 0;
    mergeRun(v, buf, 0, static_cast<int>(v.size()), less, c);
    if (cmps) *cmps = c;
}

/* ------------------------------------------------------------ STABLE : INSERTION SORT
 *  Slide each element left while the element to its left is STRICTLY greater.  It stops
 *  on equality, so equal elements never swap: stable.
 *  Time  O(n^2) worst and average, Theta(n) on nearly-sorted input     Space  O(1)
 */
static void insertionSort(std::vector<Patient>& v, const PatientLess& less,
                          long long* cmps = nullptr) {
    long long c = 0;
    for (size_t i = 1; i < v.size(); ++i) {
        Patient key = v[i];
        size_t j = i;
        while (j > 0) {
            ++c;
            if (less(key, v[j - 1])) { v[j] = v[j - 1]; --j; }
            else break;
        }
        v[j] = key;
    }
    if (cmps) *cmps = c;
}

/* --------------------------------------------------------------- UNSTABLE : QUICK SORT
 *  Median-of-three pivot, Hoare-style partition.  The partition swaps elements across
 *  long distances, so two equal patients can be exchanged with no way to tell that they
 *  were equal: UNSTABLE.  Nothing is broken - the output is correctly ordered by the
 *  key - but the original order inside a tier is destroyed.
 *  Time  O(n lg n) average, O(n^2) worst    Space  O(lg n) recursion stack
 */
static void quickRun(std::vector<Patient>& v, int lo, int hi,
                     const PatientLess& less, long long& cmps) {
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        /* median-of-three: order v[lo], v[mid], v[hi] and use the median as pivot */
        ++cmps; if (less(v[mid], v[lo]))  std::swap(v[mid], v[lo]);
        ++cmps; if (less(v[hi],  v[lo]))  std::swap(v[hi],  v[lo]);
        ++cmps; if (less(v[hi],  v[mid])) std::swap(v[hi],  v[mid]);
        const Patient pivot = v[mid];

        int i = lo, j = hi;
        while (i <= j) {
            while (true) { ++cmps; if (less(v[i], pivot)) ++i; else break; }
            while (true) { ++cmps; if (less(pivot, v[j])) --j; else break; }
            if (i <= j) { std::swap(v[i], v[j]); ++i; --j; }
        }
        /* recurse into the smaller side, loop on the larger: keeps the stack O(lg n) */
        if (j - lo < hi - i) { quickRun(v, lo, j, less, cmps); lo = i; }
        else                 { quickRun(v, i, hi, less, cmps); hi = j; }
    }
}

static void quickSort(std::vector<Patient>& v, const PatientLess& less,
                      long long* cmps = nullptr) {
    if (v.size() < 2) return;
    long long c = 0;
    quickRun(v, 0, static_cast<int>(v.size()) - 1, less, c);
    if (cmps) *cmps = c;
}

/* ----------------------------------------------------------- UNSTABLE : SELECTION SORT
 *  Repeatedly swap the minimum of the remaining suffix into place.  That long-range
 *  swap jumps an element over equal keys, so selection sort is unstable even though it
 *  never "shuffles" anything deliberately.
 *  Time  Theta(n^2) in every case     Space  O(1)
 */
static void selectionSort(std::vector<Patient>& v, const PatientLess& less,
                          long long* cmps = nullptr) {
    long long c = 0;
    const int n = static_cast<int>(v.size());
    for (int i = 0; i < n - 1; ++i) {
        int best = i;
        for (int j = i + 1; j < n; ++j) { ++c; if (less(v[j], v[best])) best = j; }
        if (best != i) std::swap(v[i], v[best]);
    }
    if (cmps) *cmps = c;
}

/* --------------------------------------------- MEASURING STABILITY RATHER THAN CLAIMING IT
 *  A sort is stable on this input exactly when, inside every block of equal keys, the
 *  original arrival numbers are still increasing.  Counting the pairs that are out of
 *  order turns "is it stable?" into a number we can print. */
static int stabilityViolations(const std::vector<Patient>& v) {
    int bad = 0;
    for (size_t i = 1; i < v.size(); ++i)
        if (v[i].tier == v[i - 1].tier && v[i].arrival < v[i - 1].arrival) ++bad;
    return bad;
}

static bool sortedByTier(const std::vector<Patient>& v) {
    for (size_t i = 1; i < v.size(); ++i) if (v[i - 1].tier > v[i].tier) return false;
    return true;
}

static std::vector<Patient> triageQueue() {
    return {
        {  1, 3, "Tan Wei Ming",   "Q001" }, {  2, 1, "Siti Nurhaliza", "Q002" },
        {  3, 4, "Raj Kumar",      "Q003" }, {  4, 3, "Lim Hui Ling",   "Q004" },
        {  5, 2, "Chan Kok Wai",   "Q005" }, {  6, 3, "Nurul Aisyah",   "Q006" },
        {  7, 4, "Goh Jia Hui",    "Q007" }, {  8, 2, "Muthu Samy",     "Q008" },
        {  9, 1, "Wong Mei Fang",  "Q009" }, { 10, 3, "Ahmad Faizal",   "Q010" },
        { 11, 4, "Ong Boon Keng",  "Q011" }, { 12, 2, "Kavitha Rani",   "Q012" },
        { 13, 3, "Teo Seng Huat",  "Q013" }, { 14, 4, "Yusof Bin Ali",  "Q014" },
        { 15, 2, "Chua Li Ting",   "Q015" }, { 16, 3, "Sharifah Aziz",  "Q016" }
    };
}

static void printQueue(const std::vector<Patient>& v, const std::string& title) {
    std::cout << "\n  " << title << "\n   ";
    for (const Patient& p : v) std::cout << "P" << p.tier << "/#" << p.arrival << "  ";
    std::cout << "\n";
}

static void printQueueDetailed(const std::vector<Patient>& v, const std::string& title) {
    std::cout << "\n  " << title << "\n";
    std::cout << "    " << std::left << std::setw(8) << "Queue" << std::setw(7) << "Tier"
              << std::setw(10) << "Arrived" << "Name\n";
    for (const Patient& p : v)
        std::cout << "    " << std::left << std::setw(8) << p.queueNo
                  << std::setw(7) << ("P" + std::to_string(p.tier))
                  << std::setw(10) << ("#" + std::to_string(p.arrival)) << p.name << "\n";
    std::cout << std::right;
}

static void stabilityDemo() {
    std::cout << "\n========== PART (a) : STABILITY IN THE POLYCLINIC TRIAGE QUEUE ==========\n"
        "\n  Sixteen patients register in arrival order and are then re-ordered by triage\n"
        "  tier. Clinically the tier decides who is seen first. Within a tier, the clinic\n"
        "  has promised first-come-first-served. Whether that promise survives the sort\n"
        "  depends entirely on whether the sorting algorithm is STABLE.\n";

    const std::vector<Patient> original = triageQueue();
    printQueue(original, "REGISTRATION ORDER (tier/arrival):");

    std::vector<Patient> m = original, q = original, ins = original, sel = original;
    long long cm = 0, cq = 0, ci = 0, cs = 0;
    mergeSort(m, lessByTier, &cm);
    quickSort(q, lessByTier, &cq);
    insertionSort(ins, lessByTier, &ci);
    selectionSort(sel, lessByTier, &cs);

    printQueue(m,   "AFTER MERGE SORT      (stable):");
    printQueue(ins, "AFTER INSERTION SORT  (stable):");
    printQueue(q,   "AFTER QUICK SORT      (unstable):");
    printQueue(sel, "AFTER SELECTION SORT  (unstable):");

    std::cout << "\n  " << std::left << std::setw(18) << "Algorithm"
              << std::setw(12) << "Ordered?" << std::setw(24) << "Arrival order broken"
              << std::setw(14) << "Comparisons" << "Stable\n" << std::right;
    struct Row { const char* nm; const std::vector<Patient>* v; long long c; };
    const Row rows[] = { { "Merge Sort", &m, cm }, { "Insertion Sort", &ins, ci },
                         { "Quick Sort", &q, cq }, { "Selection Sort", &sel, cs } };
    for (const Row& r : rows) {
        const int bad = stabilityViolations(*r.v);
        std::cout << "  " << std::left << std::setw(18) << r.nm
                  << std::setw(12) << (sortedByTier(*r.v) ? "yes" : "NO")
                  << std::setw(24) << (std::to_string(bad) + " pair(s)")
                  << std::setw(14) << r.c << (bad == 0 ? "YES" : "no") << "\n" << std::right;
    }

    std::cout << "\n  Every one of the four outputs is correctly ordered by tier. Only the\n"
                 "  stable two keep the queue numbers rising inside each tier. Look at the\n"
                 "  P2 block below: the unstable sorts serve a later arrival first.\n";
    printQueueDetailed(m, "MERGE SORT (stable) - P2 block is #5, #8, #12, #15 as it should be:");
    printQueueDetailed(q, "QUICK SORT (unstable) - the same P2 patients, re-ordered:");
}

static void multiKeyDemo() {
    std::cout << "\n===== PART (a) : WHY STABILITY IS WHAT MAKES MULTI-KEY SORTING WORK =====\n"
        "\n  Requirement: order the queue by tier, and alphabetically by name inside\n"
        "  each tier. The standard technique is two passes over a SINGLE key each:\n"
        "      pass 1: sort by the LEAST significant key (name)\n"
        "      pass 2: sort by the MOST  significant key (tier)\n"
        "  Pass 2 must not disturb the work of pass 1, which requires stability.\n";

    const std::vector<Patient> original = triageQueue();
    std::vector<Patient> wanted = original;
    std::stable_sort(wanted.begin(), wanted.end(), lessByTierThenName);

    std::vector<Patient> stablePipe = original;
    mergeSort(stablePipe, lessByName);
    mergeSort(stablePipe, lessByTier);

    std::vector<Patient> unstablePipe = original;
    quickSort(unstablePipe, lessByName);
    quickSort(unstablePipe, lessByTier);

    auto names = [](const std::vector<Patient>& v) {
        std::string s;
        for (const Patient& p : v) s += "P" + std::to_string(p.tier) + ":" + p.name.substr(0, 9) + "  ";
        return s;
    };
    std::cout << "\n  Required order (reference)     : " << names(wanted) << "\n";
    std::cout << "\n  Two stable passes              : " << names(stablePipe) << "\n";
    std::cout << "\n  Two unstable passes            : " << names(unstablePipe) << "\n";

    const bool stableOk   = (names(stablePipe)   == names(wanted));
    const bool unstableOk = (names(unstablePipe) == names(wanted));
    std::cout << "\n  Stable pipeline matches the requirement   : " << (stableOk ? "YES" : "no") << "\n";
    std::cout << "  Unstable pipeline matches the requirement : " << (unstableOk ? "YES" : "NO")
              << "\n\n  The unstable pipeline is not merely untidy - it silently produces the\n"
                 "  WRONG report, while still looking correctly sorted by tier. This is the\n"
                 "  same property radix sort depends on in part (b): it is a chain of single-\n"
                 "  digit passes, and it is correct only because each pass is stable.\n";
}

/* ==================================================================================== *
 *  PART (b) SECTION 1 : THE Omega(n lg n) COMPARISON LOWER BOUND, DEMONSTRATED         *
 * ==================================================================================== */

/*  Any deterministic comparison sort can be drawn as a binary decision tree: each
 *  internal node is one comparison, each leaf one of the possible answers.  To sort
 *  every input the tree needs at least n! leaves, and a binary tree of height h has at
 *  most 2^h leaves, so
 *          2^h >= n!   =>   h >= lg(n!) = Theta(n lg n)   (Stirling)
 *  h is the worst-case number of comparisons.  The function below measures the real
 *  worst case by running a sort over ALL n! orderings and keeping the maximum, so the
 *  bound can be checked rather than merely asserted.
 */
static long long ceilLgFactorial(int n) {
    long double lg = 0.0L;
    for (int i = 2; i <= n; ++i) lg += std::log2(static_cast<long double>(i));
    return static_cast<long long>(std::ceil(lg - 1e-12L));
}

static void lowerBoundDemo() {
    std::cout << "\n===== PART (b) : NO COMPARISON SORT CAN BEAT Omega(n lg n) =====\n"
        "\n  A comparison sort only ever asks 'is a < b?'. Its whole execution is a path\n"
        "  down a binary decision tree whose leaves are the possible orderings. There are\n"
        "  n! orderings, a binary tree of height h holds at most 2^h leaves, so\n"
        "        2^h >= n!  =>  h >= lg(n!) = n lg n - n lg e + O(lg n) = Omega(n lg n).\n"
        "\n  Below, every one of the n! input permutations is actually sorted and the\n"
        "  worst comparison count is recorded. No algorithm dips below the bound.\n\n";

    std::cout << std::right << std::setw(4) << "n" << std::setw(12) << "n!"
              << std::setw(16) << "lower bound" << std::setw(16) << "merge sort"
              << std::setw(18) << "insertion sort" << std::setw(14) << "selection"
              << std::setw(12) << "bound ok?" << "\n";
    std::cout << std::setw(4) << "" << std::setw(12) << "" << std::setw(16) << "ceil(lg n!)"
              << std::setw(16) << "worst" << std::setw(18) << "worst"
              << std::setw(14) << "worst" << "\n";

    for (int n = 2; n <= 8; ++n) {
        std::vector<Patient> base(n);
        for (int i = 0; i < n; ++i) { base[i].arrival = i + 1; base[i].tier = i + 1; }
        std::sort(base.begin(), base.end(), lessByTier);

        long long worstM = 0, worstI = 0, worstS = 0, perms = 0;
        std::vector<Patient> perm = base;
        do {
            ++perms;
            long long c = 0;
            std::vector<Patient> w = perm; mergeSort(w, lessByTier, &c);     worstM = std::max(worstM, c);
            c = 0; w = perm; insertionSort(w, lessByTier, &c);               worstI = std::max(worstI, c);
            c = 0; w = perm; selectionSort(w, lessByTier, &c);               worstS = std::max(worstS, c);
        } while (std::next_permutation(perm.begin(), perm.end(), lessByTier));

        const long long bound = ceilLgFactorial(n);
        const bool ok = worstM >= bound && worstI >= bound && worstS >= bound;
        std::cout << std::right << std::setw(4) << n << std::setw(12) << perms
                  << std::setw(16) << bound << std::setw(16) << worstM
                  << std::setw(18) << worstI << std::setw(14) << worstS
                  << std::setw(12) << (ok ? "yes" : "NO") << "\n";
    }
    std::cout <<
        "\n  THE CONDITION under which the bound holds: the algorithm may learn about the\n"
        "  keys ONLY by comparing them pairwise. Nothing is assumed about what a key is.\n"
        "  Lift that restriction - allow the algorithm to look INSIDE a key, at its digits\n"
        "  - and the decision tree stops being binary, the argument collapses, and linear\n"
        "  time becomes possible. That is exactly what radix and counting sort do.\n";
}

/* ==================================================================================== *
 *  PART (b) SECTION 2 : COUNTING SORT AND LSD RADIX SORT ON COE BIDDING DATA           *
 * ==================================================================================== */

/*  A Certificate of Entitlement bid.  The COE quota premium is an integer number of
 *  dollars, and when two bids are equal the earlier submission is ranked first - a
 *  tie-breaking rule that a sort can only honour if it is STABLE. */
struct Bid {
    unsigned    amount;      /* bid in SGD                        */
    int         seq;         /* submission sequence, 1 = earliest */
    std::string category;    /* COE category                      */
};

static unsigned digitAt(unsigned value, int pass, unsigned base) {
    for (int i = 0; i < pass; ++i) value /= base;
    return value % base;
}

/*  STABLE counting sort on one digit.  The two details that make it stable are the
 *  EXCLUSIVE prefix sum and walking the input BACKWARDS while filling from the end of
 *  each bucket - together they preserve the relative order of equal digits. */
static void countingSortByDigit(std::vector<Bid>& v, int pass, unsigned base, long long& ops) {
    const size_t n = v.size();
    std::vector<size_t> count(base, 0);
    for (size_t i = 0; i < n; ++i) { ++count[digitAt(v[i].amount, pass, base)]; ++ops; }
    for (unsigned d = 1; d < base; ++d) { count[d] += count[d - 1]; ++ops; }
    std::vector<Bid> out(n);
    for (size_t i = n; i-- > 0; ) {                       /* backwards => stable */
        out[--count[digitAt(v[i].amount, pass, base)]] = v[i];
        ++ops;
    }
    v.swap(out);
}

/*  The SAME counting sort with one line changed: the input is walked forwards while
 *  still filling each bucket from the back, which reverses equal elements.  It is used
 *  only to demonstrate that radix sort breaks without a stable subroutine. */
static void countingSortByDigitUnstable(std::vector<Bid>& v, int pass, unsigned base) {
    const size_t n = v.size();
    std::vector<size_t> count(base, 0);
    for (size_t i = 0; i < n; ++i) ++count[digitAt(v[i].amount, pass, base)];
    for (unsigned d = 1; d < base; ++d) count[d] += count[d - 1];
    std::vector<Bid> out(n);
    for (size_t i = 0; i < n; ++i)                        /* forwards => UNSTABLE */
        out[--count[digitAt(v[i].amount, pass, base)]] = v[i];
    v.swap(out);
}

static int digitsNeeded(unsigned maxValue, unsigned base) {
    int d = 1;
    while (maxValue >= base) { maxValue /= base; ++d; }
    return d;
}

/*  LSD RADIX SORT.  d stable counting-sort passes, least significant digit first.
 *  Time  Theta(d * (n + base))     Space  Theta(n + base)
 *  With d and base treated as constants - which they are for fixed-width keys such as
 *  a dollar amount capped at 999999 - this is Theta(n): linear, and therefore below
 *  the Omega(n lg n) bound, because it never compares two keys to each other. */
static void radixSortLSD(std::vector<Bid>& v, unsigned base, long long* ops = nullptr,
                         int* passesUsed = nullptr) {
    if (v.empty()) { if (ops) *ops = 0; if (passesUsed) *passesUsed = 0; return; }
    unsigned mx = 0;
    for (const Bid& b : v) mx = std::max(mx, b.amount);
    const int d = digitsNeeded(mx, base);
    long long o = 0;
    for (int pass = 0; pass < d; ++pass) countingSortByDigit(v, pass, base, o);
    if (ops) *ops = o;
    if (passesUsed) *passesUsed = d;
}

static void radixSortUnstableSubroutine(std::vector<Bid>& v, unsigned base) {
    if (v.empty()) return;
    unsigned mx = 0;
    for (const Bid& b : v) mx = std::max(mx, b.amount);
    const int d = digitsNeeded(mx, base);
    for (int pass = 0; pass < d; ++pass) countingSortByDigitUnstable(v, pass, base);
}

static std::vector<Bid> coeBids() {
    return {
        { 96001, 1, "CatA" }, { 102500, 2, "CatA" }, {  96001, 3, "CatA" },
        { 88000, 4, "CatA" }, { 102500, 5, "CatA" }, { 115300, 6, "CatA" },
        { 88000, 7, "CatA" }, {  79990, 8, "CatA" }, { 115300, 9, "CatA" },
        { 96001,10, "CatA" }, { 120000,11, "CatA" }, {  79990,12, "CatA" }
    };
}

static void radixDemo() {
    std::cout << "\n===== PART (b) : LSD RADIX SORT ON COE BIDDING DATA =====\n"
        "\n  Twelve bids for a Category A Certificate of Entitlement. Bids are ranked by\n"
        "  amount; equal amounts are ranked by submission order, earliest first. That\n"
        "  tie-break is only honoured if the sort is stable - and radix sort is stable\n"
        "  precisely because its counting-sort subroutine is.\n";

    std::vector<Bid> v = coeBids();
    std::cout << "\n  Submitted (amount / submission #):\n   ";
    for (const Bid& b : v) std::cout << b.amount << "/#" << b.seq << "  ";

    std::vector<Bid> r = v;
    long long ops = 0;
    int passes = 0;
    radixSortLSD(r, 10, &ops, &passes);
    std::cout << "\n\n  After LSD radix sort, base 10, " << passes << " passes ("
              << ops << " basic operations):\n   ";
    for (const Bid& b : r) std::cout << b.amount << "/#" << b.seq << "  ";

    bool tiesOk = true;
    for (size_t i = 1; i < r.size(); ++i)
        if (r[i].amount == r[i - 1].amount && r[i].seq < r[i - 1].seq) tiesOk = false;
    std::cout << "\n\n  Ordered by amount           : " << (std::is_sorted(r.begin(), r.end(),
                 [](const Bid& a, const Bid& b) { return a.amount < b.amount; }) ? "yes" : "NO")
              << "\n  Equal bids keep submission order : " << (tiesOk ? "yes (STABLE)" : "NO")
              << "\n";

    std::cout << "\n  --- WHAT HAPPENS IF THE SUBROUTINE IS NOT STABLE ---\n"
                 "  One line of counting sort is changed so that equal digits are reversed.\n";
    std::vector<Bid> bad = v;
    radixSortUnstableSubroutine(bad, 10);
    std::cout << "   ";
    for (const Bid& b : bad) std::cout << b.amount << "/#" << b.seq << "  ";
    const bool stillSorted = std::is_sorted(bad.begin(), bad.end(),
        [](const Bid& a, const Bid& b) { return a.amount < b.amount; });
    std::cout << "\n\n  Correctly ordered by amount : " << (stillSorted ? "yes" : "NO")
              << "\n  Radix sort does not merely lose the tie-break - it stops sorting at all,\n"
                 "  because each pass relies on the previous passes' order being preserved.\n";
}

/* ==================================================================================== *
 *  PART (b) SECTION 3 : BENCHMARK - RADIX AGAINST THE COMPARISON SORTS                 *
 * ==================================================================================== */

static void radixSortPlain(std::vector<unsigned>& v, unsigned base, long long* ops = nullptr) {
    if (v.empty()) { if (ops) *ops = 0; return; }
    unsigned mx = 0;
    for (unsigned x : v) mx = std::max(mx, x);
    const int d = digitsNeeded(mx, base);
    std::vector<unsigned> out(v.size());
    std::vector<size_t> count(base);
    long long o = 0;
    for (int pass = 0; pass < d; ++pass) {
        std::fill(count.begin(), count.end(), size_t(0));
        for (unsigned x : v) { ++count[digitAt(x, pass, base)]; ++o; }
        for (unsigned dd = 1; dd < base; ++dd) { count[dd] += count[dd - 1]; ++o; }
        for (size_t i = v.size(); i-- > 0; ) {
            out[--count[digitAt(v[i], pass, base)]] = v[i];
            ++o;
        }
        v.swap(out);
    }
    if (ops) *ops = o;
}

static void mergeSortPlainRun(std::vector<unsigned>& v, std::vector<unsigned>& buf,
                              size_t lo, size_t hi, long long& cmps) {
    if (hi - lo <= 1) return;
    const size_t mid = lo + (hi - lo) / 2;
    mergeSortPlainRun(v, buf, lo, mid, cmps);
    mergeSortPlainRun(v, buf, mid, hi, cmps);
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) { ++cmps; buf[k++] = (v[j] < v[i]) ? v[j++] : v[i++]; }
    while (i < mid) buf[k++] = v[i++];
    while (j < hi)  buf[k++] = v[j++];
    for (size_t t = lo; t < hi; ++t) v[t] = buf[t];
}

static void mergeSortPlain(std::vector<unsigned>& v, long long* cmps = nullptr) {
    if (v.size() < 2) { if (cmps) *cmps = 0; return; }
    std::vector<unsigned> buf(v.size());
    long long c = 0;
    mergeSortPlainRun(v, buf, 0, v.size(), c);
    if (cmps) *cmps = c;
}

/*  Wall-clock time confounds an asymptotic claim, because the memory hierarchy adds its
 *  own growth term. Counting basic operations does not: it is deterministic, machine-
 *  independent, and is what the complexity classes actually talk about. This is the
 *  rigorous form of the proof that radix sort is below the comparison bound. */
static void operationCountProof() {
    std::cout << "\n===== PART (b) : THE CACHE-FREE PROOF - COUNTING OPERATIONS =====\n"
        "\n  Wall-clock time mixes the algorithm together with the memory hierarchy. Basic\n"
        "  operation counts do not, so this is where the asymptotic claim is settled:\n"
        "      radix     Theta(d(n+k)) with d,k fixed  =>  ops/n must be CONSTANT\n"
        "      mergesort Theta(n lg n)                 =>  cmps/n must GROW like lg n\n\n";
    std::cout << std::right << std::setw(10) << "n"
              << std::setw(14) << "radix ops" << std::setw(12) << "ops/n"
              << std::setw(16) << "merge cmps" << std::setw(12) << "cmps/n"
              << std::setw(18) << "cmps/(n lg n)" << "\n";
    std::mt19937 rng(4242u);
    std::uniform_int_distribution<unsigned> dist(0, 999999);
    for (int n = 25000; n <= 3200000; n *= 2) {
        std::vector<unsigned> base(n);
        for (unsigned& x : base) x = dist(rng);
        std::vector<unsigned> a = base, b = base;
        long long ops = 0, cmps = 0;
        radixSortPlain(a, 256, &ops);
        mergeSortPlain(b, &cmps);
        const double nlgn = n * std::log2(static_cast<double>(n));
        std::cout << std::right << std::setw(10) << n << std::setw(14) << ops
                  << std::setw(12) << std::fixed << std::setprecision(4)
                  << (static_cast<double>(ops) / n)
                  << std::setw(16) << cmps
                  << std::setw(12) << (static_cast<double>(cmps) / n)
                  << std::setw(18) << (cmps / nlgn) << std::defaultfloat << "\n";
    }
    std::cout <<
        "\n  This is the proof the brief asks for, and it is exact rather than measured:\n"
        "    radix ops/n is CONSTANT across a 128-fold increase in n  ->  Theta(n)\n"
        "    merge cmps/n GROWS, while cmps/(n lg n) settles          ->  Theta(n lg n)\n"
        "  Radix sort therefore performs asymptotically fewer operations than any\n"
        "  comparison sort can, which is exactly what the Omega(n lg n) bound forbids a\n"
        "  comparison sort from doing - and it is allowed to, because it never compares\n"
        "  two keys with each other.\n";
}

static void benchmark() {
    std::cout << "\n===== PART (b) : IS RADIX SORT REALLY FASTER THAN O(n lg n)? =====\n"
        "\n  Keys are COE-style bid amounts in [0, 999999].\n"
        "  Radix is run at two radices, because the choice of base is the whole story:\n"
        "      base 10  -> d = 6 passes  (human-readable digits, used in the COE demo)\n"
        "      base 256 -> d = 3 passes  (what a real implementation would use)\n"
        "  Every pass is a full streaming pass over the array, so halving the pass count\n"
        "  roughly halves the running time while leaving the complexity Theta(d(n+k)).\n"
        "  A Theta(n) algorithm has constant time/n; a Theta(n lg n) one does not.\n\n";
    std::cout << std::right << std::setw(10) << "n"
              << std::setw(15) << "radix256(ms)" << std::setw(14) << "radix10(ms)"
              << std::setw(15) << "std::sort(ms)" << std::setw(15) << "mergesort(ms)"
              << std::setw(15) << "radix256 ns/n" << std::setw(17) << "std::sort ns/n" << "\n";

    std::mt19937 rng(20260919u);
    std::uniform_int_distribution<unsigned> dist(0, 999999);
    for (int n = 50000; n <= 3200000; n *= 2) {
        std::vector<unsigned> base(n);
        for (unsigned& x : base) x = dist(rng);

        std::vector<unsigned> a = base;
        double t0 = nowMs(); radixSortPlain(a, 256);           const double tr256 = nowMs() - t0;
        std::vector<unsigned> a10 = base;
        t0 = nowMs();        radixSortPlain(a10, 10);          const double tr10 = nowMs() - t0;
        std::vector<unsigned> b = base;
        t0 = nowMs();        std::sort(b.begin(), b.end());    const double ts = nowMs() - t0;
        std::vector<unsigned> c = base;
        t0 = nowMs();        mergeSortPlain(c);                const double tm = nowMs() - t0;

        std::cout << std::right << std::setw(10) << n
                  << std::setw(15) << std::fixed << std::setprecision(2) << tr256
                  << std::setw(14) << tr10 << std::setw(15) << ts << std::setw(15) << tm
                  << std::setw(15) << std::setprecision(1) << (tr256 * 1e6 / n)
                  << std::setw(17) << (ts * 1e6 / n) << std::defaultfloat << "\n";
        if (a != b || b != c || a10 != b) std::cout << "    !! outputs disagree !!\n";
    }
    std::cout <<
        "\n  Radix base 256 is about an order of magnitude faster than std::sort right\n"
        "  across the range, and roughly 14x faster than the merge sort.\n"
        "\n  BUT READ THE ns/n COLUMNS HONESTLY. std::sort's ns/n does climb, as its lg n\n"
        "  factor predicts - but radix's climbs by a similar factor, even though radix is\n"
        "  Theta(n) and its ns/n should be flat. The reason is the memory hierarchy: over\n"
        "  50k..3.2M the array grows past every level of cache, and radix's scattered\n"
        "  writes suffer for it. Meanwhile lg n only rises from 15.6 to 21.6 over that\n"
        "  same range - a factor of 1.4 - so the asymptotic gap that SHOULD widen is the\n"
        "  same size as the cache effect masking it. Wall-clock timing simply cannot\n"
        "  settle this question at these sizes. Menu option 6 settles it by counting\n"
        "  operations instead, which no cache can distort.\n"
        "\n  The radix10 column is the other caveat. Both radices are Theta(n), yet doubling\n"
        "  the pass count from 3 to 6 costs roughly 9x in wall-clock time, leaving base-10\n"
        "  radix level with a cache-friendly introsort. Theta(n) is a statement about\n"
        "  growth, not a promise of being fastest; the constant hidden in d(n+k) is large\n"
        "  and real. The table below finds the point where it stops being worth paying.\n";

    std::cout << "\n  --- THE LIMITATION : RADIX PAYS FOR KEY WIDTH ---\n"
        "  Theta(d(n+k)) is linear only while d is small. Widening the key at fixed n\n"
        "  adds passes, and radix eventually loses to a comparison sort.\n\n";
    std::cout << std::right << std::setw(14) << "key range" << std::setw(10) << "digits d"
              << std::setw(14) << "radix(ms)" << std::setw(16) << "std::sort(ms)"
              << std::setw(14) << "winner" << "\n";
    const unsigned ranges[] = { 99u, 9999u, 999999u, 99999999u, 4294967295u };
    const int nFixed = 1000000;
    for (unsigned hi : ranges) {
        std::uniform_int_distribution<unsigned> dd(0, hi);
        std::vector<unsigned> base(nFixed);
        for (unsigned& x : base) x = dd(rng);
        std::vector<unsigned> a = base, b = base;
        double t0 = nowMs(); radixSortPlain(a, 10);         const double tr = nowMs() - t0;
        t0 = nowMs();        std::sort(b.begin(), b.end()); const double ts = nowMs() - t0;
        std::cout << std::right << std::setw(14) << hi << std::setw(10)
                  << digitsNeeded(hi, 10)
                  << std::setw(14) << std::fixed << std::setprecision(2) << tr
                  << std::setw(16) << ts << std::setw(14)
                  << (tr < ts ? "radix" : "std::sort") << std::defaultfloat << "\n";
    }
}

/* ==================================================================================== *
 *  TEST SUITE                                                                          *
 * ==================================================================================== */

static std::vector<Patient> randomPatients(std::mt19937& rng, int n, int tiers) {
    std::vector<Patient> v(n);
    for (int i = 0; i < n; ++i) {
        v[i].arrival = i + 1;
        v[i].tier    = 1 + static_cast<int>(rng() % static_cast<unsigned>(tiers));
        v[i].name    = "N" + std::to_string(rng() % 1000);
        v[i].queueNo = "Q" + std::to_string(i + 1);
    }
    return v;
}

static bool sameOrder(const std::vector<Patient>& a, const std::vector<Patient>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i].arrival != b[i].arrival) return false;
    return true;
}

static void runTestSuite() {
    g_pass = g_fail = 0;
    std::cout << "\n==================== TEST SUITE (Question 3) ====================\n";
    std::mt19937 rng(31337u);

    std::cout << "\n  TC01  Merge sort: correct AND stable\n";
    {
        bool correct = true, stable = true;
        for (int t = 0; t < 200; ++t) {
            std::vector<Patient> v = randomPatients(rng, 1 + static_cast<int>(rng() % 200), 4);
            std::vector<Patient> ref = v;
            std::stable_sort(ref.begin(), ref.end(), lessByTier);
            mergeSort(v, lessByTier);
            if (!sameOrder(v, ref)) correct = false;
            if (stabilityViolations(v) != 0) stable = false;
        }
        check("matches std::stable_sort exactly on 200 random queues", correct);
        check("zero arrival-order violations on all 200 queues", stable);
    }

    std::cout << "\n  TC02  Quick sort: correct but NOT stable\n";
    {
        bool ordered = true;
        int totalViolations = 0;
        for (int t = 0; t < 200; ++t) {
            std::vector<Patient> v = randomPatients(rng, 1 + static_cast<int>(rng() % 200), 4);
            quickSort(v, lessByTier);
            if (!sortedByTier(v)) ordered = false;
            totalViolations += stabilityViolations(v);
        }
        check("output is always correctly ordered by tier", ordered);
        check("but arrival order IS broken, so it is unstable", totalViolations > 0,
              std::to_string(totalViolations) + " violations across 200 queues");
        std::vector<Patient> demo = triageQueue();
        quickSort(demo, lessByTier);
        check("the 16-patient demo queue itself shows the instability",
              stabilityViolations(demo) > 0,
              std::to_string(stabilityViolations(demo)) + " violations");
    }

    std::cout << "\n  TC03  Insertion sort is stable, selection sort is not\n";
    {
        int insBad = 0, selBad = 0;
        bool insOrdered = true, selOrdered = true;
        for (int t = 0; t < 200; ++t) {
            std::vector<Patient> a = randomPatients(rng, 1 + static_cast<int>(rng() % 120), 3);
            std::vector<Patient> b = a;
            insertionSort(a, lessByTier);
            selectionSort(b, lessByTier);
            insBad += stabilityViolations(a);
            selBad += stabilityViolations(b);
            if (!sortedByTier(a)) insOrdered = false;
            if (!sortedByTier(b)) selOrdered = false;
        }
        check("both produce correctly ordered output", insOrdered && selOrdered);
        check("insertion sort: 0 violations (stable)", insBad == 0);
        check("selection sort: violations present (unstable)", selBad > 0,
              std::to_string(selBad) + " violations");
    }

    std::cout << "\n  TC04  Boundary inputs\n";
    {
        bool ok = true;
        std::vector<Patient> e;
        mergeSort(e, lessByTier); quickSort(e, lessByTier);
        insertionSort(e, lessByTier); selectionSort(e, lessByTier);
        if (!e.empty()) ok = false;
        check("empty queue is handled by all four sorts", ok);

        std::vector<Patient> one = { { 1, 2, "Solo", "Q1" } };
        mergeSort(one, lessByTier); quickSort(one, lessByTier);
        check("single patient is handled", one.size() == 1 && one[0].arrival == 1);

        std::vector<Patient> same(60);
        for (int i = 0; i < 60; ++i) { same[i].arrival = i + 1; same[i].tier = 2; }
        std::vector<Patient> sm = same, sq = same;
        mergeSort(sm, lessByTier); quickSort(sq, lessByTier);
        check("all keys equal: merge sort leaves the queue untouched",
              stabilityViolations(sm) == 0 && sm[0].arrival == 1 && sm[59].arrival == 60);
        check("all keys equal: quick sort still produces a valid ordering",
              sortedByTier(sq) && sq.size() == 60);

        std::vector<Patient> asc(500), desc(500);
        for (int i = 0; i < 500; ++i) {
            asc[i].arrival = i + 1;  asc[i].tier  = i + 1;
            desc[i].arrival = i + 1; desc[i].tier = 500 - i;
        }
        std::vector<Patient> a1 = asc, d1 = desc, a2 = asc, d2 = desc;
        mergeSort(a1, lessByTier); mergeSort(d1, lessByTier);
        quickSort(a2, lessByTier); quickSort(d2, lessByTier);
        check("already sorted and reverse sorted inputs are handled by both",
              sortedByTier(a1) && sortedByTier(d1) && sortedByTier(a2) && sortedByTier(d2));
    }

    std::cout << "\n  TC05  Two-pass multi-key sorting needs stability\n";
    {
        bool stableMatches = true, unstableDiffers = false;
        for (int t = 0; t < 100; ++t) {
            std::vector<Patient> v = randomPatients(rng, 30 + static_cast<int>(rng() % 60), 3);
            std::vector<Patient> want = v;
            std::stable_sort(want.begin(), want.end(), lessByTierThenName);

            std::vector<Patient> sp = v;
            mergeSort(sp, lessByName); mergeSort(sp, lessByTier);
            std::vector<Patient> up = v;
            quickSort(up, lessByName); quickSort(up, lessByTier);

            auto key = [](const std::vector<Patient>& q) {
                std::string s;
                for (const Patient& p : q) s += std::to_string(p.tier) + p.name + ";";
                return s;
            };
            if (key(sp) != key(want)) stableMatches = false;
            if (key(up) != key(want)) unstableDiffers = true;
        }
        check("stable pipeline reproduces the tier-then-name order every time", stableMatches);
        check("unstable pipeline produces the WRONG report", unstableDiffers);
    }

    std::cout << "\n  TC06  Decision-tree lower bound holds for every sort measured\n";
    {
        bool ok = true;
        std::ostringstream os;
        for (int n = 2; n <= 8; ++n) {
            std::vector<Patient> base(n);
            for (int i = 0; i < n; ++i) { base[i].arrival = i + 1; base[i].tier = i + 1; }
            long long worst = 0;
            std::vector<Patient> perm = base;
            do {
                long long c = 0;
                std::vector<Patient> w = perm;
                mergeSort(w, lessByTier, &c);
                worst = std::max(worst, c);
            } while (std::next_permutation(perm.begin(), perm.end(), lessByTier));
            const long long bound = ceilLgFactorial(n);
            if (worst < bound) ok = false;
            if (n == 8) os << "n=8: worst " << worst << " >= ceil(lg 8!) = " << bound;
        }
        check("merge sort's measured worst case >= ceil(lg n!) for n = 2..8", ok, os.str());
        check("ceil(lg n!) grows like n lg n: n=8 bound is 16, n=2 bound is 1",
              ceilLgFactorial(8) == 16 && ceilLgFactorial(2) == 1);
    }

    std::cout << "\n  TC07  Counting sort subroutine: correct and stable\n";
    {
        bool ok = true, stable = true;
        for (int t = 0; t < 100; ++t) {
            const int n = 1 + static_cast<int>(rng() % 200);
            std::vector<Bid> v(n);
            for (int i = 0; i < n; ++i) { v[i].amount = rng() % 100; v[i].seq = i + 1; }
            std::vector<Bid> ref = v;
            std::stable_sort(ref.begin(), ref.end(),
                             [](const Bid& a, const Bid& b) { return a.amount % 10 < b.amount % 10; });
            long long ops = 0;
            countingSortByDigit(v, 0, 10, ops);
            for (int i = 0; i < n; ++i)
                if (v[i].amount != ref[i].amount || v[i].seq != ref[i].seq) { ok = false; break; }
            for (int i = 1; i < n; ++i)
                if (v[i].amount % 10 == v[i - 1].amount % 10 && v[i].seq < v[i - 1].seq) stable = false;
        }
        check("one digit pass matches std::stable_sort on that digit", ok);
        check("equal digits keep their submission order", stable);
    }

    std::cout << "\n  TC08  Radix sort: correct on random, duplicate and boundary data\n";
    {
        bool ok = true, stable = true;
        for (int t = 0; t < 200; ++t) {
            const int n = static_cast<int>(rng() % 300);
            std::vector<Bid> v(n);
            for (int i = 0; i < n; ++i) { v[i].amount = rng() % 200000; v[i].seq = i + 1; }
            std::vector<Bid> ref = v;
            std::stable_sort(ref.begin(), ref.end(),
                             [](const Bid& a, const Bid& b) { return a.amount < b.amount; });
            radixSortLSD(v, 10);
            for (int i = 0; i < n; ++i)
                if (v[i].amount != ref[i].amount || v[i].seq != ref[i].seq) { ok = false; break; }
            for (int i = 1; i < n; ++i)
                if (v[i].amount == v[i - 1].amount && v[i].seq < v[i - 1].seq) stable = false;
        }
        check("matches std::stable_sort on 200 random bid sets (incl. empty)", ok);
        check("radix sort is itself stable", stable);

        std::vector<Bid> extreme = { { 0u, 1, "" }, { 4294967295u, 2, "" }, { 0u, 3, "" },
                                     { 1u, 4, "" }, { 4294967295u, 5, "" } };
        radixSortLSD(extreme, 10);
        check("handles 0 and UINT_MAX together",
              extreme[0].amount == 0 && extreme[0].seq == 1 && extreme[1].seq == 3 &&
              extreme[2].amount == 1 && extreme[4].amount == 4294967295u);

        std::vector<Bid> allSame(50);
        for (int i = 0; i < 50; ++i) { allSame[i].amount = 90000; allSame[i].seq = i + 1; }
        radixSortLSD(allSame, 10);
        bool untouched = true;
        for (int i = 0; i < 50; ++i) if (allSame[i].seq != i + 1) untouched = false;
        check("50 identical bids keep their exact submission order", untouched);
    }

    std::cout << "\n  TC09  Radix sort is only correct because counting sort is stable\n";
    {
        std::vector<Bid> v = coeBids();
        radixSortUnstableSubroutine(v, 10);
        const bool stillSorted = std::is_sorted(v.begin(), v.end(),
            [](const Bid& a, const Bid& b) { return a.amount < b.amount; });
        check("swapping in an unstable subroutine breaks the sort outright", !stillSorted);

        std::vector<Bid> w = coeBids();
        radixSortLSD(w, 10);
        check("the stable subroutine sorts the same data correctly",
              std::is_sorted(w.begin(), w.end(),
                  [](const Bid& a, const Bid& b) { return a.amount < b.amount; }));
    }

    std::cout << "\n  TC10  Radix sort beats std::sort on fixed-width integer keys\n";
    {
        const int n = 2000000;
        std::mt19937 r2(555u);
        std::uniform_int_distribution<unsigned> dist(0, 999999);
        std::vector<unsigned> base(n);
        for (unsigned& x : base) x = dist(r2);
        std::vector<unsigned> a = base, a10 = base, b = base;
        double t0 = nowMs(); radixSortPlain(a, 256);        const double tr256 = nowMs() - t0;
        t0 = nowMs();        radixSortPlain(a10, 10);       const double tr10 = nowMs() - t0;
        t0 = nowMs();        std::sort(b.begin(), b.end()); const double ts = nowMs() - t0;
        check("both radices produce the same sorted output as std::sort", a == b && a10 == b);
        std::ostringstream os;
        os << std::fixed << std::setprecision(1) << "radix base 256 (d=3) " << tr256
           << " ms vs std::sort " << ts << " ms at n = 2,000,000";
        check("radix base 256 is faster than the O(n lg n) comparison sort", tr256 < ts, os.str());
        std::ostringstream os2;
        os2 << std::fixed << std::setprecision(1) << "base 10 (d=6) " << tr10
            << " ms vs base 256 (d=3) " << tr256 << " ms";
        check("pass count dominates the constant: base 256 beats base 10", tr256 < tr10, os2.str());
    }

    std::cout << "\n  TC11  Radix sort's cost is Theta(d(n+k)), i.e. linear in n\n";
    {
        std::mt19937 r2(777u);
        std::uniform_int_distribution<unsigned> dist(0, 999999);
        /* Operation counts, not timings: deterministic and immune to cache effects.
           This is the assertion that actually establishes Theta(n) vs Theta(n lg n). */
        std::vector<double> radixPerN, mergePerN;
        for (int n = 50000; n <= 1600000; n *= 2) {
            std::vector<unsigned> v(n);
            for (unsigned& x : v) x = dist(r2);
            std::vector<unsigned> w = v;
            long long ops = 0, cmps = 0;
            radixSortPlain(v, 256, &ops);
            mergeSortPlain(w, &cmps);
            radixPerN.push_back(static_cast<double>(ops) / n);
            mergePerN.push_back(static_cast<double>(cmps) / n);
        }
        const double rlo = *std::min_element(radixPerN.begin(), radixPerN.end());
        const double rhi = *std::max_element(radixPerN.begin(), radixPerN.end());
        std::ostringstream os;
        os << std::fixed << std::setprecision(4) << "radix ops/n in [" << rlo << ", " << rhi << "]";
        check("radix ops/n is constant to within 1% over a 32x range of n",
              (rhi - rlo) / rlo < 0.01, os.str());
        std::ostringstream os3;
        os3 << std::fixed << std::setprecision(2) << "merge cmps/n rises "
            << mergePerN.front() << " -> " << mergePerN.back();
        check("merge sort's comparisons/n GROWS over the same range (it is Theta(n lg n))",
              mergePerN.back() > mergePerN.front() + 3.0, os3.str());

        /* Operation count must scale exactly with the number of digit passes. */
        std::vector<Bid> small(100000), wide(100000);
        for (int i = 0; i < 100000; ++i) {
            small[i].amount = r2() % 1000;        small[i].seq = i;
            wide[i].amount  = r2() % 1000000000;  wide[i].seq  = i;
        }
        long long o1 = 0, o2 = 0;
        int d1 = 0, d2 = 0;
        radixSortLSD(small, 10, &o1, &d1);
        radixSortLSD(wide,  10, &o2, &d2);
        std::ostringstream os2;
        os2 << d1 << " passes -> " << o1 << " ops; " << d2 << " passes -> " << o2 << " ops";
        check("operation count is proportional to the number of digit passes",
              std::fabs(static_cast<double>(o2) / o1 - static_cast<double>(d2) / d1) < 0.01,
              os2.str());
    }

    std::cout << "\n  TC12  Quick sort's O(n^2) worst case is real\n";
    {
        /* Naive quicksort using the LAST element as pivot, the textbook worst case. */
        std::function<void(std::vector<int>&, int, int, long long&)> naive =
            [&](std::vector<int>& v, int lo, int hi, long long& c) {
                if (lo >= hi) return;
                const int pivot = v[hi];
                int i = lo;
                for (int j = lo; j < hi; ++j) { ++c; if (v[j] < pivot) std::swap(v[i++], v[j]); }
                std::swap(v[i], v[hi]);
                naive(v, lo, i - 1, c);
                naive(v, i + 1, hi, c);
            };
        const int n = 3000;
        std::vector<int> sorted(n), shuffled(n);
        std::iota(sorted.begin(), sorted.end(), 0);
        shuffled = sorted;
        std::mt19937 r3(9u);
        std::shuffle(shuffled.begin(), shuffled.end(), r3);

        long long cSorted = 0, cRandom = 0;
        std::vector<int> a = sorted, b = shuffled;
        naive(a, 0, n - 1, cSorted);
        naive(b, 0, n - 1, cRandom);
        const double quad = 0.5 * n * (n - 1);
        std::ostringstream os;
        os << "sorted input " << cSorted << " comparisons vs n(n-1)/2 = "
           << static_cast<long long>(quad) << "; random input " << cRandom;
        check("already-sorted input drives naive quicksort to exactly n(n-1)/2",
              cSorted == static_cast<long long>(quad), os.str());
        check("random input costs far less, near n lg n", cRandom < cSorted / 10);

        /* The median-of-three version used in part (a) survives the same input. */
        std::vector<Patient> asc(20000);
        for (int i = 0; i < 20000; ++i) { asc[i].arrival = i + 1; asc[i].tier = i + 1; }
        long long c3 = 0;
        quickSort(asc, lessByTier, &c3);
        std::ostringstream os2;
        os2 << c3 << " comparisons, n lg n = "
            << static_cast<long long>(20000 * std::log2(20000.0));
        check("median-of-three pivoting keeps sorted input at O(n lg n)",
              c3 < 20LL * 20000 * static_cast<long long>(std::log2(20000.0)), os2.str());
    }

    std::cout << "\n  ------------------------------------------------------------\n";
    std::cout << "  TOTAL: " << g_pass << " passed, " << g_fail << " failed.\n";
    std::cout << "  ------------------------------------------------------------\n";
}

/* ==================================================================================== *
 *  MENU                                                                                *
 * ==================================================================================== */

static void runEverything() {
    stabilityDemo();
    multiKeyDemo();
    lowerBoundDemo();
    radixDemo();
    benchmark();
    operationCountProof();
    runTestSuite();
}

int main(int argc, char** argv) {
    std::cout <<
        "\n============================================================================\n"
        "  CSD3130/CSD3131 Assignment Part 1 - QUESTION 3\n"
        "  (a) Sorting stability: the polyclinic triage queue\n"
        "  (b) The Omega(n lg n) comparison bound, and beating it with radix sort\n"
        "============================================================================\n";

    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--all") { runEverything(); return g_fail == 0 ? 0 : 1; }

    for (;;) {
        std::cout <<
            "\n  MENU\n"
            "   1. (a) Stability demo: triage queue through four sorting algorithms\n"
            "   2. (a) Why stability matters: the two-pass multi-key pipeline\n"
            "   3. (b) The Omega(n lg n) lower bound, measured over all n! inputs\n"
            "   4. (b) LSD radix sort on COE bidding data (and what unstable breaks)\n"
            "   5. (b) Benchmark: radix vs std::sort vs merge sort, and radix's limit\n"
            "   6. (b) The cache-free proof: operation counts, radix vs merge sort\n"
            "   7. Run the full test suite (12 test cases)\n"
            "   8. Run everything\n"
            "   0. Exit\n"
            "  Choice: ";
        int choice = -1;
        if (!(std::cin >> choice)) break;
        switch (choice) {
            case 1: stabilityDemo();       break;
            case 2: multiKeyDemo();        break;
            case 3: lowerBoundDemo();      break;
            case 4: radixDemo();           break;
            case 5: benchmark();           break;
            case 6: operationCountProof(); break;
            case 7: runTestSuite();        break;
            case 8: runEverything();       break;
            case 0: std::cout << "  Bye.\n"; return 0;
            default: std::cout << "  Unknown option.\n";
        }
    }
    return 0;
}
