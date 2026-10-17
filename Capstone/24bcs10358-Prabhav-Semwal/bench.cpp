#include "order_book.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

enum class Kind { Add, Cancel, Query };

struct Operation {
    Kind kind;
    std::uint64_t id = 0;
    Side side = Side::Buy;
    int price = 0;
    long qty = 0;
};

std::vector<Operation> make_workload(std::size_t count) {
    std::mt19937 rng(42);
    std::vector<Operation> ops;
    std::vector<std::uint64_t> live;
    ops.reserve(count);
    live.reserve(count);
    std::uint64_t next_id = 1;
    for (std::size_t i = 0; i < count; ++i) {
        auto choice = rng() % 100;
        if (choice < 70 || live.empty()) {
            Side side = rng() % 2 == 0 ? Side::Buy : Side::Sell;
            int price = (side == Side::Buy ? 5000 : 10001) + rng() % 5000;
            long qty = 1 + rng() % 100;
            ops.push_back({Kind::Add, next_id, side, price, qty});
            live.push_back(next_id++);
        } else if (choice < 90) {
            std::size_t index = rng() % live.size();
            ops.push_back({Kind::Cancel, live[index]});
            live[index] = live.back();
            live.pop_back();
        } else {
            ops.push_back({Kind::Query});
        }
    }
    return ops;
}

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <class Book>
void apply(Book& book, const Operation& op) {
    if (op.kind == Kind::Add)
        book.add_order(op.id, op.side, op.price, op.qty);
    else if (op.kind == Kind::Cancel)
        check(book.cancel_order(op.id), "workload cancelled a non-live ID");
    else
        book.get_bbo();
}

template <class Book>
void edge_tests() {
    Book book;
    check(book.get_bbo() == BBO{}, "empty BBO");
    check(book.get_bids().empty() && book.get_asks().empty(), "empty depth");
    check(!book.cancel_order(7), "unknown cancellation");
    book.add_order(0, Side::Buy, 5000, 3);
    book.add_order(UINT64_MAX, Side::Buy, 9999, 7);
    book.add_order(2, Side::Buy, 9999, 5);
    book.add_order(3, Side::Sell, 15000, 9);
    book.add_order(4, Side::Sell, 10001, 11);
    check(book.get_bids(10) == std::vector<Level>{{9999, 12}, {5000, 3}}, "bid aggregation/order");
    check(book.get_asks(10) == std::vector<Level>{{10001, 11}, {15000, 9}}, "ask aggregation/order");
    check(book.get_bids(0).empty() && book.get_asks(-1).empty(), "nonpositive depth");
    check(book.get_bids(1) == std::vector<Level>{{9999, 12}}, "depth limit");
    check(book.cancel_order(UINT64_MAX), "large ID cancellation");
    check(book.get_bbo().bid_qty == 5, "partial level cancellation");
    check(book.cancel_order(2) && book.get_bbo().bid_price == 5000, "best bid removal");
    check(book.cancel_order(4) && book.get_bbo().ask_price == 15000, "best ask removal");
    check(!book.cancel_order(4), "repeated cancellation");
    for (auto order : std::vector<Order>{{Side::Buy, 4999, 1}, {Side::Buy, 10000, 1},
             {Side::Sell, 10000, 1}, {Side::Sell, 15001, 1}, {Side::Buy, 5000, 0},
             {Side::Sell, 10001, -1}, {static_cast<Side>(7), 5000, 1}}) {
        bool threw = false;
        try { book.add_order(99, order.side, order.price, order.qty); }
        catch (const std::invalid_argument&) { threw = true; }
        check(threw, "invalid input accepted");
    }
    bool duplicate = false;
    try { book.add_order(0, Side::Buy, 7000, 2); }
    catch (const std::invalid_argument&) { duplicate = true; }
    check(duplicate, "duplicate live ID accepted");
    check(book.cancel_order(0) && book.cancel_order(3), "final cancellations");
    check(book.get_bbo() == BBO{}, "book not empty after cancellations");
    book.add_order(100, Side::Sell, 10001, 1);
    check(!book.get_bbo().has_bid && book.get_bbo().has_ask, "one-sided book");
}

