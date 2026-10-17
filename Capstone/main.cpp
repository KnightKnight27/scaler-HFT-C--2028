#include "orderbook.h"
#include <iostream>

int main() {
    std::cout << "=== Default OrderBook Demo ===" << std::endl;
    OrderBook book;

    book.add_order(1, Side::Buy, 99.50, 100);
    book.add_order(2, Side::Buy, 99.50, 50);
    book.add_order(3, Side::Buy, 99.00, 200);
    book.add_order(4, Side::Sell, 100.50, 150);
    book.add_order(5, Side::Sell, 101.00, 300);

    BBO bbo = book.get_bbo();
    std::cout << "BBO: Bid " << bbo.bid_level.price << " x " << bbo.bid_level.qty
              << " | Ask " << bbo.ask_level.price << " x " << bbo.ask_level.qty << std::endl;

    std::cout << "Top 3 Bids:" << std::endl;
    for (const auto& level : book.get_bids(3)) {
        std::cout << "  " << level.price << " : " << level.qty << std::endl;
    }

    std::cout << "Top 3 Asks:" << std::endl;
    for (const auto& level : book.get_asks(3)) {
        std::cout << "  " << level.price << " : " << level.qty << std::endl;
    }

    book.cancel_order(1);
    bbo = book.get_bbo();
    std::cout << "BBO after cancelling order 1: Bid " << bbo.bid_level.price << " x " << bbo.bid_level.qty
              << " | Ask " << bbo.ask_level.price << " x " << bbo.ask_level.qty << std::endl;

    return 0;
}
