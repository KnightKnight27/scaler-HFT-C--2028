#include "orderBookArray.hpp"
#include "orderBookHeap.hpp"
#include "orderBookMap.hpp"
#include "types.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace std;

enum class OpType { Add, Cancel, GetBBO };

struct Operation {
  OpType type;
  uint64_t id;
  Side side;
  double price;
  long qty;
};

vector<Operation> generateWorkload(size_t totalOps, unsigned int seed = 42) {
  mt19937 rng(seed);
  uniform_int_distribution<int> opDist(1, 100);
  uniform_int_distribution<int> sideDist(0, 1);
  uniform_int_distribution<long> qtyDist(1, 100);
  uniform_int_distribution<int> bidTickDist(0, 4999);     // 50.00 to 99.99
  uniform_int_distribution<int> askTickDist(5001, 10000); // 100.01 to 150.00

  vector<Operation> ops;
  ops.reserve(totalOps);

  vector<uint64_t> liveOrders;
  uint64_t nextId = 1;

  for (size_t i = 0; i < totalOps; ++i) {
    int roll = opDist(rng);

    if (roll <= 70 || liveOrders.empty()) {
      Side side = (sideDist(rng) == 0) ? Side::Buy : Side::Sell;
      int tick = (side == Side::Buy) ? bidTickDist(rng) : askTickDist(rng);
      double price = tickToPrice(tick);
      long qty = qtyDist(rng);
      uint64_t id = nextId++;

      Operation op{OpType::Add, id, side, price, qty};
      ops.push_back(op);
      liveOrders.push_back(id);
    } else if (roll <= 90) {
      size_t lastIndex = liveOrders.size() - 1;
      uniform_int_distribution<size_t> idxDist(0, lastIndex);
      size_t idx = idxDist(rng);
      uint64_t idToCancel = liveOrders[idx];

      liveOrders[idx] = liveOrders[lastIndex];
      liveOrders.pop_back();

      Operation op{OpType::Cancel, idToCancel, Side::Buy, 0.0, 0};
      ops.push_back(op);
    } else {
      Operation op{OpType::GetBBO, 0, Side::Buy, 0.0, 0};
      ops.push_back(op);
    }
  }

  return ops;
}

inline bool priceEqual(double a, double b) { return fabs(a - b) < 0.0001; }

bool bboEqual(const BBO &a, const BBO &b) {
  if (a.hasBid != b.hasBid || a.hasAsk != b.hasAsk) {
    return false;
  }

  if (a.hasBid) {
    bool priceMatches = priceEqual(a.bidPrice, b.bidPrice);
    bool qtyMatches = (a.bidQty == b.bidQty);
    if (!priceMatches || !qtyMatches) {
      return false;
    }
  }

  if (a.hasAsk) {
    bool priceMatches = priceEqual(a.askPrice, b.askPrice);
    bool qtyMatches = (a.askQty == b.askQty);
    if (!priceMatches || !qtyMatches) {
      return false;
    }
  }

  return true;
}

bool levelsEqual(const vector<Level> &a, const vector<Level> &b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    bool priceMatches = priceEqual(a[i].price, b[i].price);
    bool qtyMatches = (a[i].qty == b[i].qty);
    if (!priceMatches || !qtyMatches) {
      return false;
    }
  }
  return true;
}

unique_ptr<OrderBook> makeBook(char implType) {
  if (implType == 'A')
    return make_unique<OrderBookMap>();
  if (implType == 'B')
    return make_unique<OrderBookHeap>();
  return make_unique<OrderBookArray>();
}

bool checkCase(bool cond, char implType, const string &what) {
  if (!cond)
    cerr << "  impl " << implType << " failed: " << what << endl;
  return cond;
}