void compare(const MapBook& a, const VectorBook& b, const ArrayBook& c, int depth) {
    auto bbo = a.get_bbo();
    auto bids = a.get_bids(depth);
    auto asks = a.get_asks(depth);
    check(bbo == b.get_bbo() && bbo == c.get_bbo(), "BBO mismatch");
    check(bids == b.get_bids(depth) && bids == c.get_bids(depth), "bid level mismatch");
    check(asks == b.get_asks(depth) && asks == c.get_asks(depth), "ask level mismatch");
}

void reference_test() {
    MapBook a;
    VectorBook b;
    ArrayBook c;
    std::map<std::uint64_t, Order> reference;
    for (const auto& op : make_workload(2000)) {
        apply(a, op);
        apply(b, op);
        apply(c, op);
        if (op.kind == Kind::Add) reference.emplace(op.id, Order{op.side, op.price, op.qty});
        if (op.kind == Kind::Cancel) check(reference.erase(op.id) == 1, "reference cancel");
        std::map<int, long, std::greater<int>> bids;
        std::map<int, long> asks;
        for (const auto& entry : reference) {
            const auto& order = entry.second;
            if (order.side == Side::Buy) bids[order.price] += order.qty;
            else asks[order.price] += order.qty;
        }
        std::vector<Level> expected_bids, expected_asks;
        for (auto level : bids) expected_bids.push_back({level.first, level.second});
        for (auto level : asks) expected_asks.push_back({level.first, level.second});
        check(a.get_bids(5000) == expected_bids && a.get_asks(5000) == expected_asks,
              "independent reference mismatch");
        compare(a, b, c, 5000);
    }
}

void verify(const std::vector<Operation>& ops) {
    std::cout << "Running correctness test...\n";
    edge_tests<MapBook>();
    edge_tests<VectorBook>();
    edge_tests<ArrayBook>();
    reference_test();
    MapBook a;
    VectorBook b;
    ArrayBook c;
    for (std::size_t i = 0; i < ops.size(); ++i) {
        try {
            apply(a, ops[i]);
            apply(b, ops[i]);
            apply(c, ops[i]);
            compare(a, b, c, (i + 1) % 10000 == 0 ? 5000 : 10);
        } catch (const std::exception& error) {
            throw std::runtime_error("operation " + std::to_string(i + 1) + ": " + error.what());
        }
    }
    compare(a, b, c, 5000);
    std::cout << "Implementation A: PASS\nImplementation B: PASS\nImplementation C: PASS\n"
              << "All implementations produce identical results.\n"
              << "Checked " << ops.size() << " operations; BBO and top-10 after every operation,\n"
              << "full depth every 10000 operations and at the end.\n"
              << "Edge cases and independent full-depth reference test: PASS\n";
}

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) {
    return (hash ^ value) * UINT64_C(1099511628211);
}

std::uint64_t consume(std::uint64_t hash, const BBO& bbo) {
    hash = mix(hash, bbo.has_bid);
    hash = mix(hash, bbo.bid_price);
    hash = mix(hash, bbo.bid_qty);
    hash = mix(hash, bbo.has_ask);
    hash = mix(hash, bbo.ask_price);
    return mix(hash, bbo.ask_qty);
}

struct Result {
    double total, add, cancel, bbo;
    BBO final;
    std::uint64_t checksum;
};

template <class Book>
Result run_once(const std::vector<Operation>& ops, bool timed) {
    Book book;
    using Clock = std::chrono::steady_clock;
    long long add_ns = 0, cancel_ns = 0, bbo_ns = 0;
    std::size_t adds = 0, cancels = 0, queries = 0;
    std::uint64_t checksum = UINT64_C(14695981039346656037);
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < ops.size(); ++i) {
        const auto& op = ops[i];
        Clock::time_point start;
        if (op.kind == Kind::Add) {
            if (timed) start = Clock::now();
            book.add_order(op.id, op.side, op.price, op.qty);
            if (timed) add_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
            ++adds;
        } else if (op.kind == Kind::Cancel) {
            if (timed) start = Clock::now();
            bool cancelled = book.cancel_order(op.id);
            if (timed) cancel_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
            check(cancelled, "non-live benchmark cancel");
            ++cancels;
        } else {
            if (timed) start = Clock::now();
            auto bbo = book.get_bbo();
            if (timed) bbo_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
            checksum = consume(checksum, bbo);
            ++queries;
        }
        if ((i + 1) % 100 == 0) {
            for (auto level : book.get_bids(10)) {
                checksum = mix(checksum, level.price);
                checksum = mix(checksum, level.qty);
            }
            for (auto level : book.get_asks(10)) {
                checksum = mix(checksum, level.price);
                checksum = mix(checksum, level.qty);
            }
        }
    }
    double total = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    auto final = book.get_bbo();
    checksum = consume(checksum, final);
    for (auto level : book.get_bids(5000)) {
        checksum = mix(checksum, level.price);
        checksum = mix(checksum, level.qty);
    }
    for (auto level : book.get_asks(5000)) {
        checksum = mix(checksum, level.price);
        checksum = mix(checksum, level.qty);
    }
    return {total, adds ? double(add_ns) / adds : 0, cancels ? double(cancel_ns) / cancels : 0,
            queries ? double(bbo_ns) / queries : 0, final, checksum};
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    std::size_t middle = values.size() / 2;
    return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
}

