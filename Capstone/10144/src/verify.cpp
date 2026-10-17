#include "book_a.hpp"
#include "book_b.hpp"
#include "book_c.hpp"
#include "workload.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <sstream>

namespace {
using namespace ob;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
// Independent model: one ordered ID ledger and one combined-price aggregate map.
// Rebuild from ledger independently at every bounded step and long checkpoints.
class Reference {
    std::map<std::uint64_t, Order> live_;
    std::map<int, Qty> sums_;
public:
    void add_order(std::uint64_t id, Side side, int price, Qty qty) {
        require(live_.emplace(id, Order{id, side, price, qty}).second, "reference duplicate");
        sums_[price] += qty;
    }
    bool cancel_order(std::uint64_t id) {
        auto it = live_.find(id);
        if (it == live_.end()) return false;
        auto level = sums_.find(it->second.price);
        level->second -= it->second.qty;
        if (!level->second) sums_.erase(level);
        live_.erase(it);
        return true;
    }
    std::vector<Level> get_bids(int n = 10) const {
        std::vector<Level> out;
        auto it = sums_.lower_bound(10000);
        while (it != sums_.begin() && n-- > 0) { --it; out.push_back({it->first, it->second}); }
        return out;
    }
    std::vector<Level> get_asks(int n = 10) const {
        std::vector<Level> out;
        for (auto it = sums_.upper_bound(10000); it != sums_.end() && n-- > 0; ++it)
            out.push_back({it->first, it->second});
        return out;
    }
    BBO get_bbo() const {
        BBO b;
        auto it = sums_.lower_bound(10000);
        if (it != sums_.begin()) { --it; b.has_bid = true; b.bid_price = it->first; b.bid_qty = it->second; }
        it = sums_.upper_bound(10000);
        if (it != sums_.end()) { b.has_ask = true; b.ask_price = it->first; b.ask_qty = it->second; }
        return b;
    }
    void invariant() const {
        std::array<Qty, 15001> rebuilt{};
        for (const auto& item : live_) {
            const auto& o = item.second;
            require(o.id == item.first && o.qty > 0, "reference live-ID/quantity invariant");
            require(o.price >= 5000 && o.price <= 15000 && o.price != 10000,
                    "reference price invariant");
            rebuilt[static_cast<std::size_t>(o.price)] += o.qty;
        }
        std::size_t occupied = 0;
        for (const auto q : rebuilt) occupied += q != 0;
        require(occupied == sums_.size(), "reference occupied-level invariant");
        for (const auto& level : sums_)
            require(level.second > 0 && rebuilt[static_cast<std::size_t>(level.first)] == level.second,
                    "reference ledger versus aggregate invariant");
    }
};
std::string bbo_text(const BBO& b) {
    std::ostringstream s;
    s << b.has_bid << ':' << b.bid_price << 'x' << b.bid_qty << '/'
      << b.has_ask << ':' << b.ask_price << 'x' << b.ask_qty;
    return s.str();
}
void equal_levels(const std::vector<Level>& expected, const std::vector<Level>& actual,
                  const std::string& label) {
    if (expected == actual) return;
    std::ostringstream s;
    s << label << " expected size=" << expected.size() << " actual size=" << actual.size();
    const auto n = std::max(expected.size(), actual.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (i < expected.size() && i < actual.size() && expected[i] == actual[i]) continue;
        s << " first difference index=" << i;
        if (i < expected.size()) s << " expected=" << expected[i].price << 'x' << expected[i].qty;
        else s << " expected=<absent>";
        if (i < actual.size()) s << " actual=" << actual[i].price << 'x' << actual[i].qty;
        else s << " actual=<absent>";
        break;
    }
    throw std::runtime_error(s.str());
}
template<class Book> void compare(const Reference& ref, const Book& book, const std::string& label, int depth) {
    const BBO e = ref.get_bbo(), a = book.get_bbo();
    if (!(e == a)) throw std::runtime_error(label + " BBO expected=" + bbo_text(e) + " actual=" + bbo_text(a));
    equal_levels(ref.get_bids(depth), book.get_bids(depth), label + " bids");
    equal_levels(ref.get_asks(depth), book.get_asks(depth), label + " asks");
}
struct Suite {
    BookA a; BookB b; BookC c; Reference ref;
    void check(int depth) const {
        compare(ref, a, "A", depth); compare(ref, b, "B", depth); compare(ref, c, "C", depth);
    }
    void step(const Operation& op, bool full) {
        if (op.kind == Kind::Add) {
            const auto o = op.order;
            ref.add_order(o.id, o.side, o.price, o.qty);
            a.add_order(o.id, o.side, o.price, o.qty);
            b.add_order(o.id, o.side, o.price, o.qty);
            c.add_order(o.id, o.side, o.price, o.qty);
        } else if (op.kind == Kind::Cancel) {
            const bool e = ref.cancel_order(op.order.id);
            const bool ra = a.cancel_order(op.order.id), rb = b.cancel_order(op.order.id), rc = c.cancel_order(op.order.id);
            require(e == ra && e == rb && e == rc, "cancel expected=" + std::to_string(e) +
                    " actual A/B/C=" + std::to_string(ra) + '/' + std::to_string(rb) + '/' + std::to_string(rc));
        }
        check(10);
        if (full) { ref.invariant(); check(10001); }
    }
};
template<class Exception, class Fn> void rejects(Fn fn) {
    bool caught = false;
    try { fn(); } catch (const Exception&) { caught = true; }
    require(caught, "required exception missing");
}
template<class Book> void edge_tests() {
    Book book;
    require(book.get_bbo() == BBO{}, "empty deterministic BBO");
    require(!book.cancel_order(0), "empty cancel");
    require(book.get_bids().empty() && book.get_asks().empty(), "empty depth");
    const auto max_id = std::numeric_limits<std::uint64_t>::max();
    book.add_order(0, Side::Buy, 5000, 7);
    book.add_order(max_id, Side::Buy, 5000, 3);
    require(book.get_bbo().bid_qty == 10 && !book.get_bbo().has_ask, "single side aggregate");
    rejects<std::invalid_argument>([&] { book.add_order(0, Side::Buy, 5000, 1); });
    for (const auto side : {Side::Buy, Side::Sell}) {
        for (int p : {-1, 0, 4999, 10000, 15001, std::numeric_limits<int>::max()})
            rejects<std::invalid_argument>([&] { book.add_order(1, side, p, 1); });
    }
    rejects<std::invalid_argument>([&] { book.add_order(1, Side::Buy, 10001, 1); });
    rejects<std::invalid_argument>([&] { book.add_order(1, Side::Sell, 9999, 1); });
    rejects<std::invalid_argument>([&] { book.add_order(1, static_cast<Side>(-1), 5000, 1); });
    for (Qty q : {Qty{0}, Qty{-1}, std::numeric_limits<Qty>::min()})
        rejects<std::invalid_argument>([&] { book.add_order(1, Side::Buy, 5000, q); });
    require(book.get_bbo().bid_qty == 10 && book.get_bids(100).size() == 1, "invalid add unchanged");
    require(book.get_bids(0).empty() && book.get_bids(-1).empty() && book.get_asks(-9).empty(), "nonpositive depth");
    require(book.cancel_order(0) && book.get_bbo().bid_qty == 3, "partial level cancel");
    require(!book.cancel_order(0) && !book.cancel_order(12345), "double/unknown cancel");
    book.add_order(2, Side::Buy, 9999, 1);
    book.add_order(3, Side::Sell, 10001, 2);
    book.add_order(4, Side::Sell, 15000, 4);
    require(book.get_bids(1) == std::vector<Level>{{9999,1}}, "bid ordering/depth");
    require(book.get_asks(99) == std::vector<Level>{{10001,2},{15000,4}}, "ask ordering/endpoints");
    require(book.cancel_order(2) && book.get_bbo().bid_price == 5000, "remove best bid");
    require(book.cancel_order(3) && book.get_bbo().ask_price == 15000, "remove best ask");
    require(book.cancel_order(max_id) && !book.get_bbo().has_bid, "remove last bid");
    require(book.cancel_order(4) && book.get_bbo() == BBO{}, "remove final level");
    const Qty max_qty = std::numeric_limits<Qty>::max();
    book.add_order(5, Side::Sell, 10001, max_qty);
    rejects<std::overflow_error>([&] { book.add_order(6, Side::Sell, 10001, 1); });
    require(!book.cancel_order(6) && book.get_bbo().ask_qty == max_qty, "overflow atomicity");
    require(book.cancel_order(5) && book.get_bbo() == BBO{}, "maximum quantity cancel");
}
void replay_checked(const Trace& t, std::uint64_t seed, bool all_full) {
    Suite suite;
    suite.check(10001);
    for (std::size_t i = 0; i < t.ops.size(); ++i) {
        try {
            const bool full = all_full || (i+1)%10000 == 0 || i+1 == t.ops.size();
            suite.step(t.ops[i], full);
            if ((i+1)%100 == 0) suite.check(10);
        } catch (const std::exception& e) {
            throw std::runtime_error("seed=" + std::to_string(seed) + " operation=" + std::to_string(i) +
                                     " " + describe(t.ops[i]) + " " + e.what());
        }
    }
}
}
void verify(std::size_t count, std::uint64_t seed) {
    edge_tests<BookA>(); edge_tests<BookB>(); edge_tests<BookC>();
    std::cout << "PASS deterministic edges A/B/C: API, invalid input, uint64 extremes, overflow\n";
    // Sweep every valid tick; full-depth comparisons after every insertion/cancel.
    Suite all;
    for (std::uint64_t i = 0; i < 10000; ++i) {
        const Side side = i < 5000 ? Side::Buy : Side::Sell;
        const int price = i < 5000 ? 5000+static_cast<int>(i) : 10001+static_cast<int>(i-5000);
        try { all.step({Kind::Add, {i, side, price, 1}}, true); }
        catch (const std::exception& e) { throw std::runtime_error("all-tick add index=" + std::to_string(i) + " " + e.what()); }
    }
    // Multiplication by coprime 7919 visits each of 10,000 IDs exactly once.
    for (std::uint64_t i = 0; i < 10000; ++i) {
        const auto id = (i*7919)%10000;
        try { all.step({Kind::Cancel, {id, Side::Buy, 0, 0}}, true); }
        catch (const std::exception& e) { throw std::runtime_error("all-tick cancel index=" + std::to_string(i) + " id=" + std::to_string(id) + " " + e.what()); }
    }
    std::cout << "PASS all 10000 valid ticks + permuted drain, full depth and rebuilt ledger every step\n";
    for (auto s : {std::uint64_t{0}, std::uint64_t{7}, std::uint64_t{2026}}) {
        auto t = generate(2000, s);
        // Widen IDs while preserving add/cancel correspondence.
        for (auto& op : t.ops) op.order.id = std::numeric_limits<std::uint64_t>::max()-op.order.id*7919;
        replay_checked(t, s, true);
    }
    std::cout << "PASS bounded seeds 0,7,2026 (2000 each), sparse uint64 IDs, full depth every step\n";
    const auto trace = generate(count, seed);
    replay_checked(trace, seed, false);
    std::cout << "PASS primary replay seed=" << seed << " ops=" << count << " trace_hash=" << trace.hash
              << " adds=" << trace.adds << " cancels=" << trace.cancels << " BBO=" << trace.queries
              << "; BBO + top10 each step, full depth/rebuilt ledger each 10000 and final\n"
              << "Implementation A: PASS\nImplementation B: PASS\nImplementation C: PASS\n"
              << "All implementations and independent reference agree.\n";
}
