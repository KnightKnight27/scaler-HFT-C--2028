#include "impl_a/orderbook_a.hpp"
#include "impl_b/orderbook_b.hpp"
#include "impl_c/orderbook_c.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <random>
#include <sstream>
#include <string_view>
#include <map>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(__cplusplus >= 201703L);
static_assert(std::is_abstract_v<orderbook::OrderBook>);
static_assert(!std::is_copy_constructible_v<orderbook::OrderBookA>);
static_assert(!std::is_move_constructible_v<orderbook::OrderBookA>);

namespace orderbook {

// This friend is defined only in verification code, not in the library.
struct OrderBookATestAccess {
    static bool valid(const OrderBookA& book) {
        std::size_t count = 0;
        for (const Side side : {Side::bid, Side::ask}) {
            const auto& levels = side == Side::bid ? book.bids_ : book.asks_;
            for (auto level = levels.begin(); level != levels.end(); ++level) {
                if (level->second.orders.empty() || level->second.total <= 0) return false;
                Quantity sum = 0;
                for (auto node = level->second.orders.begin();
                     node != level->second.orders.end(); ++node) {
                    const Order& order = *node;
                    if (order.side != side || order.price != level->first || order.quantity <= 0)
                        return false;
                    const auto lo = side == Side::bid ? min_bid_ticks : min_ask_ticks;
                    const auto hi = side == Side::bid ? max_bid_ticks : max_ask_ticks;
                    if (order.price < lo || order.price > hi) return false;
                    if (order.quantity > std::numeric_limits<Quantity>::max() - sum) return false;
                    sum += order.quantity;
                    const auto loc = book.live_.find(order.id);
                    if (loc == book.live_.end() || loc->second.side != side ||
                        loc->second.level != level || loc->second.order != node ||
                        book.used_ids_.count(order.id) != 1) return false;
                    ++count;
                }
                if (sum != level->second.total) return false;
            }
        }
        return count == book.live_.size() && book.used_ids_.size() >= count;
    }

    static std::vector<OrderId> fifo(const OrderBookA& book, Side side, PriceTicks price) {
        const auto& levels = side == Side::bid ? book.bids_ : book.asks_;
        const auto level = levels.find(price);
        std::vector<OrderId> ids;
        if (level != levels.end()) {
            for (const auto& order : level->second.orders) ids.push_back(order.id);
        }
        return ids;
    }

    static std::size_t used_count(const OrderBookA& book) { return book.used_ids_.size(); }
};

struct OrderBookBTestAccess {
    static bool valid(const OrderBookB& book) {
        std::size_t count = 0;
        std::unordered_set<std::uint64_t> seqs;
        for (Side side : {Side::bid, Side::ask}) {
            const auto& heap = side == Side::bid ? book.bids_ : book.asks_;
            const auto& totals = side == Side::bid ? book.bid_totals_ : book.ask_totals_;
            std::map<PriceTicks, Quantity> expected;
            for (std::size_t pos = 0; pos < heap.size(); ++pos) {
                const auto& entry = heap[pos]; const auto& order = entry.order;
                const auto lo = side == Side::bid ? min_bid_ticks : min_ask_ticks;
                const auto hi = side == Side::bid ? max_bid_ticks : max_ask_ticks;
                if (order.side != side || order.price < lo || order.price > hi || order.quantity <= 0)
                    return false;
                if (!seqs.insert(entry.seq).second || entry.seq >= book.next_seq_) return false;
                const auto loc = book.live_.find(order.id);
                if (loc == book.live_.end() || loc->second.side != side || loc->second.pos != pos ||
                    book.used_ids_.count(order.id) != 1) return false;
                if (pos > 0) {
                    const auto& parent = heap[(pos - 1) / 2];
                    const bool better_price = side == Side::bid ? order.price > parent.order.price
                                                               : order.price < parent.order.price;
                    if (better_price || (order.price == parent.order.price && entry.seq < parent.seq))
                        return false;
                }
                auto& sum = expected[order.price];
                if (order.quantity > std::numeric_limits<Quantity>::max() - sum) return false;
                sum += order.quantity; ++count;
            }
            if (expected.size() != totals.size()) return false;
            for (const auto& level : expected) {
                const auto actual = totals.find(level.first);
                if (actual == totals.end() || actual->second != level.second) return false;
            }
        }
        return count == book.live_.size() && book.used_ids_.size() >= count;
    }
    static std::vector<OrderId> fifo(const OrderBookB& book, Side side, PriceTicks price) {
        const auto& heap = side == Side::bid ? book.bids_ : book.asks_;
        std::vector<std::pair<std::uint64_t, OrderId>> arrivals;
        for (const auto& entry : heap)
            if (entry.order.price == price) arrivals.push_back({entry.seq, entry.order.id});
        std::sort(arrivals.begin(), arrivals.end());
        std::vector<OrderId> ids;
        for (const auto& entry : arrivals) ids.push_back(entry.second);
        return ids;
    }
    static std::size_t used_count(const OrderBookB& book) { return book.used_ids_.size(); }
    static OrderId at(const OrderBookB& book, Side side, std::size_t pos) {
        return (side == Side::bid ? book.bids_ : book.asks_).at(pos).order.id;
    }
    static std::size_t size(const OrderBookB& book, Side side) {
        return (side == Side::bid ? book.bids_ : book.asks_).size();
    }
    static void exhaust_sequence(OrderBookB& book) {
        book.next_seq_ = std::numeric_limits<std::uint64_t>::max();
    }
};
struct OrderBookCTestAccess {
    static bool valid(const OrderBookC& book) {
        std::size_t count = 0;
        for (Side side : {Side::bid, Side::ask}) {
            const auto& levels = side == Side::bid ? book.bids_ : book.asks_;
            const auto& bits = side == Side::bid ? book.bid_bits_ : book.ask_bits_;
            // 5000 = 78*64 + 8: all remaining high bits must be zero.
            if ((bits[78] & ~std::uint64_t{255}) != 0) return false;
            for (std::size_t index = 0; index < 5000; ++index) {
                const auto& level = levels[index];
                const bool occupied = ((bits[index / 64] >> (index % 64)) & 1) != 0;
                if (occupied != !level.orders.empty()) return false;
                if (occupied ? level.total <= 0 : level.total != 0) return false;
                Quantity sum = 0;
                for (auto node = level.orders.begin(); node != level.orders.end(); ++node) {
                    const auto& order = *node;
                    const auto price = static_cast<PriceTicks>(index) + (side == Side::bid ? 5000 : 10001);
                    if (order.side != side || order.price != price || order.quantity <= 0) return false;
                    if (order.quantity > std::numeric_limits<Quantity>::max() - sum) return false;
                    sum += order.quantity;
                    const auto loc = book.live_.find(order.id);
                    if (loc == book.live_.end() || loc->second.side != side ||
                        loc->second.index != index || loc->second.order != node ||
                        book.used_ids_.count(order.id) != 1) return false;
                    ++count;
                }
                if (sum != level.total) return false;
            }
        }
        return count == book.live_.size() && book.used_ids_.size() >= count;
    }
    static std::vector<OrderId> fifo(const OrderBookC& book, Side side, PriceTicks price) {
        const auto& levels = side == Side::bid ? book.bids_ : book.asks_;
        const auto index = static_cast<std::size_t>(price - (side == Side::bid ? 5000 : 10001));
        std::vector<OrderId> ids;
        for (const auto& order : levels.at(index).orders) ids.push_back(order.id);
        return ids;
    }
    static std::size_t used_count(const OrderBookC& book) { return book.used_ids_.size(); }
    static std::uint64_t word(const OrderBookC& book, Side side, std::size_t w) {
        return (side == Side::bid ? book.bid_bits_ : book.ask_bits_).at(w);
    }
    static void set_unused_bits(OrderBookC& book, bool set) {
        book.bid_bits_[78] = set ? ~std::uint64_t{255} : 0;
        book.ask_bits_[78] = set ? ~std::uint64_t{255} : 0;
    }
    static unsigned bit(std::uint64_t word, bool highest) {
        return OrderBookC::bit_index(word, highest);
    }
};
} // namespace orderbook

