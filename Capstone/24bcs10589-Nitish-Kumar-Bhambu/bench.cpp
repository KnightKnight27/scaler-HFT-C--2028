#include "order_book.hpp"
#include "map_order_book.hpp"
#include "ring_order_book.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error("Check failed: " + message);
    }
}

void print_bbo(const BBO& bbo) {
    if (bbo.has_bid) {
        std::cout << "Best bid: " << bbo.bid_price << ", quantity: " << bbo.bid_qty << '\n';
    } else {
        std::cout << "No bids\n";
    }
    if (bbo.has_ask) {
        std::cout << "Best ask: " << bbo.ask_price << ", quantity: " << bbo.ask_qty << '\n';
    } else {
        std::cout << "No asks\n";
    }
}

template<class Book>
void verify_one() {
    Book book;
    BBO empty = book.get_bbo();
    check(!empty.has_bid && !empty.has_ask, "empty book");
    check(book.get_bids().empty() && book.get_asks().empty(), "empty depth");
    check(!book.cancel_order(100), "unknown cancellation");

    book.add_order(1, Side::Buy, 9950, 37);
    book.add_order(2, Side::Sell, 10200, 10);
    book.add_order(3, Side::Buy, 9950, 13);
    book.add_order(4, Side::Buy, 9900, 8);
    book.add_order(5, Side::Sell, 10200, 6);
    book.add_order(6, Side::Sell, 10300, 12);

    BBO bbo = book.get_bbo();
    check(bbo.has_bid && bbo.bid_price == 9950 && bbo.bid_qty == 50, "best bid aggregation");
    check(bbo.has_ask && bbo.ask_price == 10200 && bbo.ask_qty == 16, "best ask aggregation");
    print_bbo(bbo);

    std::vector<Level> bids = book.get_bids(10);
    std::vector<Level> asks = book.get_asks(10);
    check(bids.size() == 2 && asks.size() == 2, "insufficient depth");
    check(bids[0].price == 9950 && bids[0].qty == 50 && bids[1].price == 9900 && bids[1].qty == 8, "bid depth");
    check(asks[0].price == 10200 && asks[0].qty == 16 && asks[1].price == 10300 && asks[1].qty == 12, "ask depth");
    check(book.get_bids(1).size() == 1 && book.get_bids(1)[0].qty == 50, "depth counts levels, not orders");
    check(book.get_asks(1).size() == 1 && book.get_asks(1)[0].qty == 16, "ask depth aggregation");
    check(book.get_bids(0).empty() && book.get_asks(-1).empty(), "nonpositive depth");

    check(book.cancel_order(1), "cancel a live order");
    check(book.get_bbo().bid_qty == 13, "remaining quantity");
    check(!book.cancel_order(1), "repeated cancellation");
    check(book.cancel_order(3) && book.get_bbo().bid_price == 9900, "best bid changes");
    check(book.cancel_order(4) && !book.get_bbo().has_bid, "no bids remain");
    check(book.cancel_order(2) && book.cancel_order(5), "remove best ask level");
    check(book.get_bbo().ask_price == 10300, "best ask changes");
    check(book.cancel_order(6) && !book.get_bbo().has_ask, "book drained");

    book.add_order(7, Side::Buy, 5000, 1);
    book.add_order(8, Side::Buy, 9999, 2);
    book.add_order(9, Side::Sell, 15000, 4);
    book.add_order(10, Side::Sell, 10001, 3);
    check(book.get_bbo().bid_price == 9999 && book.get_bbo().ask_price == 10001, "price boundaries");
    bool rejected = false;
    try {
        book.add_order(11, Side::Sell, 1020, 1);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected && !book.cancel_order(11), "invalid price rejected");

    rejected = false;
    try {
        book.add_order(7, Side::Buy, 5000, 1);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected && book.get_bids(10)[1].qty == 1, "duplicate live ID rejected");

    rejected = false;
    try {
        book.add_order(12, Side::Buy, 5000, 0);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected && !book.cancel_order(12), "zero quantity rejected");

    Book largeQuantity;
    largeQuantity.add_order(0, Side::Buy, 9950, std::numeric_limits<long>::max());
    rejected = false;
    try {
        largeQuantity.add_order(1, Side::Buy, 9950, 1);
    } catch (const std::overflow_error&) {
        rejected = true;
    }
    check(rejected && !largeQuantity.cancel_order(1), "overflow rejected before insertion");
    check(largeQuantity.get_bbo().bid_qty == std::numeric_limits<long>::max(), "quantity preserved after rejection");
    check(book.cancel_order(8) && book.get_bbo().bid_price == 5000, "sparse bid gap");
    check(book.cancel_order(10) && book.get_bbo().ask_price == 15000, "sparse ask gap");
    check(book.cancel_order(7) && book.cancel_order(9), "boundary book drained");
    book.add_order(13, Side::Buy, 9999, 2);
    book.add_order(14, Side::Sell, 10001, 3);
    check(book.get_bbo().bid_price == 9999 && book.get_bbo().ask_price == 10001, "refill empty book");
    rejected = false;
    try {
        book.add_order(15, static_cast<Side>(99), 9999, 1);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected && !book.cancel_order(15), "invalid side rejected");
    largeQuantity.add_order(2, Side::Sell, 10200, std::numeric_limits<long>::max());
    rejected = false;
    try {
        largeQuantity.add_order(3, Side::Sell, 10200, 1);
    } catch (const std::overflow_error&) {
        rejected = true;
    }
    check(rejected && !largeQuantity.cancel_order(3), "ask overflow rejected");
    check(largeQuantity.get_bbo().ask_qty == std::numeric_limits<long>::max(), "ask quantity preserved");
    Book mixedIds;
    mixedIds.add_order(100, Side::Buy, 9999, 2);
    mixedIds.add_order(5, Side::Buy, 9999, 3);
    mixedIds.add_order(50, Side::Sell, 10001, 4);
    mixedIds.add_order(std::numeric_limits<unsigned long long>::max(), Side::Sell, 15000, 1);
    check(mixedIds.get_bbo().bid_qty == 5, "out-of-order ID insertion");
    check(mixedIds.cancel_order(5) && !mixedIds.cancel_order(5), "out-of-order ID cancellation");
    check(mixedIds.cancel_order(std::numeric_limits<unsigned long long>::max()), "maximum ID cancellation");
    check(mixedIds.get_bbo().bid_qty == 2 && mixedIds.get_bbo().ask_qty == 4, "out-of-order quantities");
    std::cout << "Correctness checks: PASS\n";
}

