#include "array_book.hpp"
#include "map_book.hpp"
#include "vector_book.hpp"
#include "workload.hpp"
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>

namespace ob {
static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Book>
static void edge_tests() {
    Book book;
    require(book.get_bbo() == BBO{}, "empty BBO");
    require(book.get_bids().empty() && book.get_asks().empty(), "empty depth");
    require(!book.cancel_order(123), "unknown cancellation");

    const OrderId large = std::numeric_limits<OrderId>::max();
    book.add_order(0, Side::Buy, 9999, 10);
    book.add_order(large, Side::Buy, 9999, 20);
    book.add_order(71, Side::Buy, 5000, 7);
    book.add_order(2, Side::Sell, 10001, 30);
    book.add_order(93, Side::Sell, 15000, 40);
    require(book.get_bbo() == BBO{true, 9999, 30, true, 10001, 30}, "aggregate BBO");
    require(book.get_bids() == std::vector<Level>{{9999,30},{5000,7}}, "bid order/depth");
    require(book.get_asks() == std::vector<Level>{{10001,30},{15000,40}}, "ask order/depth");
    require(book.get_bids(1) == std::vector<Level>{{9999,30}}, "depth one");
    require(book.get_asks(1) == std::vector<Level>{{10001,30}}, "ask depth one");
    require(book.get_bids(0).empty() && book.get_asks(-1).empty(), "nonpositive depth");
    const auto before = book.get_bbo();
    auto rejects = [&](OrderId id, Side side, Price price, Quantity qty) {
        bool caught = false;
        try { book.add_order(id, side, price, qty); }
        catch (const std::invalid_argument&) { caught = true; }
        require(caught && book.get_bbo() == before, "reject invalid add without mutation");
    };
    rejects(0, Side::Buy, 9998, 5);
    rejects(44, Side::Buy, 10000, 5);
    rejects(44, Side::Sell, 10000, 5);
    rejects(44, Side::Buy, 4999, 5);
    rejects(44, Side::Sell, 15001, 5);
    rejects(44, Side::Buy, 5000, 0);
    rejects(44, Side::Sell, 15000, -1);
    rejects(44, static_cast<Side>(9), 5000, 1);

    require(book.cancel_order(0), "cancel part of a level");
    require(!book.cancel_order(0), "repeated cancellation");
    require(book.get_bbo().bid_qty == 20, "remaining aggregate");
    require(book.cancel_order(large), "cancel best bid");
    require(book.get_bbo().bid_price == 5000, "best bid crosses sparse gap");
    require(book.cancel_order(2), "cancel best ask");
    require(book.get_bbo().ask_price == 15000, "best ask crosses sparse gap");
    require(book.cancel_order(71), "cancel final bid");
    require(book.get_bbo() == BBO{false,0,0,true,15000,40}, "one-sided book");
    require(book.cancel_order(93) && book.get_bbo() == BBO{}, "drain book");
    book.add_order(12345, Side::Buy, 7777, 2);
    require(book.get_bbo() == BBO{true,7777,2,false,0,0}, "refill after empty");

    Book full;
    for (int i = 0; i < price_count; ++i) {
        full.add_order(2 * i, Side::Buy, 5000 + i, 1);
        full.add_order(2 * i + 1, Side::Sell, 10001 + i, 2);
    }
    require(full.get_bids(9999).size() == price_count, "all bid levels");
    require(full.get_asks(9999).size() == price_count, "all ask levels");
    for (int i = 0; i < price_count; ++i) {
        require(full.cancel_order(2 * (price_count - 1 - i)), "drain descending bids");
        require(full.cancel_order(2 * i + 1), "drain ascending asks");
        if (i + 1 < price_count)
            require(full.get_bbo() == BBO{true,9998-i,1,true,10002+i,2}, "successive BBOs");
    }
    require(full.get_bbo() == BBO{}, "fully drained ladder");
}

static void table_tests() {
    OrderTable table;
    Order removed{};
    for (OrderId id : {0,16,32,48}) table.insert(id, {Side::Buy, 5000, 1});
    require(table.erase(16, removed) && table.find(32) && table.find(48), "probe tombstone");
    table.insert(64, {Side::Sell, 15000, 2});
    require(table.find(64) && !table.find(16), "reuse deleted slot");
    require(!table.erase(16, removed) && !table.find(999), "missing table key");

    OrderTable churn;
    std::unordered_map<OrderId, Order> reference;
    std::mt19937 rng(7);
    OrderId next = 1;
    std::vector<OrderId> live;
    for (int i = 0; i < 20000; ++i) {
        if (live.empty() || rng() % 100 < 55) {
            OrderId id = next++ * UINT64_C(0x1000000010);
            Order order{Side::Buy, static_cast<Price>(5000 + rng() % 5000), 1};
            churn.insert(id, order);
            reference.emplace(id, order);
            live.push_back(id);
        } else {
            auto index = rng() % live.size();
            auto id = live[index];
            require(churn.erase(id, removed), "churn erase");
            require(removed.price == reference.at(id).price && !churn.find(id), "churn value");
            reference.erase(id);
            live[index] = live.back(); live.pop_back();
        }
        if (i % 100 == 0)
            for (const auto& entry : reference) {
                auto found = churn.find(entry.first);
                require(found && found->price == entry.second.price, "table rehash preserves orders");
            }
    }
    for (auto id : live) require(churn.erase(id, removed), "drain table");
    for (OrderId id = 1; id <= 2000; ++id) {
        const auto key = id + UINT64_C(0x8000000000000000);
        churn.insert(key, {Side::Sell, 12000, 2});
        require(churn.erase(key, removed), "repeated tombstone cleanup");
    }
}

void run_edge_tests() {
    edge_tests<MapBook>(); std::cout << "Implementation A edge cases: PASS\n";
    edge_tests<VectorBook>(); std::cout << "Implementation B edge cases: PASS\n";
    edge_tests<ArrayBook>(); std::cout << "Implementation C edge cases: PASS\n";
    table_tests(); std::cout << "Flat table collisions, growth, deletion and churn: PASS\n";
}

void verify(const Workload& w) {
    MapBook a(w.peak_live);
    VectorBook b(w.peak_live);
    ArrayBook c(w.peak_live);
    std::uint64_t ca = 0, cb = 0, cc = 0;
    for (std::size_t i = 0; i < w.operations.size(); ++i) {
        const auto& op = w.operations[i];
        const bool ra = apply(a, op, ca), rb = apply(b, op, cb), rc = apply(c, op, cc);
        const auto bbo = a.get_bbo();
        const auto bids = a.get_bids(price_count), asks = a.get_asks(price_count);
        if (!ra || ra != rb || ra != rc || !(bbo == b.get_bbo()) || !(bbo == c.get_bbo()) ||
            bids != b.get_bids(price_count) || bids != c.get_bids(price_count) ||
            asks != b.get_asks(price_count) || asks != c.get_asks(price_count))
            throw std::runtime_error("verification mismatch at operation " + std::to_string(i + 1));
        if ((i + 1) % 100 == 0) {
            const auto top_bids = a.get_bids(), top_asks = a.get_asks();
            require(top_bids == b.get_bids() && top_bids == c.get_bids() &&
                    top_asks == b.get_asks() && top_asks == c.get_asks(), "depth-10 mismatch");
            consume(ca, top_bids); consume(ca, top_asks);
            consume(cb, b.get_bids()); consume(cb, b.get_asks());
            consume(cc, c.get_bids()); consume(cc, c.get_asks());
        }
        if ((i + 1) % 100000 == 0) std::cout << "Verified " << i + 1 << " operations\n" << std::flush;
    }
    require(ca == cb && ca == cc, "verification checksum mismatch");
    std::cout << "Implementation A: PASS\nImplementation B: PASS\nImplementation C: PASS\n"
              << "All implementations produce identical results after every operation.\n"
              << "Compared complete bid/ask depth, BBO, prices, quantities and cancellation results.\n"
              << "Checksum: " << ca << '\n';
}
}
