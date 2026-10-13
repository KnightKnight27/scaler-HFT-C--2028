#include "array_order_book.hpp"
#include "heap_order_book.hpp"
#include "map_order_book.hpp"

#include <cassert>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using Factory = std::function<std::unique_ptr<OrderBook>()>;

void contract(const Factory& make) {
    auto book = make();
    const auto empty = book->get_bbo();
    assert(!empty.has_bid && !empty.has_ask);

    book->add_order(1, Side::Buy, 9999, 10);
    book->add_order(2, Side::Buy, 9999, 5);
    book->add_order(3, Side::Sell, 10001, 7);
    const auto bbo = book->get_bbo();
    assert(bbo.has_bid && bbo.bid_price == 9999 && bbo.bid_quantity == 15);
    assert(bbo.has_ask && bbo.ask_price == 10001 && bbo.ask_quantity == 7);

    const auto bids = book->get_bids(10);
    assert(bids.size() == 1 && bids[0].price == 9999 && bids[0].quantity == 15);
    assert(book->cancel_order(2));
    assert(!book->cancel_order(2));
    assert(!book->cancel_order(999));
    assert(book->get_bbo().bid_quantity == 10);
    bool rejected = false;
    rejected = false;
    try { book->add_order(2, Side::Buy, 9998, 1); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);

    rejected = false;
    try { book->add_order(4, Side::Buy, 10000, 1); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { book->add_order(5, Side::Sell, 15001, 1); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { book->add_order(6, Side::Buy, 9999, 0); } catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);

    if (dynamic_cast<HeapOrderBook*>(book.get())) {
        book->add_order(7, Side::Buy, 9000, 1);
        assert(book->cancel_order(7));
        book->add_order(8, Side::Buy, 9000, 2);
        const auto readded = book->get_bids(10);
        assert(readded.size() == 2 && readded[1].price == 9000 && readded[1].quantity == 2);
    }
}

int main() {
    contract([] { return std::make_unique<MapOrderBook>(); });
    contract([] { return std::make_unique<HeapOrderBook>(); });
    contract([] { return std::make_unique<ArrayOrderBook>(); });
}