struct Operation {
    int type;
    unsigned long long id;
    Side side;
    int price;
    long quantity;
};

std::vector<Operation> make_workload(int count) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> chooseType(0, 99);
    std::uniform_int_distribution<int> chooseSide(0, 1);
    std::uniform_int_distribution<int> chooseQuantity(1, 100);
    std::uniform_int_distribution<int> buyPrice(5000, 9999);
    std::uniform_int_distribution<int> sellPrice(10001, 15000);
    std::vector<unsigned long long> liveIds;
    std::vector<Operation> workload;
    unsigned long long nextId = 1;

    for (int i = 0; i < count; i++) {
        Operation op{};
        int choice = chooseType(rng);
        if (choice < 70 || liveIds.empty()) {
            op.type = 0;
            op.id = nextId;
            nextId++;
            op.quantity = chooseQuantity(rng);
            if (chooseSide(rng) == 0) {
                op.side = Side::Buy;
                op.price = buyPrice(rng);
            } else {
                op.side = Side::Sell;
                op.price = sellPrice(rng);
            }
            liveIds.push_back(op.id);
        } else if (choice < 90) {
            op.type = 1;
            std::uniform_int_distribution<std::size_t> chooseId(0, liveIds.size() - 1);
            std::size_t position = chooseId(rng);
            op.id = liveIds[position];
            liveIds[position] = liveIds.back();
            liveIds.pop_back();
        } else {
            op.type = 2;
        }
        workload.push_back(op);
    }
    return workload;
}

unsigned long long include_bbo(unsigned long long checksum, const BBO& bbo) {
    checksum = checksum * 31 + bbo.has_bid;
    checksum = checksum * 31 + bbo.bid_price;
    checksum = checksum * 31 + bbo.bid_qty;
    checksum = checksum * 31 + bbo.has_ask;
    checksum = checksum * 31 + bbo.ask_price;
    return checksum * 31 + bbo.ask_qty;
}