template <class Book>
void benchmark(char impl, const std::vector<Operation>& ops, int runs, bool profile) {
    std::vector<double> total, add, cancel, bbo;
    BBO final;
    std::uint64_t checksum = 0;
    std::cout << "trial,total_ms,ns_add,ns_cancel,ns_bbo,checksum\n";
    for (int i = 0; i < runs; ++i) {
        auto result = run_once<Book>(ops, !profile);
        if (i) check(result.checksum == checksum && result.final == final, "trials disagree");
        checksum = result.checksum;
        final = result.final;
        total.push_back(result.total);
        add.push_back(result.add);
        cancel.push_back(result.cancel);
        bbo.push_back(result.bbo);
        std::cout << i + 1 << ',' << result.total << ',' << result.add << ','
                  << result.cancel << ',' << result.bbo << ',' << checksum << '\n';
    }
    std::cout << "Implementation " << impl << (profile ? " profile replay" : " median") << '\n'
              << "Total time (ms): " << median(total) << '\n';
    if (!profile)
        std::cout << "ns / add: " << median(add) << "\nns / cancel: " << median(cancel)
                  << "\nns / BBO: " << median(bbo) << '\n';
    std::cout << "Final BBO (integer ticks):\nBid: " << final.bid_price << " x " << final.bid_qty
              << "\nAsk: " << final.ask_price << " x " << final.ask_qty
              << "\nChecksum: " << checksum << '\n';
}

std::size_t number(const std::string& text) {
    std::size_t used = 0;
    check(!text.empty() && text.front() != '-', "expected positive number");
    auto value = std::stoull(text, &used);
    check(used == text.size() && value > 0, "expected positive number");
    return value;
}

int main(int argc, char** argv) {
    try {
        char impl = 0;
        bool verification = false, profile = false;
        std::size_t count = 1000000;
        int runs = 5;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--verify") verification = true;
            else if (arg == "--profile") profile = true;
            else if (arg == "--impl" && i + 1 < argc) {
                std::string value = argv[++i];
                check(value.size() == 1 && value[0] >= 'A' && value[0] <= 'C', "implementation must be A, B or C");
                impl = value[0];
            } else if (arg == "--ops" && i + 1 < argc) {
                count = number(argv[++i]);
                check(count <= 1000000, "--ops maximum is 1000000");
            } else if (arg == "--runs" && i + 1 < argc) {
                auto value = number(argv[++i]);
                check(value <= 100, "--runs maximum is 100");
                runs = static_cast<int>(value);
            } else {
                throw std::invalid_argument("Usage: ./bench --verify | --impl A|B|C [--runs N] [--profile] [--ops N]");
            }
        }
        check(verification != (impl != 0), "choose --verify or --impl A|B|C");
        check(!verification || !profile, "--profile requires --impl");
        auto ops = make_workload(count);
        std::size_t adds = 0, cancels = 0, queries = 0;
        for (const auto& op : ops) {
            if (op.kind == Kind::Add) ++adds;
            else if (op.kind == Kind::Cancel) ++cancels;
            else ++queries;
        }
        std::cout << std::fixed << std::setprecision(3)
                  << "Seed: 42; operations: " << count << "; add: " << adds
                  << "; cancel: " << cancels << "; BBO: " << queries
                  << "; depth calls per side: " << count / 100 << '\n';
        if (verification) verify(ops);
        else if (impl == 'A') benchmark<MapBook>(impl, ops, profile ? 1 : runs, profile);
        else if (impl == 'B') benchmark<VectorBook>(impl, ops, profile ? 1 : runs, profile);
        else benchmark<ArrayBook>(impl, ops, profile ? 1 : runs, profile);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
