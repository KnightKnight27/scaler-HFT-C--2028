#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "common.hpp"
#include "fixed_order_bk.hpp"
#include "map_order_bk.hpp"
#include "pq_order_bk.hpp"

enum class OpType { Add, Cancel, BBO, Depth };

struct Operation {
  OpType type;
  uint64_t id;
  Side side;
  int price;
  long qty;
};

std::string format_price(int tick) {
  int whole = tick / 100;
  int frac = std::abs(tick % 100);
  std::string s = std::to_string(whole) + ".";
  if (frac < 10)
    s += "0";
  s += std::to_string(frac);
  return s;
}

std::vector<Operation> make_workload(int total_ops = 1000000) {
  std::vector<Operation> ops;
  ops.reserve(total_ops + total_ops / 50);

  std::mt19937 rng(42);
  std::uniform_int_distribution<int> side_dist(0, 1);
  std::uniform_int_distribution<int> bid_dist(MIN_BID_TICK, MAX_BID_TICK);
  std::uniform_int_distribution<int> ask_dist(MIN_ASK_TICK, MAX_ASK_TICK);
  std::uniform_int_distribution<long> qty_dist(1, 100);

  std::vector<uint64_t> live_orders;
  uint64_t next_id = 1;

  for (int i = 0; i < total_ops; ++i) {
    int p = rng() % 100;
    if (p < 70 || live_orders.empty()) {
      uint64_t id = next_id++;
      Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
      int price = (side == Side::Buy) ? bid_dist(rng) : ask_dist(rng);
      long qty = qty_dist(rng);

      ops.push_back({OpType::Add, id, side, price, qty});
      live_orders.push_back(id);
    } else if (p < 90) {
      size_t idx = rng() % live_orders.size();
      uint64_t id = live_orders[idx];
      live_orders[idx] = live_orders.back();
      live_orders.pop_back();

      ops.push_back({OpType::Cancel, id, Side::Buy, 0, 0});
    } else {
      ops.push_back({OpType::BBO, 0, Side::Buy, 0, 0});
    }

    if (i % 100 == 0) {
      ops.push_back({OpType::Depth, 0, Side::Buy, 0, 0});
    }
  }

  return ops;
}

void run_verify(const std::vector<Operation>& ops) {
  std::cout << "Running correctness test..." << std::endl;

  MapOrderBook b1;
  HeapOrderBook b2;
  FixedOrderBook b3;

  bool pass1 = true, pass2 = true, pass3 = true;

  for (size_t i = 0; i < ops.size(); ++i) {
    const auto& op = ops[i];
    if (op.type == OpType::Add) {
      b1.add_order(op.id, op.side, op.price, op.qty);
      b2.add_order(op.id, op.side, op.price, op.qty);
      b3.add_order(op.id, op.side, op.price, op.qty);
    } else if (op.type == OpType::Cancel) {
      bool c1 = b1.cancel_order(op.id);
      bool c2 = b2.cancel_order(op.id);
      bool c3 = b3.cancel_order(op.id);
      if (c1 != c2 || c1 != c3) {
        pass2 = false;
        pass3 = false;
        break;
      }
    } else if (op.type == OpType::BBO) {
      BBO bb1 = b1.get_bbo();
      BBO bb2 = b2.get_bbo();
      BBO bb3 = b3.get_bbo();

      if (bb1.has_bid != bb2.has_bid || bb1.bid_price != bb2.bid_price ||
          bb1.bid_qty != bb2.bid_qty || bb1.has_ask != bb2.has_ask ||
          bb1.ask_price != bb2.ask_price || bb1.ask_qty != bb2.ask_qty)
        pass2 = false;

      if (bb1.has_bid != bb3.has_bid || bb1.bid_price != bb3.bid_price ||
          bb1.bid_qty != bb3.bid_qty || bb1.has_ask != bb3.has_ask ||
          bb1.ask_price != bb3.ask_price || bb1.ask_qty != bb3.ask_qty)
        pass3 = false;

      if (!pass2 || !pass3)
        break;
    } else if (op.type == OpType::Depth) {
      auto bids1 = b1.get_bids(10);
      auto bids2 = b2.get_bids(10);
      auto bids3 = b3.get_bids(10);

      if (bids1.size() != bids2.size() || bids1.size() != bids3.size()) {
        pass2 = false;
        pass3 = false;
        break;
      }

      auto asks1 = b1.get_asks(10);
      auto asks2 = b2.get_asks(10);
      auto asks3 = b3.get_asks(10);

      if (asks1.size() != asks2.size() || asks1.size() != asks3.size()) {
        pass2 = false;
        pass3 = false;
        break;
      }
    }
  }

  std::cout << "Implementation A: " << (pass1 ? "PASS" : "FAIL") << std::endl;
  std::cout << "Implementation B: " << (pass2 ? "PASS" : "FAIL") << std::endl;
  std::cout << "Implementation C: " << (pass3 ? "PASS" : "FAIL") << std::endl;

  if (pass1 && pass2 && pass3)
    std::cout << "All implementations produce identical results." << std::endl;
  else
    std::cout << "Outputs did not match!" << std::endl;
}