template<class Book>
void benchmark(int count, int repeats) {
    std::vector<Operation> workload = make_workload(count);
    long operationCounts[3] = {};
    for (const Operation& op : workload) {
        operationCounts[op.type]++;
    }
    std::cout << "Seed: 42, operations: " << count << '\n';
    std::cout << "Adds: " << operationCounts[0] << ", cancels: " << operationCounts[1]
              << ", BBO queries: " << operationCounts[2] << '\n';

    std::vector<double> totals;
    std::vector<double> lifecycleTimes;
    std::vector<double> addTimes, cancelTimes, bboTimes;
    unsigned long long firstChecksum = 0;

    for (int repetition = 0; repetition < repeats; repetition++) {
        auto lifecycleStart = std::chrono::steady_clock::now();
        {
        Book book;
        long long operationNs[3] = {};
        unsigned long long checksum = 0;
        auto totalStart = std::chrono::steady_clock::now();

        for (std::size_t i = 0; i < workload.size(); i++) {
            const Operation& op = workload[i];
            BBO result{};
            bool cancelled = true;
            auto start = std::chrono::steady_clock::now();
            if (op.type == 0) {
                book.add_order(op.id, op.side, op.price, op.quantity);
            } else if (op.type == 1) {
                cancelled = book.cancel_order(op.id);
            } else {
                result = book.get_bbo();
            }
            auto end = std::chrono::steady_clock::now();
            operationNs[op.type] += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            if (!cancelled) {
                throw std::runtime_error("Benchmark selected a nonlive ID");
            }
            if (op.type == 2) {
                checksum = include_bbo(checksum, result);
            }
            if ((i + 1) % 100 == 0) {
                for (const Level& level : book.get_bids(10)) {
                    checksum = checksum * 31 + level.price;
                    checksum = checksum * 31 + level.qty;
                }
                for (const Level& level : book.get_asks(10)) {
                    checksum = checksum * 31 + level.price;
                    checksum = checksum * 31 + level.qty;
                }
            }
        }
        auto totalEnd = std::chrono::steady_clock::now();
        checksum = include_bbo(checksum, book.get_bbo());
        if (repetition == 0) {
            firstChecksum = checksum;
        }
        check(checksum == firstChecksum, "repeat checksum differs");

        double totalMs = std::chrono::duration<double, std::milli>(totalEnd - totalStart).count();
        double addNs = operationCounts[0] ? static_cast<double>(operationNs[0]) / operationCounts[0] : 0;
        double cancelNs = operationCounts[1] ? static_cast<double>(operationNs[1]) / operationCounts[1] : 0;
        double bboNs = operationCounts[2] ? static_cast<double>(operationNs[2]) / operationCounts[2] : 0;
        totals.push_back(totalMs);
        addTimes.push_back(addNs);
        cancelTimes.push_back(cancelNs);
        bboTimes.push_back(bboNs);
        std::cout << "Run " << repetition + 1 << ": " << totalMs << " ms, add " << addNs
                  << " ns, cancel " << cancelNs << " ns, BBO " << bboNs
                  << " ns, checksum " << checksum << '\n';
        }
        auto lifecycleEnd = std::chrono::steady_clock::now();
        double lifecycleMs = std::chrono::duration<double, std::milli>(lifecycleEnd - lifecycleStart).count();
        lifecycleTimes.push_back(lifecycleMs);
        std::cout << "Lifecycle run " << repetition + 1 << ": " << lifecycleMs << " ms\n";
    }

    std::sort(lifecycleTimes.begin(), lifecycleTimes.end());
    std::cout << "Median replay including construction/destruction: "
              << lifecycleTimes[lifecycleTimes.size() / 2] << " ms\n";
    std::size_t middle = totals.size() / 2;
    std::sort(totals.begin(), totals.end());
    std::sort(addTimes.begin(), addTimes.end());
    std::sort(cancelTimes.begin(), cancelTimes.end());
    std::sort(bboTimes.begin(), bboTimes.end());
    std::cout << "Medians: " << totals[middle] << " ms, add " << addTimes[middle] << " ns, cancel "
              << cancelTimes[middle] << " ns, BBO " << bboTimes[middle] << " ns\n";
}

bool same_bbo(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.bid_price == b.bid_price && a.bid_qty == b.bid_qty
        && a.has_ask == b.has_ask && a.ask_price == b.ask_price && a.ask_qty == b.ask_qty;
}

bool same_levels(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); i++) {
        if (a[i].price != b[i].price || a[i].qty != b[i].qty) {
            return false;
        }
    }
    return true;
}

