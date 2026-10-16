#include "orderbook/interleaved_book.hpp"
#include "orderbook/tree_book.hpp"
#include "orderbook/vector_book.hpp"
#include "orderbook/workload.hpp"
#include <iostream>
#include <string>
#include <unordered_map>
#include <memory>

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void check_layout(const ob::InterleavedBook& book) {
    const auto view = book.storage_view();
    const auto first = reinterpret_cast<std::uintptr_t>(view.data);
    const auto last = reinterpret_cast<std::uintptr_t>(view.data + 2) - 1;
    require(first % ob::InterleavedBook::storage_alignment == 0, "unaligned single price buffer");
    for (const std::size_t line_bytes : {64U, 128U})
        require(first / line_bytes == last / line_bytes, "top pair crosses cache line");
    require(view.bid_count <= 5000 && view.ask_count <= 5000, "side capacity");
    const auto bids = book.get_bids(5000), asks = book.get_asks(5000);
    require(bids.size() == view.bid_count && asks.size() == view.ask_count, "layout counts");
    for (std::size_t i = 0; i < bids.size(); ++i) {
        require(view.data[2 * i] == bids[i], "bid not stored at even rank");
        require(i == 0 || bids[i - 1].price > bids[i].price, "bid storage not descending");
    }
    for (std::size_t i = 0; i < asks.size(); ++i) {
        require(view.data[2 * i + 1] == asks[i], "ask not stored at odd rank");
        require(i == 0 || asks[i - 1].price < asks[i].price, "ask storage not ascending");
    }
}
void layout_tests() {
    ob::InterleavedBook stack_book;
    check_layout(stack_book);
    auto heap_book = std::make_unique<ob::InterleavedBook>();
    check_layout(*heap_book);
    std::vector<ob::InterleavedBook> books(2);
    for (const auto& book : books) check_layout(book);
    std::vector<ob::InterleavedBook> relocated;
    relocated.reserve(1);
    relocated.emplace_back();
    relocated[0].add_order(1, ob::Side::Buy, 9999, 3);
    relocated[0].add_order(2, ob::Side::Sell, 10001, 5);
    const auto before_move = relocated[0].get_bbo();
    relocated.emplace_back(); // Forces growth beyond capacity 1 with a populated book.
    require(relocated[0].get_bbo() == before_move, "populated vector relocation lost quotes");
    check_layout(relocated[0]);
    require(relocated[0].cancel_order(1) && relocated[0].cancel_order(2), "relocated order lookup");
    check_layout(relocated[0]);
    const ob::Tick bid_prices[] = {9000, 9999, 5000, 9500, 8000};
    const ob::Tick ask_prices[] = {12000, 10001, 15000, 11000, 14000};
    for (std::size_t i = 0; i < 5; ++i) {
        stack_book.add_order(i, ob::Side::Buy, bid_prices[i], 1);
        check_layout(stack_book); // Includes a one-sided state and all insertion ranks.
        stack_book.add_order(i + 10, ob::Side::Sell, ask_prices[i], 1);
        check_layout(stack_book);
    }
    const std::uint64_t bid_ids[] = {1, 3, 0, 4, 2};
    const std::uint64_t ask_ids[] = {11, 13, 10, 14, 12};
    for (std::size_t i = 0; i < 5; ++i) {
        require(stack_book.cancel_order(bid_ids[i]), "cancel next best bid");
        check_layout(stack_book);
        require(stack_book.cancel_order(ask_ids[i]), "cancel next best ask");
        check_layout(stack_book);
    }
    std::cout << "Layout tests: PASS (one buffer, opposite rank order, complete top pair in 64/128-byte lines)\n";
}
template<class Exception, class Fn> void must_throw(Fn fn, const std::string& label) {
    try { fn(); } catch (const Exception&) { return; }
    throw std::runtime_error("expected exception: " + label);
}
template<class Book> void edge_tests() {
    Book b;
    require(b.get_bbo() == ob::BBO{}, "empty BBO");
    require(b.get_bids().empty() && b.get_asks().empty(), "empty depth");
    require(!b.cancel_order(42), "unknown cancel");
    b.add_order(90, ob::Side::Buy, 9999, 10);
    b.add_order(1, ob::Side::Buy, 9999, 20); // Out-of-order IDs are supported.
    b.add_order(UINT64_MAX, ob::Side::Sell, 10001, 15);
    b.add_order(0, ob::Side::Buy, 5000, 1);
    b.add_order(8, ob::Side::Sell, 15000, 2);
    require(b.get_bbo() == ob::BBO{true, 9999, 30, true, 10001, 15}, "aggregation and boundary BBO");
    require(b.get_bids(10) == std::vector<ob::Level>{{9999,30},{5000,1}}, "bid ordering and insufficient depth");
    require(b.get_asks(1) == std::vector<ob::Level>{{10001,15}}, "ask depth");
    require(b.get_bids(-1).empty() && b.get_asks(0).empty(), "nonpositive depth");
    const auto state = b.get_bbo();
    must_throw<std::invalid_argument>([&] { b.add_order(90, ob::Side::Buy, 9998, 1); }, "duplicate");
    must_throw<std::invalid_argument>([&] { b.add_order(10, ob::Side::Buy, 10000, 1); }, "mid tick");
    must_throw<std::invalid_argument>([&] { b.add_order(10, ob::Side::Sell, 10000, 1); }, "ask below range");
    must_throw<std::invalid_argument>([&] { b.add_order(10, ob::Side::Buy, 4999, 1); }, "bid below range");
    must_throw<std::invalid_argument>([&] { b.add_order(10, ob::Side::Sell, 15001, 1); }, "ask above range");
    must_throw<std::invalid_argument>([&] { b.add_order(10, ob::Side::Buy, 9000, 0); }, "zero quantity");
    must_throw<std::invalid_argument>([&] { b.add_order(10, ob::Side::Buy, 9000, -1); }, "negative quantity");
    must_throw<std::invalid_argument>([&] { b.add_order(10, static_cast<ob::Side>(9), 9000, 1); }, "invalid side");
    require(b.get_bbo() == state, "invalid additions changed state");
    require(b.cancel_order(90) && !b.cancel_order(90), "repeat cancel");
    require(b.get_bbo().bid_qty == 20, "partial level cancel");
    require(b.cancel_order(1) && b.get_bbo().bid_price == 5000, "next best bid");
    require(b.cancel_order(UINT64_MAX) && b.get_bbo().ask_price == 15000, "next best ask");
    require(b.cancel_order(0) && b.cancel_order(8), "drain");
    require(b.get_bbo() == ob::BBO{}, "drained BBO");

    Book overflow;
    overflow.add_order(1, ob::Side::Buy, 5000, std::numeric_limits<long>::max());
    must_throw<std::overflow_error>([&] { overflow.add_order(2, ob::Side::Buy, 5000, 1); }, "aggregate overflow");
    require(!overflow.cancel_order(2) && overflow.get_bbo().bid_qty == std::numeric_limits<long>::max(), "overflow changed state");

    // Cover the complete price universe and repeated best-level removal.
    Book dense;
    for (int i = 0; i < ob::levels_per_side; ++i) {
        dense.add_order(static_cast<std::uint64_t>(i * 2), ob::Side::Buy, 5000 + i, i + 1);
        dense.add_order(static_cast<std::uint64_t>(i * 2 + 1), ob::Side::Sell, 10001 + i, i + 1);
    }
    require(dense.get_bids(INT32_MAX).size() == 5000 && dense.get_asks(INT32_MAX).size() == 5000, "full depth");
    for (int i = 0; i < ob::levels_per_side; ++i) {
        require(dense.get_bbo().bid_price == 9999 - i && dense.get_bbo().ask_price == 10001 + i, "dense extrema");
        require(dense.cancel_order(static_cast<std::uint64_t>((4999 - i) * 2)), "dense bid cancel");
        require(dense.cancel_order(static_cast<std::uint64_t>(i * 2 + 1)), "dense ask cancel");
    }
    require(dense.get_bbo() == ob::BBO{}, "dense empty");
    Book sparse;
    for (const int i : {0, 63, 64, 127, 128, 4991, 4992, 4999}) {
        sparse.add_order(static_cast<std::uint64_t>(i), ob::Side::Buy, 5000 + i, 1);
        sparse.add_order(static_cast<std::uint64_t>(i + 10000), ob::Side::Sell, 10001 + i, 2);
    }
    const auto bids = sparse.get_bids(5000), asks = sparse.get_asks(5000);
    require(bids.size() == 8 && asks.size() == 8, "sparse count");
    for (std::size_t i = 0; i < 8; ++i)
        require(bids[i].price - 5000 == asks[7 - i].price - 10001, "sparse word traversal");
}