namespace {
using namespace orderbook;
template <typename Book>
using Access = std::conditional_t<std::is_same_v<Book, OrderBookA>,
                                  OrderBookATestAccess,
                                  std::conditional_t<std::is_same_v<Book, OrderBookB>,
                                                     OrderBookBTestAccess, OrderBookCTestAccess>>;

constexpr Quantity max_qty = std::numeric_limits<Quantity>::max();
constexpr Depth all_depth = std::numeric_limits<Depth>::max();

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool same_levels(const DepthSnapshot& actual, const DepthSnapshot& expected) {
    if (actual.size() != expected.size()) return false;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (actual[i].price != expected[i].price ||
            actual[i].total_quantity != expected[i].total_quantity) return false;
    }
    return true;
}

void levels(const DepthSnapshot& actual, const DepthSnapshot& expected) {
    require(same_levels(actual, expected), "depth prices/quantities differ from expected");
}

void quote(const std::optional<PriceLevel>& actual, PriceTicks price, Quantity qty) {
    require(actual.has_value(), "expected BBO side is absent");
    require(actual->price == price && actual->total_quantity == qty, "incorrect BBO level");
}

template <typename Book>
void add(Book& book, OrderId id, Side side, PriceTicks price, Quantity qty,
         AddResult expected = AddResult::added) {
    const auto bids = book.get_bids(all_depth);
    const auto asks = book.get_asks(all_depth);
    const auto used = Access<Book>::used_count(book);
    require(book.add_order({id, side, price, qty}) == expected, "unexpected add result");
    require(Access<Book>::valid(book), "invariant violation after add");
    if (expected != AddResult::added) {
        levels(book.get_bids(all_depth), bids);
        levels(book.get_asks(all_depth), asks);
        require(Access<Book>::used_count(book) == used, "rejection consumed an ID");
    }
}

template <typename Book>
void cancel(Book& book, OrderId id, CancelResult expected = CancelResult::cancelled) {
    const auto bids = book.get_bids(all_depth);
    const auto asks = book.get_asks(all_depth);
    const auto used = Access<Book>::used_count(book);
    require(book.cancel_order(id) == expected, "unexpected cancellation result");
    require(Access<Book>::valid(book), "invariant violation after cancel");
    require(Access<Book>::used_count(book) == used, "cancel changed accepted-ID history");
    if (expected == CancelResult::not_found) {
        levels(book.get_bids(all_depth), bids);
        levels(book.get_asks(all_depth), asks);
    }
}

template <typename Book>
void fifo(const Book& book, Side side, PriceTicks price, std::vector<OrderId> expected) {
    require(Access<Book>::fifo(book, side, price) == expected, "FIFO sequence differs");
}

} // namespace

