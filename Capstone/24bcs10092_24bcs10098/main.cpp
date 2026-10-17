#include "impl_a/orderbook_a.hpp"
#include "impl_b/orderbook_b.hpp"
#include "impl_c/orderbook_c.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace orderbook;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void price(PriceTicks ticks) {
    std::cout << ticks / 100 << '.' << (ticks % 100 < 10 ? "0" : "") << ticks % 100;
}
void level(const PriceLevel& value) {
    price(value.price); std::cout << " x " << value.total_quantity;
}
void quote(const std::optional<PriceLevel>& value) {
    if (value) level(*value); else std::cout << "absent";
}
bool equal(const DepthSnapshot& a, const DepthSnapshot& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].price != b[i].price || a[i].total_quantity != b[i].total_quantity) return false;
    return true;
}
void show(OrderBook& book, const char* label, const DepthSnapshot& expected_bids,
          const DepthSnapshot& expected_asks) {
    const auto bbo = book.get_bbo();
    const auto bids = book.get_bids(10);
    const auto asks = book.get_asks(10);
    require(equal(bids, expected_bids) && equal(asks, expected_asks), "demo depth mismatch");
    auto check_quote = [](const std::optional<PriceLevel>& value, const DepthSnapshot& expected) {
        return value.has_value() == !expected.empty() &&
               (!value || (value->price == expected.front().price &&
                           value->total_quantity == expected.front().total_quantity));
    };
    require(check_quote(bbo.bid, expected_bids) && check_quote(bbo.ask, expected_asks), "demo BBO mismatch");
    std::cout << label << " | BBO bid="; quote(bbo.bid); std::cout << " ask="; quote(bbo.ask);
    std::cout << " | bids=[";
    for (std::size_t i = 0; i < bids.size(); ++i) { if (i) std::cout << ", "; level(bids[i]); }
    std::cout << "] asks=[";
    for (std::size_t i = 0; i < asks.size(); ++i) { if (i) std::cout << ", "; level(asks[i]); }
    std::cout << "]\n";
}
void demonstrate(OrderBook& book, char name) {
    std::cout << "\nImplementation " << name << " (integer ticks; displayed as price x quantity)\n";
    show(book, "empty", {}, {});
    for (const Order order : {Order{0, Side::bid, 9900, 10}, Order{1, Side::bid, 9900, 5},
                             Order{2, Side::bid, 9850, 7}, Order{3, Side::ask, 10100, 12},
                             Order{4, Side::ask, 10200, 8}})
        require(book.add_order(order) == AddResult::added, "demo add rejected");
    show(book, "five adds", {{9900, 15}, {9850, 7}}, {{10100, 12}, {10200, 8}});
    require(book.cancel_order(0) == CancelResult::cancelled, "demo partial-level cancel failed");
    show(book, "cancel ID 0", {{9900, 5}, {9850, 7}}, {{10100, 12}, {10200, 8}});
    require(book.cancel_order(1) == CancelResult::cancelled, "demo bid-level cancel failed");
    require(book.cancel_order(3) == CancelResult::cancelled, "demo ask-level cancel failed");
    show(book, "cancel IDs 1,3", {{9850, 7}}, {{10200, 8}});
    require(book.add_order({0, Side::bid, 9900, 1}) == AddResult::duplicate_id, "demo reused ID accepted");
    std::cout << "cancelled ID 0 reuse rejected; all expected states PASS\n";
}
} // namespace

int main() {
    try {
        std::cout << "Resting order book demo: add_order, cancel_order, get_bbo, get_bids, get_asks\n";
        for (char name : {'A', 'B', 'C'}) {
            std::unique_ptr<OrderBook> book;
            if (name == 'A') book = std::make_unique<OrderBookA>();
            else if (name == 'B') book = std::make_unique<OrderBookB>();
            else book = std::make_unique<OrderBookC>();
            demonstrate(*book, name);
        }
        std::cout << "\nDEMO CONSISTENCY PASS: A/B/C matched the same explicit expected states.\n";
    } catch (const std::exception& e) {
        std::cerr << "Demo error: " << e.what() << '\n'; return 1;
    }
}
