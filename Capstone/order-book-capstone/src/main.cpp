// =============================================================================
// main.cpp  –  Interactive demo / smoke test
// =============================================================================
// Demonstrates the common API across all three implementations.
// Not part of the benchmark or correctness test suite.
// =============================================================================
#include "order_book.hpp"
#include "map_order_book.hpp"
#include "vector_order_book.hpp"
#include "array_order_book.hpp"

#include <iostream>
#include <iomanip>
#include <string>
#include <memory>

using namespace hft;

static void print_bbo(const BBO& b)
{
    std::cout << "  BBO: ";
    if (b.has_bid)
        std::cout << "bid=" << std::fixed << std::setprecision(2) << b.bid_price
                  << " x " << b.bid_qty;
    else
        std::cout << "bid=(none)";
    std::cout << "  |  ";
    if (b.has_ask)
        std::cout << "ask=" << b.ask_price << " x " << b.ask_qty;
    else
        std::cout << "ask=(none)";
    std::cout << "\n";
}

static void print_depth(const std::string& label, const std::vector<Level>& levels)
{
    std::cout << "  " << label << ": ";
    for (const auto& l : levels)
        std::cout << std::fixed << std::setprecision(2) << l.price << "x" << l.qty << "  ";
    std::cout << "\n";
}

static void demo(IOrderBook& book, const std::string& name)
{
    std::cout << "\n--- Demo: " << name << " ---\n";

    book.add_order(1, Side::Buy,  99.50, 100);
    book.add_order(2, Side::Buy,  99.50,  50);   // same level
    book.add_order(3, Side::Buy,  99.00,  200);
    book.add_order(4, Side::Sell, 100.50,  80);
    book.add_order(5, Side::Sell, 101.00,  40);

    std::cout << "After 5 orders:\n";
    print_bbo(book.get_bbo());
    print_depth("Bids", book.get_bids(5));
    print_depth("Asks", book.get_asks(5));

    book.cancel_order(1);
    std::cout << "\nAfter cancelling order 1 (one of two at 99.50):\n";
    print_bbo(book.get_bbo());
    print_depth("Bids", book.get_bids(5));

    book.cancel_order(2);
    std::cout << "\nAfter cancelling order 2 (last at 99.50 – level should disappear):\n";
    print_bbo(book.get_bbo());
    print_depth("Bids", book.get_bids(5));

    std::cout << "Cancel unknown id=999 → " << book.cancel_order(999) << " (expect 0)\n";
    std::cout << "Cancel already-cancelled id=2 → " << book.cancel_order(2) << " (expect 0)\n";

    // Validate rejection
    try {
        book.add_order(10, Side::Buy, 100.00, 1);  // midpoint
    } catch (const std::invalid_argument& e) {
        std::cout << "\nExpected rejection: " << e.what() << "\n";
    }
}

int main()
{
    {
        MapOrderBook book;
        demo(book, "MapOrderBook (A)");
    }
    {
        VectorOrderBook book;
        demo(book, "VectorOrderBook (B)");
    }
    {
        ArrayOrderBook book;
        demo(book, "ArrayOrderBook (C)");
    }
    return 0;
}