template <typename Book>
int run_contract(const char* name) {
    using namespace orderbook;
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"initial empty book", [] {
            Book b;
            require(!b.get_bbo().bid && !b.get_bbo().ask, "empty book has a quote");
            levels(b.get_bids(10), {}); levels(b.get_asks(10), {});
            require(Access<Book>::valid(b), "empty invariants");
        }},
        {"first bid and ID zero", [] {
            Book b; add(b, 0, Side::bid, 9000, 7);
            quote(b.get_bbo().bid, 9000, 7); require(!b.get_bbo().ask, "unexpected ask");
            levels(b.get_bids(1), {{9000, 7}}); levels(b.get_asks(1), {});
        }},
        {"first ask with independent empty bid", [] {
            Book b; add(b, 1, Side::ask, 11000, 9);
            quote(b.get_bbo().ask, 11000, 9); require(!b.get_bbo().bid, "unexpected bid");
            levels(b.get_asks(1), {{11000, 9}}); levels(b.get_bids(1), {});
        }},
        {"best prices change on add and cancel", [] {
            Book b;
            add(b, 1, Side::bid, 8000, 3); add(b, 2, Side::bid, 9900, 4);
            add(b, 3, Side::ask, 14000, 5); add(b, 4, Side::ask, 10100, 6);
            quote(b.get_bbo().bid, 9900, 4); quote(b.get_bbo().ask, 10100, 6);
            cancel(b, 2); cancel(b, 4);
            quote(b.get_bbo().bid, 8000, 3); quote(b.get_bbo().ask, 14000, 5);
        }},
        {"same-price aggregates on both sides", [] {
            Book b;
            add(b, 1, Side::bid, 9000, 7); add(b, 2, Side::bid, 9000, 11);
            add(b, 3, Side::ask, 11000, 13); add(b, 4, Side::ask, 11000, 17);
            levels(b.get_bids(10), {{9000, 18}}); levels(b.get_asks(10), {{11000, 30}});
            quote(b.get_bbo().bid, 9000, 18); quote(b.get_bbo().ask, 11000, 30);
        }},
        {"bid FIFO first middle last cancellation", [] {
            Book b;
            add(b, 90, Side::bid, 9000, 2); add(b, 10, Side::bid, 9000, 3);
            add(b, 70, Side::bid, 9000, 5); add(b, 20, Side::bid, 9000, 7);
            add(b, 50, Side::bid, 9000, 11);
            fifo(b, Side::bid, 9000, {90, 10, 70, 20, 50});
            cancel(b, 90); fifo(b, Side::bid, 9000, {10, 70, 20, 50});
            cancel(b, 70); fifo(b, Side::bid, 9000, {10, 20, 50});
            cancel(b, 50); fifo(b, Side::bid, 9000, {10, 20});
            levels(b.get_bids(10), {{9000, 10}});
            add(b, 5, Side::bid, 9000, 13); fifo(b, Side::bid, 9000, {10, 20, 5});
        }},
        {"ask FIFO first middle last cancellation", [] {
            Book b;
            add(b, 8, Side::ask, 11000, 2); add(b, 2, Side::ask, 11000, 3);
            add(b, 7, Side::ask, 11000, 5); add(b, 1, Side::ask, 11000, 7);
            add(b, 6, Side::ask, 11000, 11);
            fifo(b, Side::ask, 11000, {8, 2, 7, 1, 6});
            cancel(b, 8); cancel(b, 7); cancel(b, 6);
            fifo(b, Side::ask, 11000, {2, 1}); levels(b.get_asks(10), {{11000, 10}});
        }},
        {"final orders remove levels and empty sides", [] {
            Book b;
            add(b, 1, Side::bid, 9999, 2); add(b, 2, Side::bid, 9999, 3);
            add(b, 3, Side::ask, 10001, 4);
            cancel(b, 1); levels(b.get_bids(10), {{9999, 3}});
            cancel(b, 2); levels(b.get_bids(10), {});
            require(!b.get_bbo().bid, "bid level survived final cancellation");
            quote(b.get_bbo().ask, 10001, 4); cancel(b, 3);
            require(!b.get_bbo().bid && !b.get_bbo().ask, "book not empty");
            add(b, 4, Side::bid, 9999, 8); levels(b.get_bids(10), {{9999, 8}});
        }},
        {"unknown and repeated cancellations", [] {
            Book b; cancel(b, 77, CancelResult::not_found);
            add(b, 1, Side::bid, 9000, 8); cancel(b, 99, CancelResult::not_found);
            cancel(b, 1); cancel(b, 1, CancelResult::not_found);
            add(b, 77, Side::ask, 11000, 3); quote(b.get_bbo().ask, 11000, 3);
        }},
        {"live and cancelled IDs cannot be reused across sides", [] {
            Book b; add(b, 0, Side::bid, 9000, 4);
            add(b, 0, Side::ask, 11000, 6, AddResult::duplicate_id);
            cancel(b, 0);
            add(b, 0, Side::bid, 9000, 4, AddResult::duplicate_id);
            add(b, 0, Side::ask, 11000, 6, AddResult::duplicate_id);
            require(!b.get_bbo().bid && !b.get_bbo().ask, "reused ID entered book");
        }},
        {"rejected adds leave IDs available", [] {
            Book b;
            add(b, 1, Side::bid, 10000, 1, AddResult::invalid_price);
            add(b, 1, Side::bid, 9999, 1);
            add(b, 2, Side::ask, 10001, 0, AddResult::invalid_quantity);
            add(b, 2, Side::ask, 10001, 2);
            add(b, 3, static_cast<Side>(5), 9000, 1, AddResult::invalid_side);
            add(b, 3, Side::bid, 9000, 3);
        }},
        {"all four inclusive price boundaries", [] {
            Book b;
            add(b, 1, Side::bid, 5000, 1); add(b, 2, Side::bid, 9999, 2);
            add(b, 3, Side::ask, 10001, 3); add(b, 4, Side::ask, 15000, 4);
            levels(b.get_bids(10), {{9999, 2}, {5000, 1}});
            levels(b.get_asks(10), {{10001, 3}, {15000, 4}});
        }},
        {"invalid bid and ask prices", [] {
            Book b; OrderId id = 1;
            for (PriceTicks price : {4999, 10000, 10001, -1,
                                    std::numeric_limits<PriceTicks>::max()})
                add(b, id++, Side::bid, price, 1, AddResult::invalid_price);
            for (PriceTicks price : {10000, 15001, 9999, -1,
                                    std::numeric_limits<PriceTicks>::min()})
                add(b, id++, Side::ask, price, 1, AddResult::invalid_price);
            require(Access<Book>::used_count(b) == 0, "invalid prices consumed IDs");
        }},
        {"zero negative and minimum quantities", [] {
            Book b; OrderId id = 1;
            for (Side side : {Side::bid, Side::ask}) {
                for (Quantity qty : {Quantity{0}, Quantity{-1}, std::numeric_limits<Quantity>::min()})
                    add(b, id++, side, side == Side::bid ? 9000 : 11000, qty,
                        AddResult::invalid_quantity);
            }
            require(!b.get_bbo().bid && !b.get_bbo().ask, "invalid quantity stored");
        }},
        {"invalid sides and validation precedence", [] {
            Book b; add(b, 1, Side::bid, 9000, 5);
            add(b, 1, static_cast<Side>(-1), -1, -1, AddResult::invalid_side);
            add(b, 1, Side::bid, 10000, 0, AddResult::invalid_price);
            add(b, 1, Side::bid, 9000, 0, AddResult::invalid_quantity);
            add(b, 1, Side::bid, 9000, 1, AddResult::duplicate_id);
        }},
        {"bid quantity overflow rollback and ID retry", [] {
            Book b; add(b, 1, Side::bid, 9000, max_qty - 2);
            add(b, 2, Side::bid, 9000, 2); quote(b.get_bbo().bid, 9000, max_qty);
            add(b, 3, Side::bid, 9000, 1, AddResult::quantity_overflow);
            fifo(b, Side::bid, 9000, {1, 2});
            add(b, 4, Side::bid, 8999, max_qty);
            cancel(b, 2); add(b, 3, Side::bid, 9000, 1);
            levels(b.get_bids(10), {{9000, max_qty - 1}, {8999, max_qty}});
        }},
        {"ask maximum quantity overflow and recovery", [] {
            Book b; add(b, 1, Side::ask, 10001, max_qty);
            add(b, 2, Side::ask, 10001, max_qty, AddResult::quantity_overflow);
            quote(b.get_bbo().ask, 10001, max_qty); cancel(b, 1);
            add(b, 2, Side::ask, 10001, max_qty);
            levels(b.get_asks(10), {{10001, max_qty}});
        }},
        {"negative zero normal and oversized depth", [] {
            Book b;
            add(b, 1, Side::bid, 7000, 1); add(b, 2, Side::bid, 9900, 2);
            add(b, 3, Side::bid, 8000, 3); add(b, 4, Side::ask, 14000, 4);
            add(b, 5, Side::ask, 10100, 5); add(b, 6, Side::ask, 12000, 6);
            for (Depth depth : {std::numeric_limits<Depth>::min(), Depth{-1}, Depth{0}}) {
                levels(b.get_bids(depth), {}); levels(b.get_asks(depth), {});
            }
            levels(b.get_bids(2), {{9900, 2}, {8000, 3}});
            levels(b.get_asks(2), {{10100, 5}, {12000, 6}});
            levels(b.get_bids(all_depth), {{9900, 2}, {8000, 3}, {7000, 1}});
            levels(b.get_asks(all_depth), {{10100, 5}, {12000, 6}, {14000, 4}});
        }},
        {"owned snapshots remain stable after mutations", [] {
            Book b; add(b, 1, Side::bid, 9000, 5); add(b, 2, Side::ask, 11000, 7);
            auto bids = b.get_bids(10); auto asks = b.get_asks(10); const auto bbo = b.get_bbo();
            cancel(b, 1); cancel(b, 2); add(b, 3, Side::bid, 9999, 11);
            levels(bids, {{9000, 5}}); levels(asks, {{11000, 7}});
            quote(bbo.bid, 9000, 5); quote(bbo.ask, 11000, 7);
            bids.front().total_quantity = 123; asks.clear();
            levels(b.get_bids(10), {{9999, 11}}); levels(b.get_asks(10), {});
        }},
        {"uint64 maximum ID and interface dispatch", [] {
            Book b; OrderBook& api = b;
            const auto id = std::numeric_limits<OrderId>::max();
            require(api.add_order({id, Side::ask, 15000, 8}) == AddResult::added, "dispatch add");
            quote(api.get_bbo().ask, 15000, 8); levels(api.get_asks(1), {{15000, 8}});
            levels(api.get_bids(1), {});
            require(api.cancel_order(id) == CancelResult::cancelled, "dispatch cancel");
            require(api.add_order({id, Side::bid, 5000, 1}) == AddResult::duplicate_id, "ID history");
            require(Access<Book>::valid(b), "dispatch invariants");
        }}
    };
    std::size_t passed = 0;
    for (const auto& test : tests) {
        try {
            test.second(); ++passed;
            std::cout << name << " PASS: " << test.first << '\n';
        } catch (const std::exception& e) {
            std::cout << name << " FAIL: " << test.first << ": " << e.what() << '\n';
        } catch (...) {
            std::cout << name << " FAIL: " << test.first << ": unknown exception\n";
        }
    }
    std::cout << name << ": " << passed << " PASS, " << tests.size() - passed
              << " FAIL, " << tests.size() << " tests.\n";
    return passed == tests.size() ? 0 : 1;
}

