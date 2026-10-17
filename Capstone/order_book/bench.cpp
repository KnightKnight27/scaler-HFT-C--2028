#include "order_books.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace orderbook;
using Clock = std::chrono::steady_clock;

struct Operation {
    enum class Kind { Add, Cancel, Bbo };
    Kind kind;
    std::uint64_t id{};
    Side side{};
    double price{};
    long qty{};
};

std::vector<Operation> make_workload(std::size_t count) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> choice(0, 99);
    std::uniform_int_distribution<int> quantity(1, 100);
    std::uniform_int_distribution<int> bid_tick(kMinBidTick, kMaxBidTick);
    std::uniform_int_distribution<int> ask_tick(kMinAskTick, kMaxAskTick);
    std::vector<std::uint64_t> live_ids;
    std::vector<Operation> operations;
    operations.reserve(count);
    std::uint64_t next_id = 1;
    for (std::size_t i = 0; i < count; ++i) {
        const int action = choice(rng);
        if (action < 70 || live_ids.empty()) {
            const Side side = choice(rng) < 50 ? Side::Buy : Side::Sell;
            const Tick tick = side == Side::Buy ? bid_tick(rng) : ask_tick(rng);
            const std::uint64_t id = next_id++;
            live_ids.push_back(id);
            operations.push_back({Operation::Kind::Add, id, side, to_price(tick), quantity(rng)});
        } else if (action < 90) {
            std::uniform_int_distribution<std::size_t> pick(0, live_ids.size() - 1);
            const auto index = pick(rng);
            const auto id = live_ids[index];
            live_ids[index] = live_ids.back();
            live_ids.pop_back();
            operations.push_back({Operation::Kind::Cancel, id});
        } else {
            operations.push_back({Operation::Kind::Bbo});
        }
    }
    return operations;
}

void apply(OrderBook& book, const Operation& operation) {
    switch (operation.kind) {
    case Operation::Kind::Add: book.add_order(operation.id, operation.side, operation.price, operation.qty); break;
    case Operation::Kind::Cancel: book.cancel_order(operation.id); break;
    case Operation::Kind::Bbo: static_cast<void>(book.get_bbo()); break;
    }
}

std::string describe(const BBO& bbo) {
    std::ostringstream out;
    out << (bbo.has_bid ? std::to_string(bbo.bid_price) + " x " + std::to_string(bbo.bid_qty) : "none")
        << " | "
        << (bbo.has_ask ? std::to_string(bbo.ask_price) + " x " + std::to_string(bbo.ask_qty) : "none");
    return out.str();
}

bool verify_edge_cases() {
    for (const char impl : {'A', 'B', 'C'}) {
        auto book = make_order_book(impl);
        if (!(book->get_bbo() == BBO{})) return false;
        book->add_order(1, Side::Buy, 50.00, 10);
        book->add_order(2, Side::Buy, 50.00, 5);
        book->add_order(3, Side::Buy, 51.00, 7);
        book->add_order(4, Side::Sell, 100.01, 9);
        if (book->get_bids(10).size() != 2 || !(book->get_bids(10)[0] == Level{51.00, 7}) ||
            !(book->get_bids(10)[1] == Level{50.00, 15})) return false;
        if (!book->cancel_order(2) || book->cancel_order(2) || book->cancel_order(99)) return false;
        if (!(book->get_bids(10)[1] == Level{50.00, 10})) return false;
        bool rejected = false;
        try {
            book->add_order(5, Side::Buy, 50.005, 1);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        if (!rejected) return false;
        rejected = false;
        try {
            book->add_order(6, Side::Sell, 100.00, 1);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        if (!rejected) return false;
    }
    return true;
}

bool verify_workload() {
    const auto operations = make_workload(1000000);
    auto a = make_order_book('A');
    auto b = make_order_book('B');
    auto c = make_order_book('C');
    for (const auto& operation : operations) {
        apply(*a, operation);
        apply(*b, operation);
        apply(*c, operation);
        const BBO a_bbo = a->get_bbo();
        const BBO b_bbo = b->get_bbo();
        const BBO c_bbo = c->get_bbo();
        const auto a_bids = a->get_bids();
        const auto b_bids = b->get_bids();
        const auto c_bids = c->get_bids();
        const auto a_asks = a->get_asks();
        const auto b_asks = b->get_asks();
        const auto c_asks = c->get_asks();
        if (!(a_bbo == b_bbo && a_bbo == c_bbo && a_bids == b_bids && a_bids == c_bids &&
              a_asks == b_asks && a_asks == c_asks)) {
            return false;
        }
    }
    return true;
}

struct Timing {
    long long total_ns{};
    long long add_ns{};
    long long cancel_ns{};
    long long bbo_ns{};
    long add_count{};
    long cancel_count{};
    long bbo_count{};
    std::string checksum;
};

Timing run_once(char impl, const std::vector<Operation>& operations) {
    auto book = make_order_book(impl);
    Timing result;
    const auto start = Clock::now();
    for (const auto& operation : operations) {
        const auto op_start = Clock::now();
        apply(*book, operation);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - op_start).count();
        if (operation.kind == Operation::Kind::Add) { result.add_ns += elapsed; ++result.add_count; }
        if (operation.kind == Operation::Kind::Cancel) { result.cancel_ns += elapsed; ++result.cancel_count; }
        if (operation.kind == Operation::Kind::Bbo) { result.bbo_ns += elapsed; ++result.bbo_count; }
        if (&operation != &operations.back() && (&operation - operations.data() + 1) % 100 == 0) {
            static_cast<void>(book->get_bids());
            static_cast<void>(book->get_asks());
        }
    }
    result.total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    result.checksum = describe(book->get_bbo()) + "|" + std::to_string(book->get_bids().size()) + "|" +
                      std::to_string(book->get_asks().size());
    return result;
}

long long median(std::vector<long long> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

void benchmark(char impl) {
    const auto operations = make_workload(1000000);
    std::vector<Timing> runs;
    for (int i = 0; i < 5; ++i) runs.push_back(run_once(impl, operations));
    std::vector<long long> total, add, cancel, bbo;
    for (const auto& run : runs) {
        total.push_back(run.total_ns);
        add.push_back(run.add_ns);
        cancel.push_back(run.cancel_ns);
        bbo.push_back(run.bbo_ns);
    }
    std::cout << implementation_name(impl) << "\n"
              << "median total ms: " << std::fixed << std::setprecision(3) << median(total) / 1e6 << "\n"
              << "median add ns: " << median(add) / static_cast<double>(runs.front().add_count) << "\n"
              << "median cancel ns: " << median(cancel) / static_cast<double>(runs.front().cancel_count) << "\n"
              << "median BBO ns: " << median(bbo) / static_cast<double>(runs.front().bbo_count) << "\n"
              << "checksum: " << runs.front().checksum << "\n";
}

int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--verify") {
            const bool pass = verify_edge_cases() && verify_workload();
            std::cout << "Correctness: " << (pass ? "PASS" : "FAIL") << "\n";
            return pass ? 0 : 1;
        }
        char impl = 'A';
        if (argc == 3 && std::string(argv[1]) == "--impl") impl = argv[2][0];
        if (argc != 1 && !(argc == 3 && std::string(argv[1]) == "--impl")) {
            std::cerr << "usage: bench [--verify | --impl A|B|C]\n";
            return 2;
        }
        benchmark(impl);
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 2;
    }
}
