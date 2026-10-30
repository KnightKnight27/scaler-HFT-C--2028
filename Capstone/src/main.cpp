#include <algorithm>
#include <print>
#include <span>
#include <string_view>

#include "order_book/order_book.h"
#include "order_book/util/price.h"

namespace {

void printLevels(std::string_view title, std::span<const LevelSnapshot> levels) {
  std::println("  {}:", title);
  for (const auto &level : levels) {
    std::println("    {:>8} x {:<5} ({} orders)", formatPrice(level.price), level.total_qty,
                 level.order_count);
  }
}

void printBook(const OrderBook &book) {
  auto asks = book.getAsks();
  std::ranges::reverse(asks);
  printLevels("asks (best last)", asks);
  printLevels("bids (best first)", book.getBids());
  const BBO bbo = book.getBBO();
  std::println("  BBO: bid {} | ask {}\n",
               bbo.best_bid ? formatPrice(bbo.best_bid->price) : "-",
               bbo.best_ask ? formatPrice(bbo.best_ask->price) : "-");
}

void submit(OrderBook &book, std::string_view trader, const Order &order) {
  std::println("{} {} {} @ {} (id {})", trader, order.side == Side::Buy ? "BUY" : "SELL",
               order.qty, formatPrice(order.price), order.id);
  const auto trades = book.addOrder(order);
  if (!trades) {
    std::println("  rejected: {}", describe(trades.error()));
    return;
  }
  for (const Trade &trade : *trades) {
    std::println("  TRADE maker {} <- taker {}: {} @ {}", trade.maker_id, trade.taker_id,
                 trade.qty, formatPrice(trade.price));
  }
  printBook(book);
}

} // namespace

int main() {
  OrderBook reliance;

  submit(reliance, "Kartik", {1, Side::Buy, toTicks(100.0), 10, 1});
  submit(reliance, "Manik", {2, Side::Sell, toTicks(101.0), 5, 2});
  submit(reliance, "Kartik", {3, Side::Buy, toTicks(100.5), 5, 3});
  submit(reliance, "Paras", {4, Side::Sell, toTicks(100.0), 10, 4});
  submit(reliance, "Anyone", {2, Side::Sell, toTicks(100.0), 10, 5});

  std::println("Cancel id 1: {}", reliance.removeOrder(1) ? "ok" : "not found");
  std::println("Cancel id 1 again: {}", reliance.removeOrder(1) ? "ok" : "not found");
  printBook(reliance);
}