int run_heap_tests() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"root cancellation repairs both heaps downward", [] {
            for (Side side : {Side::bid, Side::ask}) {
                OrderBookB b;
                for (int i = 0; i < 7; ++i)
                    add(b, static_cast<OrderId>(i + 1), side,
                        side == Side::bid ? 9900 - i * 100 : 10100 + i * 100, i + 1);
                require(OrderBookBTestAccess::at(b, side, 0) == 1, "wrong initial root");
                cancel(b, 1);
                require(OrderBookBTestAccess::at(b, side, 0) == 2, "replacement did not sift down");
                require(OrderBookBTestAccess::size(b, side) == 6, "not immediately removed");
            }
        }},
        {"middle cancellation repairs upward on both sides", [] {
            for (Side side : {Side::bid, Side::ask}) {
                OrderBookB b; OrderId id = 1;
                for (int priority : {100, 50, 90, 20, 40, 80, 85})
                    add(b, id++, side, side == Side::bid ? 9000 + priority : 11000 - priority, 1);
                require(OrderBookBTestAccess::at(b, side, 3) == 4, "upward fixture changed");
                require(OrderBookBTestAccess::at(b, side, 6) == 7, "replacement fixture changed");
                cancel(b, 4);
                require(OrderBookBTestAccess::at(b, side, 1) == 7, "replacement did not sift up");
            }
        }},
        {"middle cancellation repairs downward on both sides", [] {
            for (Side side : {Side::bid, Side::ask}) {
                OrderBookB b; OrderId id = 1;
                for (int priority : {100, 90, 80, 70, 60, 50, 40})
                    add(b, id++, side, side == Side::bid ? 9000 + priority : 11000 - priority, 1);
                cancel(b, 2);
                require(OrderBookBTestAccess::at(b, side, 1) == 4, "child did not move upward");
                require(OrderBookBTestAccess::at(b, side, 3) == 7, "replacement did not move downward");
            }
        }},
        {"last-element cancellation needs no repair", [] {
            for (Side side : {Side::bid, Side::ask}) {
                OrderBookB b;
                add(b, 1, side, side == Side::bid ? 9900 : 10100, 2);
                add(b, 2, side, side == Side::bid ? 9800 : 10200, 3);
                add(b, 3, side, side == Side::bid ? 9700 : 10300, 4);
                cancel(b, 3);
                require(OrderBookBTestAccess::at(b, side, 0) == 1 &&
                        OrderBookBTestAccess::at(b, side, 1) == 2, "last removal moved another order");
                cancel(b, 2); cancel(b, 1);
                require(OrderBookBTestAccess::size(b, side) == 0, "heap not empty");
            }
        }},
        {"equal-price roots follow insertion sequence", [] {
            for (Side side : {Side::bid, Side::ask}) {
                OrderBookB b; const PriceTicks price = side == Side::bid ? 9000 : 11000;
                for (OrderId id : {OrderId{90}, OrderId{10}, OrderId{70}, OrderId{20}})
                    add(b, id, side, price, 3);
                for (OrderId expected : {OrderId{90}, OrderId{10}, OrderId{70}, OrderId{20}}) {
                    require(OrderBookBTestAccess::at(b, side, 0) == expected, "tie order is not FIFO");
                    cancel(b, expected);
                }
            }
        }},
        {"repeated swaps growth and locator lookup", [] {
            OrderBookB b;
            for (OrderId id = 1; id <= 256; ++id) {
                const Side side = id % 2 == 0 ? Side::bid : Side::ask;
                const auto offset = static_cast<PriceTicks>((id * 37) % 5000);
                add(b, id, side, side == Side::bid ? 5000 + offset : 10001 + offset, 1);
            }
            // Odd stride permutes all 256 IDs; targets move repeatedly before cancellation.
            for (OrderId i = 0; i < 256; ++i) cancel(b, (i * 73) % 256 + 1);
            require(!b.get_bbo().bid && !b.get_bbo().ask, "stress book not empty");
            require(OrderBookBTestAccess::used_count(b) == 256, "lost accepted history");
            add(b, 1, Side::bid, 9000, 1, AddResult::duplicate_id);
        }},
        {"sequence exhaustion is detected without mutation", [] {
            OrderBookB b; add(b, 1, Side::bid, 9000, 4);
            OrderBookBTestAccess::exhaust_sequence(b);
            bool caught = false;
            try { b.add_order({2, Side::ask, 11000, 5}); }
            catch (const std::overflow_error&) { caught = true; }
            require(caught, "sequence exhaustion was not detected");
            levels(b.get_bids(10), {{9000, 4}}); levels(b.get_asks(10), {});
            require(OrderBookBTestAccess::used_count(b) == 1, "exhaustion consumed ID");
            require(OrderBookBTestAccess::valid(b), "exhaustion corrupted invariants");
            add(b, 2, Side::ask, 11000, 0, AddResult::invalid_quantity);
        }},
        {"deterministic A-B cross-verification after every operation", [] {
            OrderBookA a; OrderBookB b;
            auto compare = [&] {
                const auto x = a.get_bbo(); const auto y = b.get_bbo();
                require(x.bid.has_value() == y.bid.has_value() &&
                        x.ask.has_value() == y.ask.has_value(), "A/B BBO presence differs");
                if (x.bid) quote(y.bid, x.bid->price, x.bid->total_quantity);
                if (x.ask) quote(y.ask, x.ask->price, x.ask->total_quantity);
                for (Depth d : {Depth{-1}, Depth{0}, Depth{1}, Depth{3}, all_depth}) {
                    levels(b.get_bids(d), a.get_bids(d)); levels(b.get_asks(d), a.get_asks(d));
                }
                require(OrderBookATestAccess::valid(a) && OrderBookBTestAccess::valid(b),
                        "A/B internal invariant failure");
            };
            auto insert = [&](Order order) {
                require(a.add_order(order) == b.add_order(order), "A/B add results differ"); compare();
            };
            auto erase = [&](OrderId id) {
                require(a.cancel_order(id) == b.cancel_order(id), "A/B cancel results differ"); compare();
            };
            compare();
            insert({0, Side::bid, 9999, 3}); insert({1, Side::ask, 10001, max_qty});
            insert({2, Side::ask, 10001, 1}); erase(1);
            insert({2, Side::ask, 10001, 1}); insert({1, Side::ask, 11000, 1});
            insert({3, static_cast<Side>(5), 9000, 1});
            insert({3, Side::bid, 10000, 1}); insert({3, Side::bid, 9000, 0});
            insert({3, Side::bid, 9000, 5}); erase(999);
            for (OrderId id = 10; id < 138; ++id) {
                const Side side = id % 2 == 0 ? Side::bid : Side::ask;
                const auto offset = static_cast<PriceTicks>((id * 19) % 17);
                insert({id, side, side == Side::bid ? 8000 + offset : 12000 + offset,
                        static_cast<Quantity>(id % 7 + 1)});
                if (id >= 14 && id % 3 == 0) erase(id - 4);
            }
            for (OrderId id = 10; id < 138; ++id) { erase(id); erase(id); }
            erase(0); erase(2); erase(3);
            require(!a.get_bbo().bid && !a.get_bbo().ask, "cross-check did not drain book");
        }}
    };
    std::size_t passed = 0;
    for (const auto& test : tests) {
        try { test.second(); ++passed; std::cout << "B-specific PASS: " << test.first << '\n'; }
        catch (const std::exception& e) {
            std::cout << "B-specific FAIL: " << test.first << ": " << e.what() << '\n';
        }
    }
    std::cout << "B-specific/cross-check: " << passed << " PASS, " << tests.size() - passed
              << " FAIL, " << tests.size() << " tests.\n";
    return passed == tests.size() ? 0 : 1;
}

