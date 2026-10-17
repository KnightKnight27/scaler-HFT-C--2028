#include "order_book.hpp"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>

using Clock = std::chrono::steady_clock;
enum class Kind { Add, Cancel, Query };

struct Operation {
    Kind kind;
    std::uint64_t id;
    Side side;
    int price;
    Qty qty;
};

// Generate once, outside the timer. Swap/pop keeps random cancels live.
std::vector<Operation> make_workload(std::size_t count, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<Operation> operations;
    std::vector<std::uint64_t> live;
    operations.reserve(count);
    live.reserve(count);
    std::uint64_t next_id = 1;
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned choice = rng() % 10;
        if (choice < 7 || (choice < 9 && live.empty())) {
            const Side side = rng() % 2 == 0 ? Side::Buy : Side::Sell;
            const int price = (side == Side::Buy ? 5000 : 10001) + rng() % 5000;
            const Qty qty = 1 + rng() % 100;
            operations.push_back({Kind::Add, next_id, side, price, qty});
            live.push_back(next_id++);
        } else if (choice < 9) {
            const std::size_t slot = rng() % live.size();
            operations.push_back({Kind::Cancel, live[slot], Side::Buy, 0, 0});
            live[slot] = live.back();
            live.pop_back();
        } else {
            operations.push_back({Kind::Query, 0, Side::Buy, 0, 0});
        }
    }
    return operations;
}

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Book>
void edge_tests() {
    Book book;
    require(book.get_bbo() == BBO{}, "empty BBO");
    require(book.get_bids().empty() && book.get_asks().empty(), "empty depth");
    require(!book.cancel_order(123), "unknown cancellation");
    const auto high_id = std::numeric_limits<std::uint64_t>::max();
    book.add_order(0, Side::Buy, 5000, 10);
    book.add_order(high_id, Side::Buy, 9999, 20);
    book.add_order(2, Side::Buy, 9999, 30);
    book.add_order(3, Side::Sell, 10001, 40);
    book.add_order(4, Side::Sell, 15000, 50);
    require(book.get_bbo() == BBO{true, 9999, 50, true, 10001, 40}, "aggregated BBO");
    require(book.get_bids() == std::vector<Level>{{9999, 50}, {5000, 10}}, "bid order/depth");
    require(book.get_asks() == std::vector<Level>{{10001, 40}, {15000, 50}}, "ask order/depth");
    require(book.get_bids(1) == std::vector<Level>{{9999, 50}}, "limited bid depth");
    require(book.get_asks(1) == std::vector<Level>{{10001, 40}}, "limited ask depth");
    require(book.get_bids(0).empty() && book.get_asks(-1).empty(), "nonpositive depth");
    require(book.cancel_order(high_id), "known cancellation");
    require(book.get_bbo().bid_qty == 30, "partial level cancellation");
    require(!book.cancel_order(high_id), "repeated cancellation");
    require(book.cancel_order(2) && book.get_bbo().bid_price == 5000, "best bid removed");
    require(book.cancel_order(3) && book.get_bbo().ask_price == 15000, "best ask removed");
    require(book.cancel_order(0) && !book.get_bbo().has_bid, "one-sided book");
    require(book.cancel_order(4) && book.get_bbo() == BBO{}, "drained book");
    book.add_order(5, Side::Sell, 11000, 7);
    require(book.get_bbo() == BBO{false, 0, 0, true, 11000, 7}, "add after empty");
    bool rejected = false;
    try { book.add_order(5, Side::Sell, 12000, 1); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && book.get_asks() == std::vector<Level>{{11000, 7}}, "duplicate ID");
    for (const Order invalid : {Order{Side::Buy, 10000, 1}, Order{Side::Sell, 10000, 1},
                               Order{Side::Buy, 4999, 1}, Order{Side::Sell, 15001, 1},
                               Order{Side::Buy, 5000, 0}, Order{Side::Sell, 10001, -1}}) {
        rejected = false;
        try { book.add_order(6, invalid.side, invalid.price, invalid.qty); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "invalid order accepted");
    }
    require(book.get_asks() == std::vector<Level>{{11000, 7}}, "invalid add changed book");
}

template<class Book>
bool apply(Book& book, const Operation& op) {
    if (op.kind == Kind::Add) book.add_order(op.id, op.side, op.price, op.qty);
    else if (op.kind == Kind::Cancel) return book.cancel_order(op.id);
    else (void)book.get_bbo();
    return true;
}

