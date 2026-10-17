#include <iomanip>
#include <iostream>

#include "OrderBookA.h"
#include "OrderBookB.h"
#include "OrderBookC.h"

static void printBBO(const BBO &bbo)
{
    std::cout << "BBO: ";
    if (bbo.has_bid)
    {
        std::cout << "bid " << bbo.bid_price << " x " << bbo.bid_qty << "   ";
    }
    else
    {
        std::cout << "bid none   ";
    }
    if (bbo.has_ask)
    {
        std::cout << "ask " << bbo.ask_price << " x " << bbo.ask_qty;
    }
    else
    {
        std::cout << "ask none";
    }
    std::cout << "\n";
}

static void printLevels(const char *label, const std::vector<Level> &levels)
{
    std::cout << label << ":\n";
    if (levels.empty())
    {
        std::cout << "  (empty)\n";
    }
    for (const auto &l : levels)
    {
        std::cout << "  " << l.price << " x " << l.qty << "\n";
    }
}

template <typename Book>
static void runDemo(const char *label)
{
    std::cout << "=== " << label << " ===\n";
    Book book;

    std::cout << "-- Empty book --\n";
    printBBO(book.get_bbo());

    std::cout << "\n-- Cancel on empty book --\n";
    std::cout << "cancel_order(999) -> " << std::boolalpha << book.cancel_order(999) << " (expect false)\n";

    std::cout << "\n-- Resting orders --\n";
    book.add_order(1, Side::Buy, 99.50, 10);
    book.add_order(2, Side::Buy, 99.75, 5);
    book.add_order(3, Side::Buy, 99.75, 20);
    book.add_order(4, Side::Sell, 100.25, 15);
    book.add_order(5, Side::Sell, 100.50, 8);
    printBBO(book.get_bbo());
    printLevels("Bids (depth 10, only 2 levels exist)", book.get_bids(10));
    printLevels("Asks (depth 10, only 2 levels exist)", book.get_asks(10));

    std::cout << "\n-- Cancel order 2 (shares a price level with order 3) --\n";
    std::cout << "cancel_order(2) -> " << book.cancel_order(2) << " (expect true)\n";
    std::cout << "cancel_order(2) -> " << book.cancel_order(2) << " (expect false, already cancelled)\n";
    printLevels("Bids after cancel", book.get_bids(10));

    std::cout << "\n-- Cancel remaining order at a price level removes the level --\n";
    book.cancel_order(3);
    printLevels("Bids after clearing 99.75", book.get_bids(10));
    std::cout << "\n";
}

int main()
{
    std::cout << std::fixed << std::setprecision(2);
    runDemo<OrderBookA>("Implementation A (std::map + list)");
    runDemo<OrderBookB>("Implementation B (unordered_map + set)");
    runDemo<OrderBookC>("Implementation C (flat array, own design)");
    return 0;
}