void compare_books(const OrderBook& a, const MapOrderBook& b, const RingOrderBook& c) {
    check(same_bbo(a.get_bbo(), b.get_bbo()), "A/B BBO mismatch");
    check(same_bbo(a.get_bbo(), c.get_bbo()), "A/C BBO mismatch");
    std::vector<Level> bids = a.get_bids(5000);
    std::vector<Level> asks = a.get_asks(5000);
    check(same_levels(bids, b.get_bids(5000)), "A/B bid depth mismatch");
    check(same_levels(bids, c.get_bids(5000)), "A/C bid depth mismatch");
    check(same_levels(asks, b.get_asks(5000)), "A/B ask depth mismatch");
    check(same_levels(asks, c.get_asks(5000)), "A/C ask depth mismatch");
}

#include "ring_checks.hpp"

void verify_all() {
    std::cout << "A: vector\n";
    verify_one<OrderBook>();
    std::cout << "B: maps\n";
    verify_one<MapOrderBook>();
    std::cout << "C: ring slot pool\n";
    verify_one<RingOrderBook>();
    verify_ring();

    OrderBook a;
    MapOrderBook b;
    RingOrderBook c;
    std::vector<Operation> workload = make_workload(5000);
    for (std::size_t i = 0; i < workload.size(); i++) {
        const Operation& op = workload[i];
        if (op.type == 0) {
            a.add_order(op.id, op.side, op.price, op.quantity);
            b.add_order(op.id, op.side, op.price, op.quantity);
            c.add_order(op.id, op.side, op.price, op.quantity);
        } else if (op.type == 1) {
            check(a.cancel_order(op.id), "A random live cancellation");
            check(b.cancel_order(op.id), "B random live cancellation");
            check(c.cancel_order(op.id), "C random live cancellation");
            check(!a.cancel_order(op.id) && !b.cancel_order(op.id)
                  && !c.cancel_order(op.id), "random repeated cancellation");
        }
        try {
            compare_books(a, b, c);
        } catch (const std::exception& error) {
            throw std::runtime_error("At random operation " + std::to_string(i + 1)
                                     + ": " + error.what());
        }
    }
    for (const Operation& op : workload) {
        if (op.type == 0) {
            bool cancelledA = a.cancel_order(op.id);
            bool cancelledB = b.cancel_order(op.id);
            bool cancelledC = c.cancel_order(op.id);
            check(cancelledA == cancelledB && cancelledA == cancelledC, "drain cancellation mismatch");
            compare_books(a, b, c);
        }
    }
    check(!a.get_bbo().has_bid && !a.get_bbo().has_ask, "random book fully drained");
    std::cout << "Random comparison: PASS (seed 42, 5000 operations + drain, full depth after each)\n";
}

int main(int argc, char** argv) {
    try {
        if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--verify")) {
            verify_all();
            return 0;
        }
        std::string implementation = "A";
        int count = 1000000;
        int repeats = 5;
        std::cout << std::unitbuf;
        for (int i = 1; i < argc; i++) {
            std::string option = argv[i];
            if (option == "--impl" && i + 1 < argc) {
                implementation = argv[++i];
            } else if (option == "--repeats" && i + 1 < argc) {
                std::string text = argv[++i];
                if (text != "1" && text != "5") {
                    throw std::invalid_argument("Repeats must be 1 or 5");
                }
                repeats = std::stoi(text);
            } else if (option == "--bench" && i + 1 < argc) {
                std::string text = argv[++i];
                if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
                    throw std::invalid_argument("Operation count must be a positive integer");
                }
                count = std::stoi(text);
                if (count <= 0 || count > 1000000) {
                    throw std::invalid_argument("Operation count must be between 1 and 1000000");
                }
            } else {
                throw std::invalid_argument("Usage: ./book --verify OR ./book --impl A|B|C --bench N [--repeats 1|5]");
            }
        }
        std::cout << "Implementation: " << implementation << '\n';
        if (implementation == "A") {
            benchmark<OrderBook>(count, repeats);
        } else if (implementation == "B") {
            benchmark<MapOrderBook>(count, repeats);
        } else if (implementation == "C") {
            benchmark<RingOrderBook>(count, repeats);
        } else {
            throw std::invalid_argument("Implementation must be A, B or C");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