void verify(const std::vector<Operation>& operations) {
    edge_tests<MapBook>();
    edge_tests<VectorBook>();
    edge_tests<ArrayBook>();
    MapBook a;
    VectorBook b;
    ArrayBook c;
    for (std::size_t i = 0; i < operations.size(); ++i) {
        require(apply(a, operations[i]) && apply(b, operations[i]) && apply(c, operations[i]),
                "live cancellation failed at operation " + std::to_string(i + 1));
        const auto bbo = a.get_bbo();
        const auto bids = a.get_bids(5000);
        const auto asks = a.get_asks(5000);
        require(bbo == b.get_bbo() && bbo == c.get_bbo() &&
                bids == b.get_bids(5000) && bids == c.get_bids(5000) &&
                asks == b.get_asks(5000) && asks == c.get_asks(5000),
                "books differ at operation " + std::to_string(i + 1));
    }
    std::cout << "Edge cases: PASS\n"
              << "Implementation A: PASS\nImplementation B: PASS\nImplementation C: PASS\n"
              << "Compared BBO and every bid/ask level after all " << operations.size()
              << " operations.\nAll implementations produce identical results.\n";
}

// Unsigned arithmetic deliberately wraps. Consume every query and depth result.
void mix(std::uint64_t& checksum, std::uint64_t value) {
    checksum ^= value + 0x9e3779b97f4a7c15ULL + (checksum << 6) + (checksum >> 2);
}

void consume(std::uint64_t& checksum, const BBO& bbo) {
    mix(checksum, bbo.has_bid);
    mix(checksum, bbo.bid_price);
    mix(checksum, static_cast<std::uint64_t>(bbo.bid_qty));
    mix(checksum, bbo.has_ask);
    mix(checksum, bbo.ask_price);
    mix(checksum, static_cast<std::uint64_t>(bbo.ask_qty));
}

void consume(std::uint64_t& checksum, const std::vector<Level>& levels) {
    mix(checksum, levels.size());
    for (const auto& level : levels) {
        mix(checksum, level.price);
        mix(checksum, static_cast<std::uint64_t>(level.qty));
    }
}

struct Measurement {
    double total_ms = 0;
    std::array<double, 3> ns{};
    std::array<std::size_t, 3> counts{};
    std::uint64_t checksum = 0;
    BBO final_bbo;
};

// Timed=false is for perf: same workload, without per-operation clock calls.
template<class Book, bool Timed>
Measurement run(const std::vector<Operation>& operations) {
    Book book;
    Measurement result;
    std::array<double, 3> elapsed{};
    const auto start = Clock::now();
    for (std::size_t i = 0; i < operations.size(); ++i) {
        const auto& op = operations[i];
        const auto kind = static_cast<std::size_t>(op.kind);
        Clock::time_point before;
        if constexpr (Timed) before = Clock::now();
        BBO bbo;
        bool cancelled = true;
        if (op.kind == Kind::Add) book.add_order(op.id, op.side, op.price, op.qty);
        else if (op.kind == Kind::Cancel) cancelled = book.cancel_order(op.id);
        else bbo = book.get_bbo();
        if constexpr (Timed)
            elapsed[kind] += std::chrono::duration<double, std::nano>(Clock::now() - before).count();
        ++result.counts[kind];
        if (!cancelled) throw std::runtime_error("workload cancelled a non-live order");
        if (op.kind == Kind::Query) consume(result.checksum, bbo);
        if ((i + 1) % 100 == 0) {
            consume(result.checksum, book.get_bids(10));
            consume(result.checksum, book.get_asks(10));
        }
    }
    result.total_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    result.final_bbo = book.get_bbo();
    // Consume the full final state too, including levels outside the top ten.
    consume(result.checksum, result.final_bbo);
    consume(result.checksum, book.get_bids(5000));
    consume(result.checksum, book.get_asks(5000));
    for (std::size_t i = 0; i < elapsed.size(); ++i)
        result.ns[i] = result.counts[i] ? elapsed[i] / result.counts[i] : 0;
    return result;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
}

void print_bbo(const BBO& bbo) {
    std::cout << "Final BBO: ";
    if (bbo.has_bid) std::cout << "bid " << bbo.bid_price / 100.0 << " x " << bbo.bid_qty;
    else std::cout << "no bid";
    if (bbo.has_ask) std::cout << ", ask " << bbo.ask_price / 100.0 << " x " << bbo.ask_qty;
    else std::cout << ", no ask";
    std::cout << '\n';
}

