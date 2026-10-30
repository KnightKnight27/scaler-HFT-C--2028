#include <functional>
#include <print>
#include <source_location>
#include <span>
#include <string_view>
#include <vector>

#include "order_book/order_book.h"

namespace {

int failures = 0;

void check(bool condition, std::string_view what,
           std::source_location location = std::source_location::current()) {
  if (!condition) {
    ++failures;
    std::println("  FAILED line {}: {}", location.line(), what);
  }
}

std::vector<Trade> place(OrderBook &book, const Order &order) {
  const auto trades = book.addOrder(order);
  check(trades.has_value(), "order accepted");
  return trades ? std::vector(trades->begin(), trades->end()) : std::vector<Trade>{};
}

void restsNonCrossingOrdersAndReportsBBO() {
  OrderBook book;
  place(book, {1, Side::Buy, 100, 10, 1});
  place(book, {2, Side::Buy, 99, 20, 2});
  place(book, {3, Side::Sell, 101, 50, 3});
  place(book, {4, Side::Buy, 100, 5, 4});

  const auto bids = book.getBids();
  check(bids.size() == 2, "two bid levels");
  check(bids[0].price == 100 && bids[0].total_qty == 15 && bids[0].order_count == 2,
        "best bid aggregates same-price orders");
  check(bids[1].price == 99, "bids sorted high to low");

  const BBO bbo = book.getBBO();
  check(bbo.best_bid && bbo.best_bid->price == 100, "best bid 100");
  check(bbo.best_ask && bbo.best_ask->price == 101, "best ask 101");
}

void matchesBestPriceFirst() {
  OrderBook book;
  place(book, {1, Side::Buy, 100, 10, 1});
  place(book, {2, Side::Buy, 101, 10, 2});

  const auto trades = place(book, {3, Side::Sell, 100, 15, 3});
  check(trades.size() == 2, "two fills");
  check(trades[0].maker_id == 2 && trades[0].price == 101 && trades[0].qty == 10,
        "higher bid filled first at maker price");
  check(trades[1].maker_id == 1 && trades[1].price == 100 && trades[1].qty == 5,
        "then next level");
  check(book.getBids()[0].total_qty == 5, "residual maker qty stays");
}

void matchesEarliestOrderFirstWithinLevel() {
  OrderBook book;
  place(book, {1, Side::Sell, 100, 5, 1});
  place(book, {2, Side::Sell, 100, 5, 2});
  place(book, {3, Side::Sell, 100, 5, 3});

  const auto trades = place(book, {4, Side::Buy, 100, 7, 4});
  check(trades.size() == 2, "two fills");
  check(trades[0].maker_id == 1 && trades[0].qty == 5, "oldest filled fully");
  check(trades[1].maker_id == 2 && trades[1].qty == 2, "second partially");
  check(!book.removeOrder(1), "fully filled order leaves the book");
  check(book.getAsks()[0].total_qty == 8 && book.getAsks()[0].order_count == 2,
        "level totals updated");
}

void restsTakerResidualAfterSweep() {
  OrderBook book;
  place(book, {1, Side::Sell, 101, 5, 1});
  place(book, {2, Side::Sell, 102, 5, 2});
  place(book, {3, Side::Sell, 104, 5, 3});

  const auto trades = place(book, {4, Side::Buy, 103, 20, 4});
  check(trades.size() == 2, "sweeps only crossing levels");

  const BBO bbo = book.getBBO();
  check(bbo.best_bid && bbo.best_bid->price == 103 && bbo.best_bid->total_qty == 10,
        "residual rests at limit price");
  check(bbo.best_ask && bbo.best_ask->price == 104, "uncrossed ask remains");
  check(book.restingOrderCount() == 2, "only residual and far ask remain");
}

void removesOrdersFromAnyPosition() {
  OrderBook book;
  place(book, {1, Side::Buy, 100, 1, 1});
  place(book, {2, Side::Buy, 100, 2, 2});
  place(book, {3, Side::Buy, 100, 3, 3});

  check(book.removeOrder(2), "remove middle order");
  check(!book.removeOrder(2), "second remove fails");
  check(!book.removeOrder(42), "unknown id fails");
  check(book.getBids()[0].total_qty == 4 && book.getBids()[0].order_count == 2,
        "level updated after cancel");

  const auto trades = place(book, {4, Side::Sell, 100, 4, 4});
  check(trades.size() == 2 && trades[0].maker_id == 1 && trades[1].maker_id == 3,
        "FIFO preserved after unlink");
  check(book.getBids().empty(), "level erased when emptied");

  place(book, {5, Side::Sell, 105, 1, 5});
  check(book.removeOrder(5) && !book.getBBO().best_ask, "cancel empties ask side");
}

void rejectsInvalidOrders() {
  OrderBook book;
  place(book, {1, Side::Buy, 100, 10, 1});
  const auto rejectedWith = [&book](const Order &order, BookError expected) {
    const auto result = book.addOrder(order);
    return !result && result.error() == expected;
  };
  check(rejectedWith({1, Side::Sell, 200, 1, 2}, BookError::DuplicateOrderId), "duplicate id");
  check(rejectedWith({2, Side::Buy, 100, 0, 3}, BookError::ZeroQuantity), "zero qty");
  check(rejectedWith({3, Side::Buy, 0, 1, 4}, BookError::NonPositivePrice), "zero price");
  check(book.restingOrderCount() == 1, "rejections leave book untouched");
}

void supportsThousandPriceLevels() {
  constexpr Price kLevels = 1000;
  OrderBook book;
  for (Price offset = 0; offset < kLevels; ++offset) {
    place(book, {static_cast<OrderId>(offset + 1), Side::Buy, 1000 + offset, 1, 0});
    place(book, {static_cast<OrderId>(offset + 1 + kLevels), Side::Sell, 3000 + offset, 1, 0});
  }
  check(book.getBids().size() == kLevels && book.getAsks().size() == kLevels, "1000 levels");
  check(book.getBids(5).size() == 5 && book.getBids(5)[0].price == 1999, "depth limited view");
  check(book.getAsks()[0].price == 3000 && book.getAsks().back().price == 3999,
        "asks sorted low to high");

  const auto trades = place(book, {9999, Side::Sell, 1000, kLevels, 0});
  check(trades.size() == kLevels && trades.back().price == 1000, "sweeps every bid level");
  check(book.getBids().empty(), "bid side empty");
}

} // namespace

int main() {
  const std::vector<std::pair<std::string_view, std::function<void()>>> tests = {
      {"restsNonCrossingOrdersAndReportsBBO", restsNonCrossingOrdersAndReportsBBO},
      {"matchesBestPriceFirst", matchesBestPriceFirst},
      {"matchesEarliestOrderFirstWithinLevel", matchesEarliestOrderFirstWithinLevel},
      {"restsTakerResidualAfterSweep", restsTakerResidualAfterSweep},
      {"removesOrdersFromAnyPosition", removesOrdersFromAnyPosition},
      {"rejectsInvalidOrders", rejectsInvalidOrders},
      {"supportsThousandPriceLevels", supportsThousandPriceLevels},
  };
  for (const auto &[name, test] : tests) {
    std::println("{}", name);
    test();
  }
  std::println("{}", failures == 0 ? "all tests passed" : "tests failed");
  return failures == 0 ? 0 : 1;
}