// the small cases from the brief, run on each book by itself
bool edgeCases(char implType) {
  auto book = makeBook(implType);
  bool ok = true;

  BBO empty = book->getBBO();
  ok &= checkCase(!empty.hasBid && !empty.hasAsk, implType, "empty book bbo");
  ok &= checkCase(book->getBids(10).empty() && book->getAsks(10).empty(),
                  implType, "empty book depth");
  ok &= checkCase(!book->cancelOrder(42), implType, "cancel on empty book");

  book->addOrder(1, Side::Buy, 99.50, 10);
  book->addOrder(2, Side::Buy, 99.50, 5);
  book->addOrder(3, Side::Buy, 98.00, 7);
  book->addOrder(4, Side::Buy, 97.25, 1);
  book->addOrder(5, Side::Sell, 100.01, 3);

  auto bids = book->getBids(10);
  ok &= checkCase(bids.size() == 3, implType, "only 3 bid levels returned");
  ok &= checkCase(bids.size() == 3 && bids[0].qty == 15 &&
                      priceEqual(bids[0].price, 99.50),
                  implType, "same price orders summed");

  ok &= checkCase(book->cancelOrder(1), implType, "cancel live order");
  ok &= checkCase(!book->cancelOrder(1), implType, "cancel same order twice");
  ok &= checkCase(!book->cancelOrder(99999999), implType, "cancel unknown id");
  ok &= checkCase(book->getBBO().bidQty == 5, implType,
                  "qty after partial level cancel");

  ok &= checkCase(book->cancelOrder(2), implType,
                  "cancel last order at best bid");
  BBO b = book->getBBO();
  ok &= checkCase(b.hasBid && priceEqual(b.bidPrice, 98.00), implType,
                  "best bid moves down");

  ok &= checkCase(book->cancelOrder(5), implType, "cancel only ask");
  ok &= checkCase(!book->getBBO().hasAsk, implType, "ask side empty again");

  // ids far past the benchmark range take a different path in impl c
  book->addOrder(5000000000ULL, Side::Sell, 120.00, 9);
  ok &= checkCase(book->getBBO().hasAsk && book->getBBO().askQty == 9, implType,
                  "very large id");
  ok &= checkCase(book->cancelOrder(5000000000ULL), implType,
                  "cancel very large id");

  return ok;
}

bool runVerification() {
  cout << "running correctness test...\n" << endl;

  bool edgeA = edgeCases('A');
  bool edgeB = edgeCases('B');
  bool edgeC = edgeCases('C');
  bool edgeOk = edgeA && edgeB && edgeC;

  OrderBookMap bookA;
  OrderBookHeap bookB;
  OrderBookArray bookC;

  const size_t testOpsCount = 100000;
  auto workload = generateWorkload(testOpsCount, 1337);
  bool agree = true;

  for (size_t step = 0; step < workload.size() && agree; ++step) {
    const auto &op = workload[step];

    if (op.type == OpType::Add) {
      bookA.addOrder(op.id, op.side, op.price, op.qty);
      bookB.addOrder(op.id, op.side, op.price, op.qty);
      bookC.addOrder(op.id, op.side, op.price, op.qty);
    } else if (op.type == OpType::Cancel) {
      bool resA = bookA.cancelOrder(op.id);
      bool resB = bookB.cancelOrder(op.id);
      bool resC = bookC.cancelOrder(op.id);
      if (!(resA && resB && resC)) {
        cerr << "FAIL: cancel of a live order returned false at step " << step
             << endl;
        agree = false;
      }
    }

    // bbo and both sides of depth after every single op
    BBO bboA = bookA.getBBO();
    if (!bboEqual(bboA, bookB.getBBO()) || !bboEqual(bboA, bookC.getBBO())) {
      cerr << "FAIL: BBO mismatch at step " << step << endl;
      agree = false;
    }

    auto bidsA = bookA.getBids(10);
    if (!levelsEqual(bidsA, bookB.getBids(10)) ||
        !levelsEqual(bidsA, bookC.getBids(10))) {
      cerr << "FAIL: bid levels mismatch at step " << step << endl;
      agree = false;
    }

    auto asksA = bookA.getAsks(10);
    if (!levelsEqual(asksA, bookB.getAsks(10)) ||
        !levelsEqual(asksA, bookC.getAsks(10))) {
      cerr << "FAIL: ask levels mismatch at step " << step << endl;
      agree = false;
    }
  }

  cout << "edge cases (empty book, unknown and double cancel, short depth, "
          "large ids): "
       << (edgeOk ? "PASS" : "FAIL") << endl;
  cout << "random workload, " << testOpsCount
       << " ops, bbo + 10 levels checked after every op: "
       << (agree ? "PASS" : "FAIL") << "\n"
       << endl;

  bool ok = edgeOk && agree;
  cout << "implementation a: " << (edgeA && agree ? "PASS" : "FAIL") << endl;
  cout << "implementation b: " << (edgeB && agree ? "PASS" : "FAIL") << endl;
  cout << "implementation c: " << (edgeC && agree ? "PASS" : "FAIL") << endl;
  if (ok)
    cout << "\nall implementations produce identical results." << endl;
  return ok;
}

