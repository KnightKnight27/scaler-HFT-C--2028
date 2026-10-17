// ./bench --impl A   (or B, C)
// ./bench --impl A --runs 1   for perf
#include <algorithm>
#include <chrono>
#include <string>
#include "workload.h"
#include "pq_book.h"
#include "vector_book.h"
#include "array_book.h"

long long checksum = 0;

// steady_clock, no kernel call
long long nowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Result {
  long long totalUs;
  long long add, cancel, bbo; // average ns
  long long p99Add, p99Cancel, p99Bbo;
};

long long average(const std::vector<long long>& v) {
  long long sum = 0;
  for (long long x : v) sum += x;
  return sum / (long long)v.size();
}

long long p99(std::vector<long long>& v) {
  std::sort(v.begin(), v.end());
  return v[v.size() * 99 / 100];
}

long long median(std::vector<long long> v) {
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

template <typename Book>
Result runOnce(const std::vector<Op>& ops, BBO& last) {
  Book book;
  std::vector<long long> lat[3]; // time of every add, cancel, bbo
  for (int t = 0; t < 3; t++) lat[t].reserve(ops.size());

  long long start = nowNs();
  for (size_t i = 0; i < ops.size(); i++) {
    const Op& op = ops[i];
    long long t0 = nowNs();

    if (op.type == ADD) {
      book.add_order(op.id, op.side, op.price, op.qty);
    } else if (op.type == CANCEL) {
      checksum += book.cancel_order(op.id);
    } else {
      BBO b = book.get_bbo();
      checksum += b.bid_price + b.bid_qty + b.ask_price + b.ask_qty;
    }

    lat[op.type].push_back(nowNs() - t0);

    if (i % 100 == 99) {
      for (Level& l : book.get_bids(10)) checksum += l.qty;
      for (Level& l : book.get_asks(10)) checksum += l.qty;
    }
  }

  long long end = nowNs();

  Result r;
  r.totalUs = (end - start) / 1000;
  r.add = average(lat[ADD]);
  r.cancel = average(lat[CANCEL]);
  r.bbo = average(lat[QUERY]);
  r.p99Add = p99(lat[ADD]);
  r.p99Cancel = p99(lat[CANCEL]);
  r.p99Bbo = p99(lat[QUERY]);
  last = book.get_bbo();
  return r;
}

void printResult(std::string name, Result r) {
  std::cout << name << "  total " << r.totalUs / 1000 << "." << r.totalUs % 1000 / 100 << "ms" << " | add " << r.add << " ns (p99 " << r.p99Add << ")" << " | cancel " << r.cancel << " ns (p99 " << r.p99Cancel << ")" << " | bbo " << r.bbo << " ns (p99 " << r.p99Bbo << ")\n";
}

// cost of 2 clock calls, this is inside every op time
long long timerOverhead() {
  std::vector<long long> v;

  for (int i = 0; i < 100000; i++) {
    long long t0 = nowNs();
    v.push_back(nowNs() - t0);
  }

  return median(v);
}

template <typename Book>
void bench(std::string name, int runs) {
  std::vector<Op> ops = makeWorkload(NUM_OPS);
  std::cout << "Implementation " << name << ", " << NUM_OPS << " ops, " << runs << " runs\n";
  std::cout << "timer overhead ~" << timerOverhead() << " ns per op (included in the numbers)\n\n";

  std::vector<Result> rs;
  BBO last{};
  for (int i = 0; i < runs; i++) {
    rs.push_back(runOnce<Book>(ops, last));
    printResult("run " + std::to_string(i + 1), rs.back());
  }

  // median of every value separately
  std::vector<long long> total, add, cancel, bbo, pAdd, pCancel, pBbo;

  for (Result& r : rs) {
    total.push_back(r.totalUs);
    add.push_back(r.add);
    cancel.push_back(r.cancel);
    bbo.push_back(r.bbo);
    pAdd.push_back(r.p99Add);
    pCancel.push_back(r.p99Cancel);
    pBbo.push_back(r.p99Bbo);
  }

  Result m;
  m.totalUs = median(total);
  m.add = median(add);
  m.cancel = median(cancel);
  m.bbo = median(bbo);
  m.p99Add = median(pAdd);
  m.p99Cancel = median(pCancel);
  m.p99Bbo = median(pBbo);
  std::cout << "\n";
  printResult("MEDIAN", m);

  std::cout << "\nFinal BBO:\nBid: ";
  printPrice(last.bid_price);
  std::cout << " x " << last.bid_qty << "\nAsk: ";
  printPrice(last.ask_price);
  std::cout << " x " << last.ask_qty << "\nChecksum: " << checksum << "\n";
}

int main(int argc, char** argv) {
  std::string impl;
  int runs = 5;
  
  for (int i = 1; i + 1 < argc; i += 2) {
    std::string arg = argv[i];
    if (arg == "--impl") impl = argv[i + 1];
    if (arg == "--runs") runs = std::stoi(argv[i + 1]);
  }

  if (impl == "A") bench<PQBook>("A (priority queue + lazy deletion)", runs);
  else if (impl == "B") bench<VectorBook>("B (sorted vector + branchless search)", runs);
  else if (impl == "C") bench<ArrayBook>("C (one price indexed array)", runs);
  else {
    std::cout << "usage: ./bench --impl A|B|C [--runs N]\n";
    return 1;
  }
  return 0;
}