int run_bitmap_tests() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"tick endpoints map to first and last valid bits", [] {
            OrderBookC c;
            add(c, 1, Side::bid, 5000, 2); add(c, 2, Side::bid, 9999, 3);
            add(c, 3, Side::ask, 10001, 5); add(c, 4, Side::ask, 15000, 7);
            for (Side side : {Side::bid, Side::ask}) {
                require(OrderBookCTestAccess::word(c, side, 0) == 1, "first bit not set");
                require(OrderBookCTestAccess::word(c, side, 78) == 128, "last bit not set");
            }
            levels(c.get_bids(10), {{9999, 3}, {5000, 2}});
            levels(c.get_asks(10), {{10001, 5}, {15000, 7}});
            add(c, 5, Side::bid, 4999, 1, AddResult::invalid_price);
            add(c, 5, Side::bid, 10000, 1, AddResult::invalid_price);
            add(c, 5, Side::ask, 10000, 1, AddResult::invalid_price);
            add(c, 5, Side::ask, 15001, 1, AddResult::invalid_price);
        }},
        {"word boundaries 63-64 127-128 and 4991-4992", [] {
            OrderBookC c; OrderId id = 1;
            for (Side side : {Side::bid, Side::ask}) {
                for (int index : {63, 64, 127, 128, 4991, 4992})
                    add(c, id++, side, (side == Side::bid ? 5000 : 10001) + index, 1);
                require(OrderBookCTestAccess::word(c, side, 0) == (std::uint64_t{1} << 63), "word0");
                require(OrderBookCTestAccess::word(c, side, 1) == ((std::uint64_t{1} << 63) | 1), "word1");
                require(OrderBookCTestAccess::word(c, side, 2) == 1, "word2");
                require(OrderBookCTestAccess::word(c, side, 77) == (std::uint64_t{1} << 63), "word77");
                require(OrderBookCTestAccess::word(c, side, 78) == 1, "word78");
            }
            levels(c.get_bids(10), {{9992, 1}, {9991, 1}, {5128, 1}, {5127, 1}, {5064, 1}, {5063, 1}});
            levels(c.get_asks(10), {{10064, 1}, {10065, 1}, {10128, 1}, {10129, 1}, {14992, 1}, {14993, 1}});
        }},
        {"final partial word has exactly eight valid bits", [] {
            OrderBookC c; OrderId id = 1;
            for (Side side : {Side::bid, Side::ask}) {
                for (int index = 4992; index < 5000; ++index)
                    add(c, id++, side, (side == Side::bid ? 5000 : 10001) + index, 1);
                require(OrderBookCTestAccess::word(c, side, 78) == 255, "invalid final word mask");
            }
            quote(c.get_bbo().bid, 9999, 1); quote(c.get_bbo().ask, 14993, 1);
            for (OrderId key = 1; key <= 16; ++key) cancel(c, key);
            require(OrderBookCTestAccess::word(c, Side::bid, 78) == 0 &&
                    OrderBookCTestAccess::word(c, Side::ask, 78) == 0, "final word not cleared");
            // Deliberately corrupt only unused bits to exercise defensive query masking.
            OrderBookCTestAccess::set_unused_bits(c, true);
            require(!OrderBookCTestAccess::valid(c), "invariants missed unused bits");
            require(!c.get_bbo().bid && !c.get_bbo().ask, "query used an invalid array index");
            levels(c.get_bids(10), {}); levels(c.get_asks(10), {});
            OrderBookCTestAccess::set_unused_bits(c, false);
            require(OrderBookCTestAccess::valid(c), "failed to restore bitmap");
        }},
        {"best changes across words and complete empty transition", [] {
            OrderBookC c;
            add(c, 1, Side::bid, 5000, 2); add(c, 2, Side::bid, 5064, 3);
            add(c, 3, Side::bid, 9999, 4); add(c, 4, Side::ask, 10001, 5);
            add(c, 5, Side::ask, 10129, 6); add(c, 6, Side::ask, 15000, 7);
            cancel(c, 3); quote(c.get_bbo().bid, 5064, 3);
            cancel(c, 2); quote(c.get_bbo().bid, 5000, 2);
            cancel(c, 4); quote(c.get_bbo().ask, 10129, 6);
            cancel(c, 5); quote(c.get_bbo().ask, 15000, 7);
            cancel(c, 1); require(!c.get_bbo().bid, "bid did not empty");
            cancel(c, 6); require(!c.get_bbo().ask, "ask did not empty");
            add(c, 7, Side::ask, 10001, 8); quote(c.get_bbo().ask, 10001, 8);
        }},
        {"FIFO cancellation keeps bit until final node", [] {
            for (Side side : {Side::bid, Side::ask}) {
                OrderBookC c; const PriceTicks price = side == Side::bid ? 5064 : 10065;
                add(c, 90, side, price, 2); add(c, 10, side, price, 3); add(c, 70, side, price, 5);
                cancel(c, 10); fifo(c, side, price, {90, 70});
                require(OrderBookCTestAccess::word(c, side, 1) == 1, "bit cleared prematurely");
                cancel(c, 90); fifo(c, side, price, {70});
                cancel(c, 70); require(OrderBookCTestAccess::word(c, side, 1) == 0, "bit remained");
                add(c, 5, side, price, 11); fifo(c, side, price, {5});
                add(c, 10, side, price, 1, AddResult::duplicate_id);
            }
        }},
        {"overflow and used IDs leave bitmap unchanged", [] {
            OrderBookC c;
            add(c, 0, Side::bid, 9999, max_qty);
            add(c, 1, Side::bid, 9999, 1, AddResult::quantity_overflow);
            require(OrderBookCTestAccess::word(c, Side::bid, 78) == 128, "overflow changed bits");
            cancel(c, 999, CancelResult::not_found); cancel(c, 0);
            add(c, 0, Side::ask, 10001, 1, AddResult::duplicate_id);
            add(c, 1, Side::bid, 9999, 2); quote(c.get_bbo().bid, 9999, 2);
            cancel(c, 0, CancelResult::not_found);
        }},
        {"sparse cross-word aggregated depth and owned snapshots", [] {
            OrderBookC c;
            add(c, 1, Side::bid, 5000, 2); add(c, 2, Side::bid, 7000, 3);
            add(c, 3, Side::bid, 9999, 5); add(c, 4, Side::bid, 7000, 7);
            add(c, 5, Side::ask, 10001, 11); add(c, 6, Side::ask, 13000, 13);
            add(c, 7, Side::ask, 15000, 17);
            const auto bids = c.get_bids(all_depth); const auto asks = c.get_asks(all_depth);
            levels(bids, {{9999, 5}, {7000, 10}, {5000, 2}});
            levels(asks, {{10001, 11}, {13000, 13}, {15000, 17}});
            levels(c.get_bids(2), {{9999, 5}, {7000, 10}});
            levels(c.get_asks(2), {{10001, 11}, {13000, 13}});
            cancel(c, 3); cancel(c, 5);
            levels(bids, {{9999, 5}, {7000, 10}, {5000, 2}});
            levels(asks, {{10001, 11}, {13000, 13}, {15000, 17}});
        }},
        {"locator consistency through index growth and arbitrary removals", [] {
            OrderBookC c;
            for (OrderId id = 1; id <= 256; ++id) {
                const Side side = id % 2 == 0 ? Side::bid : Side::ask;
                const auto index = static_cast<PriceTicks>((id * 37) % 5000);
                add(c, id, side, (side == Side::bid ? 5000 : 10001) + index, 1);
            }
            for (OrderId i = 0; i < 256; ++i) cancel(c, (i * 73) % 256 + 1);
            require(!c.get_bbo().bid && !c.get_bbo().ask, "locator test did not drain book");
            require(OrderBookCTestAccess::used_count(c) == 256, "accepted history lost");
        }},
        {"portable bit selection for every nonzero bit position", [] {
            for (unsigned bit = 0; bit < 64; ++bit) {
                const auto word = std::uint64_t{1} << bit;
                require(OrderBookCTestAccess::bit(word, true) == bit, "highest single bit wrong");
                require(OrderBookCTestAccess::bit(word, false) == bit, "lowest single bit wrong");
                require(OrderBookCTestAccess::bit(word | 1, true) == bit, "highest mixed bit wrong");
                require(OrderBookCTestAccess::bit(word | 1, false) == 0, "lowest mixed bit wrong");
            }
        }},
        {"deterministic A-B-C cross-verification after each operation", [] {
            OrderBookA a; OrderBookB b; OrderBookC c;
            auto compare = [&] {
                const auto x = a.get_bbo();
                for (OrderBook* book : {static_cast<OrderBook*>(&b), static_cast<OrderBook*>(&c)}) {
                    const auto y = book->get_bbo();
                    require(x.bid.has_value() == y.bid.has_value() &&
                            x.ask.has_value() == y.ask.has_value(), "ABC BBO flags differ");
                    if (x.bid) quote(y.bid, x.bid->price, x.bid->total_quantity);
                    if (x.ask) quote(y.ask, x.ask->price, x.ask->total_quantity);
                    for (Depth d : {Depth{-1}, Depth{0}, Depth{1}, Depth{3}, all_depth}) {
                        levels(book->get_bids(d), a.get_bids(d));
                        levels(book->get_asks(d), a.get_asks(d));
                    }
                }
                require(OrderBookATestAccess::valid(a) && OrderBookBTestAccess::valid(b) &&
                        OrderBookCTestAccess::valid(c), "ABC internal invariants differ");
            };
            auto insert = [&](Order order) {
                const auto result = a.add_order(order);
                require(b.add_order(order) == result && c.add_order(order) == result, "ABC add differs");
                compare();
            };
            auto erase = [&](OrderId id) {
                const auto result = a.cancel_order(id);
                require(b.cancel_order(id) == result && c.cancel_order(id) == result, "ABC cancel differs");
                compare();
            };
            compare(); insert({0, Side::bid, 9999, max_qty});
            insert({1, Side::bid, 9999, 1}); erase(0); insert({1, Side::bid, 9999, 1});
            insert({0, Side::ask, 15000, 1});
            const auto max_id = std::numeric_limits<OrderId>::max();
            insert({max_id, Side::ask, 10001, 7});
            insert({2, Side::ask, 15001, 1}); insert({2, Side::ask, 10001, 0});
            insert({2, static_cast<Side>(5), 10001, 1}); erase(999);
            for (OrderId id = 10; id < 138; ++id) {
                const Side side = id % 2 == 0 ? Side::bid : Side::ask;
                const auto index = static_cast<PriceTicks>((id * 127) % 5000);
                insert({id, side, (side == Side::bid ? 5000 : 10001) + index,
                        static_cast<Quantity>(id % 11 + 1)});
                if (id % 5 == 0) erase(id - 1);
            }
            for (OrderId id = 10; id < 138; ++id) { erase(id); erase(id); }
            erase(1); erase(max_id);
            require(!c.get_bbo().bid && !c.get_bbo().ask, "ABC book did not empty");
        }}
    };
    std::size_t passed = 0;
    for (const auto& test : tests) {
        try { test.second(); ++passed; std::cout << "C-specific PASS: " << test.first << '\n'; }
        catch (const std::exception& e) {
            std::cout << "C-specific FAIL: " << test.first << ": " << e.what() << '\n';
        }
    }
    std::cout << "C-specific/ABC cross-check: " << passed << " PASS, " << tests.size() - passed
              << " FAIL, " << tests.size() << " tests.\n";
    return passed == tests.size() ? 0 : 1;
}