inline uint64_t nsSince(chrono::steady_clock::time_point t0) {
  auto t1 = chrono::steady_clock::now();
  return chrono::duration_cast<chrono::nanoseconds>(t1 - t0).count();
}

struct BenchmarkResult {
  double totalMs;
  double nsPerAdd;
  double nsPerCancel;
  double nsPerBBO;
  uint64_t checksum;
  BBO finalBBO;
};

// timed = false skips the per-op clock reads, used for the perf runs
BenchmarkResult benchmarkSingleRun(char implType,
                                   const vector<Operation> &workload,
                                   bool timed) {
  unique_ptr<OrderBook> book = makeBook(implType);

  size_t addCnt = 0;
  size_t cancelCnt = 0;
  size_t bboCnt = 0;

  uint64_t totalAddNs = 0;
  uint64_t totalCancelNs = 0;
  uint64_t totalBboNs = 0;

  uint64_t checksum = 0;

  auto startTotal = chrono::steady_clock::now();

  for (size_t i = 0; i < workload.size(); ++i) {
    const auto &op = workload[i];

    if (op.type == OpType::Add) {
      auto t0 = timed ? chrono::steady_clock::now() : startTotal;
      book->addOrder(op.id, op.side, op.price, op.qty);
      if (timed)
        totalAddNs += nsSince(t0);
      addCnt++;

    } else if (op.type == OpType::Cancel) {
      auto t0 = timed ? chrono::steady_clock::now() : startTotal;
      bool ok = book->cancelOrder(op.id);
      if (timed)
        totalCancelNs += nsSince(t0);

      if (ok)
        checksum += op.id;

      cancelCnt++;

    } else {
      auto t0 = timed ? chrono::steady_clock::now() : startTotal;
      BBO b = book->getBBO();
      if (timed)
        totalBboNs += nsSince(t0);

      if (b.hasBid)
        checksum += b.bidQty;
      if (b.hasAsk)
        checksum += b.askQty;

      bboCnt++;
    }

    // depth queries every 100 ops
    if (i % 100 == 0) {
      auto bids = book->getBids(10);
      auto asks = book->getAsks(10);
      for (const auto &level : bids)
        checksum += level.qty;
      for (const auto &level : asks)
        checksum += level.qty;
    }
  }

  auto endTotal = chrono::steady_clock::now();
  double totalMs =
      chrono::duration<double, milli>(endTotal - startTotal).count();

  BBO finalBBO = book->getBBO();

  double nsPerAdd = (addCnt > 0) ? ((double)totalAddNs / addCnt) : 0.0;
  double nsPerCancel =
      (cancelCnt > 0) ? ((double)totalCancelNs / cancelCnt) : 0.0;
  double nsPerBBO = (bboCnt > 0) ? ((double)totalBboNs / bboCnt) : 0.0;

  BenchmarkResult result{totalMs,  nsPerAdd, nsPerCancel,
                         nsPerBBO, checksum, finalBBO};
  return result;
}

// run 5 times and pick median
BenchmarkResult runBenchmarks(char implType, const vector<Operation> &workload,
                              int runs = 5, bool timed = true) {
  vector<BenchmarkResult> results;
  results.reserve(runs);

  for (int r = 0; r < runs; ++r) {
    BenchmarkResult res = benchmarkSingleRun(implType, workload, timed);
    results.push_back(res);
  }

  sort(results.begin(), results.end(),
       [](const BenchmarkResult &a, const BenchmarkResult &b) {
         return a.totalMs < b.totalMs;
       });

  size_t medianIndex = runs / 2;
  return results[medianIndex];
}

