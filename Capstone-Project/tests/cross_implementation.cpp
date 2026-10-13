#include "array_order_book.hpp"
#include "heap_order_book.hpp"
#include "map_order_book.hpp"
#include "workload.hpp"

#include <cassert>
#include <memory>

bool same(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.bid_price == b.bid_price && a.bid_quantity == b.bid_quantity &&
           a.has_ask == b.has_ask && a.ask_price == b.ask_price && a.ask_quantity == b.ask_quantity;
}

bool same(const std::vector<Level>& a, const std::vector<Level>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const Level& x, const Level& y) {
        return x.price == y.price && x.quantity == y.quantity;
    });
}

int main() {
    auto a = std::make_unique<MapOrderBook>();
    auto b = std::make_unique<HeapOrderBook>();
    auto c = std::make_unique<ArrayOrderBook>();
    const auto workload = make_workload(1'000'000);
    for (std::size_t i = 0; i < workload.size(); ++i) {
        apply(workload[i], *a); apply(workload[i], *b); apply(workload[i], *c);
        assert(same(a->get_bbo(), b->get_bbo()) && same(a->get_bbo(), c->get_bbo()));
        assert(same(a->get_bids(), b->get_bids()) && same(a->get_bids(), c->get_bids()));
        assert(same(a->get_asks(), b->get_asks()) && same(a->get_asks(), c->get_asks()));
        if ((i + 1) % 100 == 0) {
            assert(same(a->get_bids(10), b->get_bids(10)) && same(a->get_bids(10), c->get_bids(10)));
            assert(same(a->get_asks(10), b->get_asks(10)) && same(a->get_asks(10), c->get_asks(10)));
        }
    }
}