template<class Book>
void benchmark(char impl, const std::vector<Operation>& operations, int repeats,
               bool profile, const std::string& csv_path) {
    std::ofstream csv;
    if (!csv_path.empty()) {
        csv.open(csv_path);
        require(csv.is_open(), "cannot open CSV output");
        csv << "impl,run,operations,total_ms,ns_add,ns_cancel,ns_bbo,checksum\n";
        csv << std::fixed << std::setprecision(3);
    }
    std::vector<double> totals, adds, cancels, queries;
    std::uint64_t expected = 0;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Implementation " << impl << (profile ? " (perf mode)" : " (timed mode)") << '\n';
    for (int repeat = 1; repeat <= repeats; ++repeat) {
        const auto r = profile ? run<Book, false>(operations) : run<Book, true>(operations);
        if (repeat == 1) {
            expected = r.checksum;
            std::cout << "Operations: " << operations.size() << " (add " << r.counts[0]
                      << ", cancel " << r.counts[1] << ", BBO " << r.counts[2] << ")\n"
                      << "Depth queries per side: " << operations.size() / 100 << '\n';
        }
        require(r.checksum == expected, "checksum changed between repeats");
        totals.push_back(r.total_ms);
        adds.push_back(r.ns[0]);
        cancels.push_back(r.ns[1]);
        queries.push_back(r.ns[2]);
        std::cout << "Run " << repeat << ": " << r.total_ms << " ms";
        if (!profile) std::cout << "; add " << r.ns[0] << "; cancel " << r.ns[1]
                                << "; BBO " << r.ns[2] << " ns";
        std::cout << "; checksum " << r.checksum << '\n';
        if (csv.is_open()) csv << impl << ',' << repeat << ',' << operations.size() << ','
                              << r.total_ms << ',' << r.ns[0] << ',' << r.ns[1] << ','
                              << r.ns[2] << ',' << r.checksum << '\n';
        if (repeat == repeats) print_bbo(r.final_bbo);
    }
    std::cout << "Median total: " << median(totals) << " ms\n";
    if (!profile) std::cout << "Median ns/add: " << median(adds)
                           << "\nMedian ns/cancel: " << median(cancels)
                           << "\nMedian ns/BBO: " << median(queries) << '\n';
}

int main(int argc, char** argv) {
    try {
        char impl = 'A';
        bool checking = false, profile = false, explicit_ops = false;
        int repeats = 5;
        unsigned seed = 42;
        std::size_t count = 1000000;
        std::string csv_path;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (i + 1 >= argc) throw std::invalid_argument("missing value for " + arg);
                return argv[++i];
            };
            if (arg == "--impl") {
                const auto text = value();
                if (text.size() != 1 || text[0] < 'A' || text[0] > 'C')
                    throw std::invalid_argument("--impl must be A, B or C");
                impl = text[0];
            } else if (arg == "--verify") checking = true;
            else if (arg == "--profile") profile = true;
            else if (arg == "--runs") repeats = std::stoi(value());
            else if (arg == "--seed") seed = static_cast<unsigned>(std::stoul(value()));
            else if (arg == "--ops") {
                const auto text = value();
                if (text.empty() || text[0] == '-') throw std::invalid_argument("invalid --ops");
                count = std::stoull(text);
                explicit_ops = true;
            } else if (arg == "--csv") csv_path = value();
            else if (arg == "--help") {
                std::cout << "Usage: ./bench [--impl A|B|C] [--runs 5] [--ops 1000000]\n"
                          << "               [--seed 42] [--csv results.csv] [--profile]\n"
                          << "       ./bench --verify [--ops 20000] [--seed 42]\n";
                return 0;
            } else throw std::invalid_argument("unknown option: " + arg);
        }
        require(repeats > 0 && count > 0, "--runs and --ops must be positive");
        if (checking && !explicit_ops) count = 20000;
        const auto operations = make_workload(count, seed);
        std::cout << "Seed: " << seed << '\n';
        if (checking) verify(operations);
        else if (impl == 'A') benchmark<MapBook>(impl, operations, repeats, profile, csv_path);
        else if (impl == 'B') benchmark<VectorBook>(impl, operations, repeats, profile, csv_path);
        else benchmark<ArrayBook>(impl, operations, repeats, profile, csv_path);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