void printResult(const string &name, const BenchmarkResult &res) {
  cout << "\n[" << name << "]\n";
  cout << fixed << setprecision(3);
  cout << "  total time  : " << res.totalMs << " ms\n";
  cout << "  ns / add    : " << res.nsPerAdd << " ns\n";
  cout << "  ns / cancel : " << res.nsPerCancel << " ns\n";
  cout << "  ns / bbo    : " << res.nsPerBBO << " ns\n";
  if (res.finalBBO.hasBid) {
    cout << "  final bid   : " << fixed << setprecision(2)
         << res.finalBBO.bidPrice << " x " << res.finalBBO.bidQty << "\n";
  } else {
    cout << "  final bid   : none\n";
  }
  if (res.finalBBO.hasAsk) {
    cout << "  final ask   : " << fixed << setprecision(2)
         << res.finalBBO.askPrice << " x " << res.finalBBO.askQty << "\n";
  } else {
    cout << "  final ask   : none\n";
  }
  cout << "  checksum    : " << res.checksum << "\n";
}

int main(int argc, char *argv[]) {
  string mode = "--all";
  char impl = 'C';
  int runs = 5;
  bool timed = true;

  for (int i = 1; i < argc; ++i) {
    string arg = argv[i];
    if (arg == "--impl" && i + 1 < argc) {
      mode = "--impl";
      impl = toupper(argv[++i][0]);
    } else if (arg == "--runs" && i + 1 < argc) {
      runs = max(1, atoi(argv[++i]));
    } else if (arg == "--untimed") {
      timed = false;
    } else {
      mode = arg;
    }
  }

  if (mode == "--verify") {
    bool ok = runVerification();
    return ok ? 0 : 1;
  }

  const size_t totalOps = 1000000;
  cout << "generating 1,000,000 operations workload (seed 42)..." << endl;
  auto workload = generateWorkload(totalOps, 42);
  cout << "workload generated.\n" << endl;

  // baseline for perf: same process work minus the order book
  if (mode == "--gen-only") {
    cout << "ops generated: " << workload.size() << endl;
    return 0;
  }

  if (mode == "--impl") {
    string name = (impl == 'A')   ? "Implementation A (std::map)"
                  : (impl == 'B') ? "Implementation B (priority_queue)"
                                  : "Implementation C (Flat Array)";

    cout << "running " << runs << " benchmark iterations for " << name
         << (timed ? "" : " (untimed)") << "..." << endl;
    BenchmarkResult res = runBenchmarks(impl, workload, runs, timed);
    printResult(name, res);
  } else {
    cout << "running comparative benchmark suite (" << runs
         << " runs each)...\n"
         << endl;

    cout << "benchmarking implementation a (map)..." << endl;
    BenchmarkResult resA = runBenchmarks('A', workload, runs);

    cout << "benchmarking implementation b (heap)..." << endl;
    BenchmarkResult resB = runBenchmarks('B', workload, runs);

    cout << "benchmarking implementation c (flat array)..." << endl;
    BenchmarkResult resC = runBenchmarks('C', workload, runs);

    printResult("Implementation A (std::map)", resA);
    printResult("Implementation B (priority_queue)", resB);
    printResult("Implementation C (Flat Array)", resC);

    cout << "\nsummary comparison (median of " << runs
         << " runs, 1,000,000 ops):\n";
    cout << left << setw(20) << "metric" << setw(16) << "impl a (map)"
         << setw(16) << "impl b (heap)" << setw(16) << "impl c (array)" << "\n";
    cout << left << setw(20) << "total time (ms)" << setw(16) << resA.totalMs
         << setw(16) << resB.totalMs << setw(16) << resC.totalMs << "\n";
    cout << left << setw(20) << "ns / add" << setw(16) << resA.nsPerAdd
         << setw(16) << resB.nsPerAdd << setw(16) << resC.nsPerAdd << "\n";
    cout << left << setw(20) << "ns / cancel" << setw(16) << resA.nsPerCancel
         << setw(16) << resB.nsPerCancel << setw(16) << resC.nsPerCancel
         << "\n";
    cout << left << setw(20) << "ns / bbo" << setw(16) << resA.nsPerBBO
         << setw(16) << resB.nsPerBBO << setw(16) << resC.nsPerBBO << "\n";
  }

  return 0;
}