// Reference deliberately scans quantity arrays and never uses a cached best or bitmap.
class Reference {
    std::unordered_map<std::uint64_t, ob::Order> orders_;
    std::array<long, 15001> quantity_{};
public:
    bool apply(const ob::Event& e) {
        if (e.kind == ob::Kind::Add) {
            require(orders_.emplace(e.id, ob::Order{e.side,e.price,e.qty}).second, "generator reused ID");
            quantity_[e.price] += e.qty;
        } else if (e.kind == ob::Kind::Cancel) {
            auto it = orders_.find(e.id);
            if (it == orders_.end()) return false;
            quantity_[it->second.price] -= it->second.qty;
            orders_.erase(it);
        }
        return true;
    }
    ob::BBO bbo() const {
        ob::BBO b;
        for (int p = 9999; p >= 5000; --p) if (quantity_[p]) {
            b.has_bid = true; b.bid_price = p; b.bid_qty = quantity_[p]; break;
        }
        for (int p = 10001; p <= 15000; ++p) if (quantity_[p]) {
            b.has_ask = true; b.ask_price = p; b.ask_qty = quantity_[p]; break;
        }
        return b;
    }
    void check(const std::vector<ob::Level>& levels, bool bids) const {
        std::size_t index = 0;
        for (int p = bids ? 9999 : 10001; bids ? p >= 5000 : p <= 15000; p += bids ? -1 : 1) {
            if (!quantity_[p]) continue;
            require(index < levels.size(), "missing reference price level");
            require(levels[index].price == p && levels[index].qty == quantity_[p], "reference price/quantity mismatch");
            ++index;
        }
        require(index == levels.size(), "extra reference price level");
    }
};
void verify(const ob::Workload& workload) {
    ob::TreeBook a; ob::VectorBook b; ob::InterleavedBook c; Reference reference;
    for (std::size_t i = 0; i < workload.events.size(); ++i) {
        const auto& e = workload.events[i];
        require(reference.apply(e), "generator cancelled a non-live order");
        require(ob::apply(a,e) == ob::apply(b,e), "A/B operation return mismatch");
        // A has already consumed this operation; apply C exactly once.
        const auto observed_c = ob::apply(c,e);
        if (e.kind == ob::Kind::Query) require(observed_c == ob::consume(reference.bbo()), "C query mismatch");
        if (e.kind == ob::Kind::Cancel) require(observed_c == 1, "C cancel mismatch");
        require(a.get_bbo() == reference.bbo() && b.get_bbo() == reference.bbo() && c.get_bbo() == reference.bbo(),
                "BBO mismatch at operation " + std::to_string(i + 1));
        const auto bids_a = a.get_bids(5000), asks_a = a.get_asks(5000);
        require(bids_a == b.get_bids(5000) && bids_a == c.get_bids(5000), "bid mismatch at operation " + std::to_string(i + 1));
        require(asks_a == b.get_asks(5000) && asks_a == c.get_asks(5000), "ask mismatch at operation " + std::to_string(i + 1));
        reference.check(bids_a, true); reference.check(asks_a, false);
        if ((i + 1) % 100 == 0) {
            check_layout(c);
            require(a.get_bids(10) == b.get_bids(10) && a.get_bids(10) == c.get_bids(10), "top ten bids");
            require(a.get_asks(10) == b.get_asks(10) && a.get_asks(10) == c.get_asks(10), "top ten asks");
        }
        if ((i + 1) % 100000 == 0) std::cout << "verified_operations=" << i + 1 << std::endl;
    }
    std::cout << "Implementation A: PASS\nImplementation B: PASS\nImplementation C: PASS\n"
              << "All implementations and independent reference agree after every operation (full depth).\n"
              << "operations=" << workload.events.size() << " adds=" << workload.adds << " cancels=" << workload.cancels
              << " queries=" << workload.queries << " hash=" << workload.hash << '\n';
}
int main(int argc, char** argv) {
    try {
        std::size_t count = 20000;
        std::uint32_t seed = 42;
        if (argc > 3) throw std::invalid_argument("usage: verify [operations] [seed]");
        if (argc >= 2) count = static_cast<std::size_t>(std::stoull(argv[1]));
        if (argc == 3) seed = static_cast<std::uint32_t>(std::stoul(argv[2]));
        if (count == 0 || count > 100000000) throw std::invalid_argument("invalid operation count");
        edge_tests<ob::TreeBook>(); edge_tests<ob::VectorBook>(); edge_tests<ob::InterleavedBook>();
        layout_tests();
        std::cout << "Edge tests: PASS (all designs)\nseed=" << seed << std::endl;
        verify(ob::generate(count, seed));
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