int run_deterministic() {
    const int a = run_contract<orderbook::OrderBookA>("A contract");
    const int b = run_contract<orderbook::OrderBookB>("B contract");
    const int c = run_contract<orderbook::OrderBookC>("C contract");
    const int heaps = run_heap_tests();
    const int bitmap = run_bitmap_tests();
    std::cout << "Total: 78 cases (20 A, 20 B contract, 20 C contract, 7 B-specific, "
                 "9 C-specific, 1 A/B cross-check, 1 A/B/C cross-check).\n";
    return a || b || c || heaps || bitmap;
}

namespace {
// Independent oracle for the generated dense-ID trace: append-only order records
// and ascending sorted vectors of aggregate levels. No tree, heap, bitmap,
// production locator, or production query helper is used.
class ReferenceModel {
    struct Record { Order order; bool active; };
    std::vector<Record> records_;
    DepthSnapshot bids_;
    DepthSnapshot asks_;

    static auto find_level(DepthSnapshot& levels, PriceTicks price) {
        return std::lower_bound(levels.begin(), levels.end(), price,
            [](const PriceLevel& level, PriceTicks p) { return level.price < p; });
    }

public:
    AddResult add_order(const Order& order) {
        if (order.side != Side::bid && order.side != Side::ask) return AddResult::invalid_side;
        if (order.price < (order.side == Side::bid ? 5000 : 10001) ||
            order.price > (order.side == Side::bid ? 9999 : 15000)) return AddResult::invalid_price;
        if (order.quantity <= 0) return AddResult::invalid_quantity;
        if (order.id < records_.size()) return AddResult::duplicate_id;
        if (order.id != records_.size()) throw std::runtime_error("oracle requires dense trace IDs");
        auto& levels = order.side == Side::bid ? bids_ : asks_;
        auto level = find_level(levels, order.price);
        if (level != levels.end() && level->price == order.price) {
            if (order.quantity > max_qty - level->total_quantity) return AddResult::quantity_overflow;
            level->total_quantity += order.quantity;
        } else {
            levels.insert(level, {order.price, order.quantity});
        }
        records_.push_back({order, true});
        return AddResult::added;
    }

