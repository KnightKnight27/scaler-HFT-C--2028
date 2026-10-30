#include <algorithm>
#include <chrono>
#include <cstdint>
#include <print>
#include <random>
#include <vector>

#include "order_book/order_book.h"

namespace {

constexpr std::size_t kOperationCount = 5'000'000;
constexpr Price kMidPrice = 100'000;
constexpr Price kHalfSpreadLevels = 500;
constexpr Price kCrossingTicks = 5;
constexpr std::uint32_t kCancelPercent = 35;
constexpr std::size_t kCancelLookback = 1'000;

using Clock = std::chrono::steady_clock;

struct Operation {
  Order order;
  bool is_cancel;
};

std::vector<Operation> generateOperations() {
  std::mt19937_64 rng(42);
  std::uniform_int_distribution<Price> level_offset(0, kHalfSpreadLevels - 1);
  std::uniform_int_distribution<Quantity> quantity(1, 100);
  std::uniform_int_distribution<std::uint32_t> percent(0, 99);

  std::vector<Operation> operations;
  operations.reserve(kOperationCount);
  OrderId next_id = 1;
  for (std::size_t step = 0; step < kOperationCount; ++step) {
    if (next_id > kCancelLookback && percent(rng) < kCancelPercent) {
      std::uniform_int_distribution<OrderId> recent(next_id - kCancelLookback, next_id - 1);
      operations.push_back({{recent(rng), Side::Buy, 0, 0, step}, true});
      continue;
    }
    const Side side = percent(rng) < 50 ? Side::Buy : Side::Sell;
    const Price offset = level_offset(rng) - kCrossingTicks;
    const Price price = side == Side::Buy ? kMidPrice - offset : kMidPrice + offset;
    operations.push_back({{next_id++, side, price, quantity(rng), step}, false});
  }
  return operations;
}

std::size_t apply(OrderBook &book, const Operation &operation) {
  if (operation.is_cancel) {
    return book.removeOrder(operation.order.id) ? 1 : 0;
  }
  const auto trades = book.addOrder(operation.order);
  return trades ? trades->size() : 0;
}

void measureThroughput(const std::vector<Operation> &operations) {
  OrderBook book(kOperationCount);
  std::size_t events = 0;
  const auto start = Clock::now();
  for (const Operation &operation : operations) {
    events += apply(book, operation);
  }
  const std::chrono::duration<double> elapsed = Clock::now() - start;

  std::println("throughput: {:.2f} M ops/s ({:.1f} ns/op), {} trades+cancels, {} resting",
               operations.size() / elapsed.count() / 1e6,
               elapsed.count() * 1e9 / operations.size(), events, book.restingOrderCount());
}

void measureLatency(const std::vector<Operation> &operations) {
  OrderBook book(kOperationCount);
  std::vector<std::int64_t> latencies_ns;
  latencies_ns.reserve(operations.size());
  for (const Operation &operation : operations) {
    const auto start = Clock::now();
    static_cast<void>(apply(book, operation));
    latencies_ns.push_back(std::chrono::nanoseconds(Clock::now() - start).count());
  }
  std::ranges::sort(latencies_ns);
  const auto percentile = [&latencies_ns](double fraction) {
    return latencies_ns[static_cast<std::size_t>(fraction * (latencies_ns.size() - 1))];
  };
  std::println("latency ns (incl. clock overhead): p50 {} | p90 {} | p99 {} | p99.9 {} | max {}",
               percentile(0.50), percentile(0.90), percentile(0.99), percentile(0.999),
               latencies_ns.back());
}

} // namespace

int main() {
  const auto operations = generateOperations();
  std::println("{} operations over {} price levels per side, {}% cancels", operations.size(),
               kHalfSpreadLevels, kCancelPercent);
  measureThroughput(operations);
  measureLatency(operations);
}