struct Metric {
  double total_ms;
  double ns_add;
  double ns_cancel;
  double ns_bbo;
  uint64_t checksum;
  BBO bbo;
};

template <typename Book>
Metric run_single(const std::vector<Operation>& ops) {
  Book bk;
  uint64_t add_time = 0, cancel_time = 0, bbo_time = 0;
  long n_add = 0, n_cancel = 0, n_bbo = 0;
  uint64_t checksum = 0;

  auto t0 = std::chrono::steady_clock::now();

  for (const auto& op : ops) {
    if (op.type == OpType::Add) {
      auto start = std::chrono::steady_clock::now();
      bk.add_order(op.id, op.side, op.price, op.qty);
      auto end = std::chrono::steady_clock::now();
      add_time +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
              .count();
      n_add++;
    } else if (op.type == OpType::Cancel) {
      auto start = std::chrono::steady_clock::now();
      bk.cancel_order(op.id);
      auto end = std::chrono::steady_clock::now();
      cancel_time +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
              .count();
      n_cancel++;
    } else if (op.type == OpType::BBO) {
      auto start = std::chrono::steady_clock::now();
      BBO b = bk.get_bbo();
      auto end = std::chrono::steady_clock::now();
      bbo_time +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
              .count();
      checksum += (b.has_bid ? b.bid_price : 0) + (b.has_ask ? b.ask_price : 0);
      n_bbo++;
    } else if (op.type == OpType::Depth) {
      auto bids = bk.get_bids(10);
      auto asks = bk.get_asks(10);
      if (!bids.empty())
        checksum += bids[0].price;
      if (!asks.empty())
        checksum += asks[0].price;
    }
  }

  auto t1 = std::chrono::steady_clock::now();
  double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

  Metric m;
  m.total_ms = total_ms;
  m.ns_add = n_add ? ((double)add_time / n_add) : 0;
  m.ns_cancel = n_cancel ? ((double)cancel_time / n_cancel) : 0;
  m.ns_bbo = n_bbo ? ((double)bbo_time / n_bbo) : 0;
  m.checksum = checksum;
  m.bbo = bk.get_bbo();
  return m;
}

template <typename Book>
void run_bench(const std::string& name, const std::vector<Operation>& ops) {
  std::cout << "Benchmarking " << name << " (5 runs)..." << std::endl;

  std::vector<Metric> runs;
  for (int r = 1; r <= 5; ++r) {
    Metric m = run_single<Book>(ops);
    runs.push_back(m);
    std::cout << "Run " << r << ": " << std::fixed << std::setprecision(2)
              << m.total_ms << " ms | add: " << m.ns_add
              << " ns | cancel: " << m.ns_cancel << " ns | BBO: " << m.ns_bbo
              << " ns" << std::endl;
  }

  std::sort(runs.begin(), runs.end(), [](const Metric& a, const Metric& b) {
    return a.total_ms < b.total_ms;
  });

  const auto& med = runs[2];

  std::cout << "\nMedian Result:" << std::endl;
  std::cout << "Total time: " << std::fixed << std::setprecision(2)
            << med.total_ms << " ms" << std::endl;
  std::cout << "ns / add:    " << std::fixed << std::setprecision(2)
            << med.ns_add << std::endl;
  std::cout << "ns / cancel: " << std::fixed << std::setprecision(2)
            << med.ns_cancel << std::endl;
  std::cout << "ns / BBO:    " << std::fixed << std::setprecision(2)
            << med.ns_bbo << std::endl;

  std::cout << "\nFinal BBO:" << std::endl;
  if (med.bbo.has_bid)
    std::cout << "Bid: " << format_price(med.bbo.bid_price) << " x "
              << med.bbo.bid_qty << std::endl;
  else
    std::cout << "Bid: None" << std::endl;

  if (med.bbo.has_ask)
    std::cout << "Ask: " << format_price(med.bbo.ask_price) << " x "
              << med.bbo.ask_qty << std::endl;
  else
    std::cout << "Ask: None" << std::endl;

  std::cout << "\nChecksum: " << med.checksum << std::endl;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cout << "Usage: " << argv[0] << " --impl A|B|C | --verify"
              << std::endl;
    return 1;
  }

  std::string opt = argv[1];
  auto ops = make_workload(1000000);

  if (opt == "--verify") {
    run_verify(ops);
  } else if (opt == "--impl" && argc >= 3) {
    std::string which = argv[2];
    if (which == "A" || which == "a")
      run_bench<MapOrderBook>("Implementation A (Map)", ops);
    else if (which == "B" || which == "b")
      run_bench<HeapOrderBook>("Implementation B (Heap)", ops);
    else if (which == "C" || which == "c")
      run_bench<FixedOrderBook>("Implementation C (Fixed Array)", ops);
    else
      std::cerr << "Unknown impl: " << which << std::endl;
  } else {
    std::cerr << "Invalid arguments" << std::endl;
    return 1;
  }

  return 0;
}