    CancelResult cancel_order(OrderId id) {
        if (id >= records_.size() || !records_[static_cast<std::size_t>(id)].active)
            return CancelResult::not_found;
        auto& record = records_[static_cast<std::size_t>(id)];
        auto& levels = record.order.side == Side::bid ? bids_ : asks_;
        auto level = find_level(levels, record.order.price);
        require(level != levels.end() && level->price == record.order.price, "oracle missing level");
        level->total_quantity -= record.order.quantity;
        if (level->total_quantity == 0) levels.erase(level);
        record.active = false; // The record itself is permanent ID history.
        return CancelResult::cancelled;
    }

    Bbo get_bbo() const {
        Bbo result;
        if (!bids_.empty()) result.bid = bids_.back();
        if (!asks_.empty()) result.ask = asks_.front();
        return result;
    }
    DepthSnapshot depth(Side side, Depth count) const {
        DepthSnapshot result;
        if (count <= 0) return result;
        const auto& levels = side == Side::bid ? bids_ : asks_;
        const auto size = std::min<Depth>(count, static_cast<Depth>(levels.size()));
        for (Depth i = 0; i < size; ++i) {
            const auto pos = side == Side::bid ? levels.size() - 1 - static_cast<std::size_t>(i)
                                              : static_cast<std::size_t>(i);
            result.push_back(levels[pos]);
        }
        return result;
    }
    bool audit(std::size_t expected_live) const {
        std::size_t live = 0;
        for (Side side : {Side::bid, Side::ask}) {
            DepthSnapshot rebuilt;
            for (const auto& record : records_) {
                if (record.active && record.order.side == side) {
                    rebuilt.push_back({record.order.price, record.order.quantity}); ++live;
                }
            }
            std::sort(rebuilt.begin(), rebuilt.end(), [](const PriceLevel& a, const PriceLevel& b) {
                return a.price < b.price;
            });
            DepthSnapshot sums;
            for (const auto& level : rebuilt) {
                if (!sums.empty() && sums.back().price == level.price)
                    sums.back().total_quantity += level.total_quantity;
                else sums.push_back(level);
            }
            if (!same_levels(sums, side == Side::bid ? bids_ : asks_)) return false;
        }
        return live == expected_live;
    }
};

const char* result_name(AddResult result) {
    switch (result) {
    case AddResult::added: return "added";
    case AddResult::invalid_side: return "invalid_side";
    case AddResult::invalid_price: return "invalid_price";
    case AddResult::invalid_quantity: return "invalid_quantity";
    case AddResult::duplicate_id: return "duplicate_id";
    case AddResult::quantity_overflow: return "quantity_overflow";
    }
    return "invalid AddResult enum";
}
const char* result_name(CancelResult result) {
    return result == CancelResult::cancelled ? "cancelled" : "not_found";
}
std::string describe(const std::optional<PriceLevel>& level) {
    if (!level) return "absent";
    return "price=" + std::to_string(level->price) + ",qty=" + std::to_string(level->total_quantity);
}

struct RandomContext {
    std::uint64_t seed;
    std::uint64_t index = 0; // One-based primary operation; 0 is initial empty state.
    std::string operation = "initial empty state";
    [[noreturn]] void fail(const char* impl, const std::string& check,
                          const std::string& expected, const std::string& observed) const {
        std::cerr << "RANDOM MISMATCH seed=" << seed << " op_index=" << index
                  << " operation={" << operation << "} implementation=" << impl
                  << " check=" << check << " expected={" << expected << "} observed={"
                  << observed << "}\n";
        throw std::runtime_error("random verification failed");
    }
    void levels(const char* impl, const char* check, const DepthSnapshot& expected,
                const DepthSnapshot& observed) const {
        const auto count = std::min(expected.size(), observed.size());
        for (std::size_t i = 0; i < count; ++i) {
            if (expected[i].price != observed[i].price ||
                expected[i].total_quantity != observed[i].total_quantity)
                fail(impl, std::string(check) + " level=" + std::to_string(i),
                     describe(expected[i]), describe(observed[i]));
        }
        if (expected.size() != observed.size())
            fail(impl, check, "size=" + std::to_string(expected.size()),
                 "size=" + std::to_string(observed.size()));
    }
};

int run_random(std::uint64_t ops, std::uint64_t seed) {
    const auto started = std::chrono::steady_clock::now();
    OrderBookA a; OrderBookB b; OrderBookC c;
    ReferenceModel model;
    std::vector<OrderId> live_ids;
    OrderId next_id = 0;
    std::mt19937_64 rng(seed);
    // Explicit rejection sampling avoids standard-library distribution differences.
    auto draw = [&](std::uint64_t bound) {
        const auto threshold = (std::uint64_t{0} - bound) % bound;
        std::uint64_t value;
        do { value = rng(); } while (value < threshold);
        return value % bound;
    };
    const std::array<OrderBook*, 3> books{&a, &b, &c};
    const std::array<const char*, 3> names{"A", "B", "C"};
    RandomContext context{seed, 0, "initial empty state"};
    std::uint64_t adds = 0, cancels = 0, bbos = 0, bids = 0, asks = 0, checkpoints = 0;
    auto compare = [&](bool full) {
        const auto expected = model.get_bbo();
        const auto expected_bids = model.depth(Side::bid, full ? all_depth : 10);
        const auto expected_asks = model.depth(Side::ask, full ? all_depth : 10);
        for (std::size_t i = 0; i < books.size(); ++i) {
            try {
                const auto observed = books[i]->get_bbo();
                if (describe(expected.bid) != describe(observed.bid))
                    context.fail(names[i], "BBO bid", describe(expected.bid), describe(observed.bid));
                if (describe(expected.ask) != describe(observed.ask))
                    context.fail(names[i], "BBO ask", describe(expected.ask), describe(observed.ask));
                context.levels(names[i], full ? "full bids" : "bids(10)", expected_bids,
                               books[i]->get_bids(full ? all_depth : 10));
                context.levels(names[i], full ? "full asks" : "asks(10)", expected_asks,
                               books[i]->get_asks(full ? all_depth : 10));
            } catch (const std::exception& e) {
                context.fail(names[i], "query/comparison", "successful query", e.what());
            }
        }
    };
    auto checkpoint = [&] {
        if (!model.audit(live_ids.size())) context.fail("reference", "audit", "valid", "invalid");
        if (!OrderBookATestAccess::valid(a)) context.fail("A", "invariants", "valid", "invalid");
        if (!OrderBookBTestAccess::valid(b)) context.fail("B", "invariants", "valid", "invalid");
        if (!OrderBookCTestAccess::valid(c)) context.fail("C", "invariants", "valid", "invalid");
        compare(true); ++checkpoints;
    };
    checkpoint();
    for (std::uint64_t index = 0; index < ops; ++index) {
        context.index = index + 1;
        const auto choice = draw(100);
        // Empty-state cancellation choices become adds, preserving live-only cancellation.
        if (choice < 70 || (choice < 90 && live_ids.empty())) {
            const Side side = draw(2) == 0 ? Side::bid : Side::ask;
            const PriceTicks price = (side == Side::bid ? 5000 : 10001) + static_cast<PriceTicks>(draw(5000));
            const Quantity qty = static_cast<Quantity>(draw(100) + 1);
            const Order order{next_id, side, price, qty};
            context.operation = "add id=" + std::to_string(order.id) + " side=" +
                (side == Side::bid ? "bid" : "ask") + " ticks=" + std::to_string(price) +
                " qty=" + std::to_string(qty);
            const auto expected = model.add_order(order);
            if (expected != AddResult::added) context.fail("reference", "valid add", "added", result_name(expected));
            for (std::size_t i = 0; i < books.size(); ++i) {
                try {
                    const auto observed = books[i]->add_order(order);
                    if (observed != expected) context.fail(names[i], "add result", result_name(expected), result_name(observed));
                } catch (const std::exception& e) {
                    context.fail(names[i], "add/exception", result_name(expected), e.what());
                }
            }
            live_ids.push_back(next_id++); ++adds;
            if (side == Side::bid) ++bids; else ++asks;
        } else if (choice < 90) {
            const auto pos = static_cast<std::size_t>(draw(live_ids.size()));
            const auto id = live_ids[pos];
            context.operation = "cancel id=" + std::to_string(id);
            const auto expected = model.cancel_order(id);
            if (expected != CancelResult::cancelled) context.fail("reference", "live cancel", "cancelled", result_name(expected));
            for (std::size_t i = 0; i < books.size(); ++i) {
                try {
                    const auto observed = books[i]->cancel_order(id);
                    if (observed != expected) context.fail(names[i], "cancel result", result_name(expected), result_name(observed));
                } catch (const std::exception& e) {
                    context.fail(names[i], "cancel/exception", result_name(expected), e.what());
                }
            }
            live_ids[pos] = live_ids.back(); live_ids.pop_back(); ++cancels;
        } else {
            context.operation = "get_bbo"; ++bbos;
        }
        // Always check BBO and top-ten snapshots, including checkpoint operations.
        compare(false);
        if (context.index % 1000 == 0) checkpoint();
        if (context.index % 10000 == 0)
            std::cout << "Random progress seed=" << seed << " ops=" << context.index << '\n' << std::flush;
    }
    if (ops % 1000 != 0) checkpoint();
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << "RANDOM PASS seed=" << seed << " ops=" << ops << " adds=" << adds
              << " cancels=" << cancels << " bbo=" << bbos << " bid_adds=" << bids
              << " ask_adds=" << asks << " live=" << live_ids.size()
              << " full/invariant_checkpoints=" << checkpoints
              << " verification_seconds=" << seconds << '\n';
    return 0;
}

std::uint64_t parse_number(std::string_view value) {
    std::uint64_t number = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || value.empty())
        throw std::invalid_argument("expected an unsigned decimal integer: " + std::string(value));
    return number;
}
} // namespace

int main(int argc, char** argv) {
    try {
        bool random = false, configured = false;
        std::uint64_t ops = 100000, seed = 42;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--help") {
                std::cout << "verify [--random [--ops N] [--seed N]]\n"
                             "Always runs 78 deterministic cases; random defaults: ops=100000 seed=42.\n";
                return 0;
            }
            if (arg == "--random") random = true;
            else if (arg == "--ops" || arg == "--seed") {
                if (++i == argc) throw std::invalid_argument("missing value for " + std::string(arg));
                const auto value = parse_number(argv[i]);
                if (arg == "--ops") ops = value; else seed = value;
                configured = true;
            } else throw std::invalid_argument("unknown option: " + std::string(arg));
        }
        if (configured && !random) throw std::invalid_argument("--ops/--seed require --random");
        if (run_deterministic() != 0) return 1;
        return random ? run_random(ops, seed) : 0;
    } catch (const std::exception& e) {
        std::cerr << "Verification error: " << e.what() << '\n';
        return 1;
    }
}
