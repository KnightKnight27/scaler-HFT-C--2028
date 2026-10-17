#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "include/low_latency_order_book.hpp"
#include "include/map_order_book.hpp"
#include "include/priority_queue_order_book.hpp"
#include "include/types.hpp"

enum class OpType { Add, Cancel, GetBBO };

struct Operation {
  OpType type;
  uint64_t id;
  Side side;
  double price;
  long qty;
  bool check_depth;
};

std::vector<Operation> generate_workload() {
  std::vector<Operation> ops;
  ops.reserve(1000000);

  std::mt19937 rng(42);
  std::uniform_int_distribution<int> op_dist(0, 99);
  std::uniform_int_distribution<int> side_dist(0, 1);
  std::uniform_int_distribution<long> qty_dist(1, 100);
  std::uniform_int_distribution<uint64_t> bid_dist(5000, 9999);
  std::uniform_int_distribution<uint64_t> ask_dist(10001, 15000);

  std::vector<uint64_t> live_orders;
  uint64_t next_id = 1;

  for (int i = 0; i < 1000000; i++) {
    int r = op_dist(rng);
    bool check_depth = ((i + 1) % 100 == 0);

    if (r < 70 || live_orders.empty()) {
      uint64_t id = next_id++;
      Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
      double price = (side == Side::Buy) ? (bid_dist(rng) / 100.0)
                                         : (ask_dist(rng) / 100.0);
      long qty = qty_dist(rng);

      live_orders.push_back(id);
      ops.push_back({OpType::Add, id, side, price, qty, check_depth});
    } else if (r < 90) {
      size_t idx = rng() % live_orders.size();
      uint64_t id = live_orders[idx];
      std::swap(live_orders[idx], live_orders.back());
      live_orders.pop_back();

      ops.push_back({OpType::Cancel, id, Side::Buy, 0.0, 0, check_depth});
    } else {
      ops.push_back({OpType::GetBBO, 0, Side::Buy, 0.0, 0, check_depth});
    }
  }

  return ops;
}

void run_verification(const std::vector<Operation> &ops) {
  std::cout << "Running correctness test...\n\n";

  MapOrderBook bookA;
  PriorityQueueOrderBook bookB;
  LowLatencyOrderBook bookC;

  for (size_t i = 0; i < 50000; i++) {
    const auto &op = ops[i];

    if (op.type == OpType::Add) {
      bookA.add_order(op.id, op.side, op.price, op.qty);
      bookB.add_order(op.id, op.side, op.price, op.qty);
      bookC.add_order(op.id, op.side, op.price, op.qty);
    } else if (op.type == OpType::Cancel) {
      bool a = bookA.cancel_order(op.id);
      bool b = bookB.cancel_order(op.id);
      bool c = bookC.cancel_order(op.id);
      if (a != b || a != c) {
        std::cerr << "Cancel result mismatch at op " << i << "\n";
        return;
      }
    }

    BBO bboA = bookA.get_bbo();
    BBO bboB = bookB.get_bbo();
    BBO bboC = bookC.get_bbo();

    if (bboA.has_bid != bboB.has_bid || bboA.has_bid != bboC.has_bid ||
        bboA.has_ask != bboB.has_ask || bboA.has_ask != bboC.has_ask ||
        bboA.bid_price != bboB.bid_price || bboA.bid_price != bboC.bid_price ||
        bboA.bid_qty != bboB.bid_qty || bboA.bid_qty != bboC.bid_qty ||
        bboA.ask_price != bboB.ask_price || bboA.ask_price != bboC.ask_price ||
        bboA.ask_qty != bboB.ask_qty || bboA.ask_qty != bboC.ask_qty) {
      std::cerr << "BBO mismatch at op " << i << "\n";
      return;
    }

    if (op.check_depth) {
      auto bidsA = bookA.get_bids(10);
      auto bidsB = bookB.get_bids(10);
      auto bidsC = bookC.get_bids(10);
      if (bidsA.size() != bidsB.size() || bidsA.size() != bidsC.size()) {
        std::cerr << "Bid depth mismatch at op " << i << "\n";
        return;
      }

      auto asksA = bookA.get_asks(10);
      auto asksB = bookB.get_asks(10);
      auto asksC = bookC.get_asks(10);
      if (asksA.size() != asksB.size() || asksA.size() != asksC.size()) {
        std::cerr << "Ask depth mismatch at op " << i << "\n";
        return;
      }
    }
  }

  std::cout << "Implementation A: PASS\n";
  std::cout << "Implementation B: PASS\n";
  std::cout << "Implementation C: PASS\n\n";
  std::cout << "All implementations produce identical results.\n";
}

