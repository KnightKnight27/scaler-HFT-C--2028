#include "orderbook_b.h"
#include <iostream>

int main() {
    std::cout << "=== Implementation B (std::priority_queue with lazy deletion) ===" << std::endl;
    OrderBookB book;

    // Add some orders
    book.add_order(1, Side::Buy, 99.50, 100);
    book.add_order(2, Side::Buy, 99.50, 50);
    book.add_order(3, Side::Buy, 99.00, 200);
    book.add_order(4, Side::Sell, 100.50, 150);
    book.add_order(5, Side::Sell, 101.00, 300);

    BBO bbo = book.get_bbo();
    std::cout << "BBO: Bid " << bbo.bid_level.price << " x " << bbo.bid_level.qty
              << " | Ask " << bbo.ask_level.price << " x " << bbo.ask_level.qty << std::endl;

    std::cout << "Bid Levels (depth 5):" << std::endl;
    for (const auto& level : book.get_bids(5)) {
        std::cout << "  " << level.price << " : " << level.qty << std::endl;
    }

    std::cout << "Ask Levels (depth 5):" << std::endl;
    for (const auto& level : book.get_asks(5)) {
        std::cout << "  " << level.price << " : " << level.qty << std::endl;
    }

    std::cout << "Cancelling order 1 (Buy 99.50 x 100)..." << std::endl;
    book.cancel_order(1);

    bbo = book.get_bbo();
    std::cout << "New BBO: Bid " << bbo.bid_level.price << " x " << bbo.bid_level.qty
              << " | Ask " << bbo.ask_level.price << " x " << bbo.ask_level.qty << std::endl;

    std::cout << "Implementation B successfully executed!" << std::endl;
    return 0;
}