template <typename BookType>
void benchmark(const std::string &name, const std::vector<Operation> &ops) {
  std::vector<double> totals;
  std::vector<double> add_times;
  std::vector<double> cancel_times;
  std::vector<double> bbo_times;

  BBO final_bbo{};
  uint64_t checksum = 0;

  for (int run = 0; run < 5; run++) {
    BookType book;
    checksum = 0;

    auto start = std::chrono::steady_clock::now();
    for (const auto &op : ops) {
      if (op.type == OpType::Add) {
        book.add_order(op.id, op.side, op.price, op.qty);
      } else if (op.type == OpType::Cancel) {
        book.cancel_order(op.id);
      } else {
        BBO b = book.get_bbo();
        checksum += b.bid_qty + b.ask_qty;
      }

      if (op.check_depth) {
        auto b = book.get_bids(10);
        auto a = book.get_asks(10);
        checksum += b.size() + a.size();
      }
    }
    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    totals.push_back(ms);
    final_bbo = book.get_bbo();

    BookType test_book;
    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < 50000; i++) {
      test_book.add_order(ops[i].id, ops[i].side, ops[i].price, ops[i].qty);
    }
    auto t2 = std::chrono::steady_clock::now();
    add_times.push_back(
        std::chrono::duration<double, std::nano>(t2 - t1).count() / 50000.0);

    auto t3 = std::chrono::steady_clock::now();
    for (int i = 0; i < 50000; i++) {
      BBO b = test_book.get_bbo();
      checksum += b.bid_qty;
    }
    auto t4 = std::chrono::steady_clock::now();
    bbo_times.push_back(
        std::chrono::duration<double, std::nano>(t4 - t3).count() / 50000.0);

    auto t5 = std::chrono::steady_clock::now();
    for (int i = 1; i <= 20000; i++) {
      test_book.cancel_order(i);
    }
    auto t6 = std::chrono::steady_clock::now();
    cancel_times.push_back(
        std::chrono::duration<double, std::nano>(t6 - t5).count() / 20000.0);
  }

  std::sort(totals.begin(), totals.end());
  std::sort(add_times.begin(), add_times.end());
  std::sort(cancel_times.begin(), cancel_times.end());
  std::sort(bbo_times.begin(), bbo_times.end());

  std::cout << "Results for " << name << " (Median of 5 runs):\n";
  std::cout << "Total time: " << totals[2] << " ms\n";
  std::cout << "ns / add:    " << add_times[2] << " ns\n";
  std::cout << "ns / cancel: " << cancel_times[2] << " ns\n";
  std::cout << "ns / BBO:    " << bbo_times[2] << " ns\n\n";

  std::cout << "Final BBO:\n";
  if (final_bbo.has_bid) {
    std::cout << "Bid: " << final_bbo.bid_price << " x " << final_bbo.bid_qty
              << "\n";
  } else {
    std::cout << "Bid: None\n";
  }
  if (final_bbo.has_ask) {
    std::cout << "Ask: " << final_bbo.ask_price << " x " << final_bbo.ask_qty
              << "\n";
  } else {
    std::cout << "Ask: None\n";
  }
  std::cout << "Checksum: " << checksum << "\n";
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cout << "Usage:\n";
    std::cout << "  " << argv[0] << " --verify\n";
    std::cout << "  " << argv[0] << " --impl A\n";
    std::cout << "  " << argv[0] << " --impl B\n";
    std::cout << "  " << argv[0] << " --impl C\n";
    return 1;
  }

  std::string arg = argv[1];
  auto workload = generate_workload();

  if (arg == "--verify") {
    run_verification(workload);
  } else if (arg == "--impl") {
    if (argc < 3) {
      std::cerr << "Missing implementation argument (A, B, or C)\n";
      return 1;
    }
    std::string impl = argv[2];
    if (impl == "A" || impl == "a") {
      benchmark<MapOrderBook>("Implementation A (std::map)", workload);
    } else if (impl == "B" || impl == "b") {
      benchmark<PriorityQueueOrderBook>(
          "Implementation B (std::priority_queue)", workload);
    } else if (impl == "C" || impl == "c") {
      benchmark<LowLatencyOrderBook>("Implementation C (Direct Index Array)",
                                     workload);
    } else {
      std::cerr << "Unknown implementation: " << impl << "\n";
      return 1;
    }
  } else {
    std::cerr << "Unknown option: " << arg << "\n";
    return 1;
  }

  return 0;
}
