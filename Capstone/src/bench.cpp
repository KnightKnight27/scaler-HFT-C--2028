#include "Types.hpp"
#include "OrderBookA.hpp"
#include "OrderBookB.hpp"
#include "OrderBookC.hpp"
#include "Workload.hpp"

#include <iostream>
#include <iomanip>
#include <memory>
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include <cassert>

struct BenchmarkResult {
    double total_time_ms;
    double ns_per_add;
    double ns_per_cancel;
    double ns_per_bbo;
    BBO final_bbo;
    uint64_t checksum;
};

// Factory to create order book by implementation letter
std::unique_ptr<OrderBook> create_order_book(char impl) {
    switch (impl) {
        case 'A': return std::make_unique<OrderBookA>();
        case 'B': return std::make_unique<OrderBookB>();
        case 'C': return std::make_unique<OrderBookC>();
        default:
            std::cerr << "Unknown implementation: " << impl << "\n";
            return nullptr;
    }
}

// Run a single benchmark iteration of 1,000,000 operations
BenchmarkResult run_single_benchmark(char impl, const std::vector<Operation>& ops) {
    auto book = create_order_book(impl);
    assert(book != nullptr);

    uint64_t checksum = 0;
    size_t add_count = 0;
    size_t cancel_count = 0;
    size_t bbo_count = 0;

    // Phase 1: Pristine timing of the full 1,000,000 interleaved operations
    auto t_start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < ops.size(); ++i) {
        const auto& op = ops[i];
        switch (op.type) {
            case OpType::Add:
                book->add_order(op.id, op.side, op.price, op.qty);
                ++add_count;
                break;
            case OpType::Cancel: {
                bool res = book->cancel_order(op.id);
                checksum = checksum * 17ULL + (res ? 1ULL : 0ULL);
                ++cancel_count;
                break;
            }
            case OpType::GetBBO: {
                BBO bbo = book->get_bbo();
                if (bbo.has_bid) {
                    checksum ^= static_cast<uint64_t>(std::llround(bbo.bid_price * 100.0)) * 1000003ULL + static_cast<uint64_t>(bbo.bid_qty);
                }
                if (bbo.has_ask) {
                    checksum ^= static_cast<uint64_t>(std::llround(bbo.ask_price * 100.0)) * 999983ULL + static_cast<uint64_t>(bbo.ask_qty);
                }
                ++bbo_count;
                break;
            }
        }

        // Every 100 operations, call get_bids(10) and get_asks(10)
        if ((i + 1) % 100 == 0) {
            auto bids = book->get_bids(10);
            auto asks = book->get_asks(10);
            for (const auto& lvl : bids) {
                checksum = (checksum * 31ULL) + static_cast<uint64_t>(std::llround(lvl.price * 100.0)) + static_cast<uint64_t>(lvl.qty);
            }
            for (const auto& lvl : asks) {
                checksum = (checksum * 31ULL) + static_cast<uint64_t>(std::llround(lvl.price * 100.0)) + static_cast<uint64_t>(lvl.qty);
            }
        }
    }

    auto t_end = std::chrono::steady_clock::now();
    double total_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    BBO final_bbo = book->get_bbo();

    // Phase 2: Separate high-precision measurement for individual operation types
    // Measure ns/add using sample batch of adds
    auto book_micro = create_order_book(impl);
    constexpr size_t MICRO_ADD_COUNT = 100000;
    std::vector<uint64_t> micro_ids;
    micro_ids.reserve(MICRO_ADD_COUNT);

    auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < MICRO_ADD_COUNT; ++i) {
        uint64_t id = 200000000ULL + i;
        double price = 50.00 + ((i % 4900) * 0.01);
        book_micro->add_order(id, (i % 2 == 0) ? Side::Buy : Side::Sell, (i % 2 == 0) ? price : (100.01 + ((i % 4900) * 0.01)), 10);
        micro_ids.push_back(id);
    }
    auto t1 = std::chrono::steady_clock::now();
    double ns_per_add = std::chrono::duration<double, std::nano>(t1 - t0).count() / MICRO_ADD_COUNT;

    // Measure ns/bbo using sample batch of get_bbo
    constexpr size_t MICRO_BBO_COUNT = 100000;
    volatile long dummy_qty = 0;
    t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < MICRO_BBO_COUNT; ++i) {
        BBO b = book_micro->get_bbo();
        dummy_qty += b.bid_qty;
    }
    t1 = std::chrono::steady_clock::now();
    double ns_per_bbo = std::chrono::duration<double, std::nano>(t1 - t0).count() / MICRO_BBO_COUNT;

    // Measure ns/cancel using cancellations on active live orders
    constexpr size_t MICRO_CANCEL_COUNT = 50000;
    t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < MICRO_CANCEL_COUNT; ++i) {
        book_micro->cancel_order(micro_ids[i]);
    }
    t1 = std::chrono::steady_clock::now();
    double ns_per_cancel = std::chrono::duration<double, std::nano>(t1 - t0).count() / MICRO_CANCEL_COUNT;

    return {total_time_ms, ns_per_add, ns_per_cancel, ns_per_bbo, final_bbo, checksum};
}

// Correctness verification suite
bool run_verification(const std::vector<Operation>& ops) {
    std::cout << "Running correctness test...\n\n";

    // Step 1: Edge cases verification
    auto a = std::make_unique<OrderBookA>();
    auto b = std::make_unique<OrderBookB>();
    auto c = std::make_unique<OrderBookC>();

    // Test 1: Empty book behavior
    BBO bboA = a->get_bbo();
    BBO bboB = b->get_bbo();
    BBO bboC = c->get_bbo();
    if (bboA.has_bid || bboA.has_ask || bboB.has_bid || bboB.has_ask || bboC.has_bid || bboC.has_ask) {
        std::cerr << "FAIL: Empty book get_bbo() must indicate has_bid == false and has_ask == false\n";
        return false;
    }
    if (!a->get_bids(10).empty() || !b->get_bids(10).empty() || !c->get_bids(10).empty()) {
        std::cerr << "FAIL: Empty book get_bids() must be empty\n";
        return false;
    }
    if (!a->get_asks(10).empty() || !b->get_asks(10).empty() || !c->get_asks(10).empty()) {
        std::cerr << "FAIL: Empty book get_asks() must be empty\n";
        return false;
    }

    // Test 2: Cancel unknown order
    if (a->cancel_order(999999) || b->cancel_order(999999) || c->cancel_order(999999)) {
        std::cerr << "FAIL: Canceling non-existent order must return false\n";
        return false;
    }

    // Test 3: Insufficient depth
    a->add_order(1, Side::Buy, 99.00, 10);
    a->add_order(2, Side::Buy, 98.00, 20);
    a->add_order(3, Side::Buy, 97.00, 30);

    b->add_order(1, Side::Buy, 99.00, 10);
    b->add_order(2, Side::Buy, 98.00, 20);
    b->add_order(3, Side::Buy, 97.00, 30);

    c->add_order(1, Side::Buy, 99.00, 10);
    c->add_order(2, Side::Buy, 98.00, 20);
    c->add_order(3, Side::Buy, 97.00, 30);

    auto bidsA = a->get_bids(10);
    auto bidsB = b->get_bids(10);
    auto bidsC = c->get_bids(10);

    if (bidsA.size() != 3 || bidsB.size() != 3 || bidsC.size() != 3) {
        std::cerr << "FAIL: Insufficient depth query must return available 3 levels\n";
        return false;
    }
    if (!(bidsA == bidsB) || !(bidsB == bidsC)) {
        std::cerr << "FAIL: Insufficient depth levels do not match\n";
        return false;
    }

    // Test 4: Cancel already cancelled order
    if (!a->cancel_order(2) || !b->cancel_order(2) || !c->cancel_order(2)) {
        std::cerr << "FAIL: Cancel live order should return true\n";
        return false;
    }
    if (a->cancel_order(2) || b->cancel_order(2) || c->cancel_order(2)) {
        std::cerr << "FAIL: Cancel already cancelled order must return false\n";
        return false;
    }

    // Step 2: Full workload replay across all three implementations
    auto test_book_a = std::make_unique<OrderBookA>();
    auto test_book_b = std::make_unique<OrderBookB>();
    auto test_book_c = std::make_unique<OrderBookC>();

    for (size_t i = 0; i < ops.size(); ++i) {
        const auto& op = ops[i];
        switch (op.type) {
            case OpType::Add:
                test_book_a->add_order(op.id, op.side, op.price, op.qty);
                test_book_b->add_order(op.id, op.side, op.price, op.qty);
                test_book_c->add_order(op.id, op.side, op.price, op.qty);
                break;
            case OpType::Cancel: {
                bool resA = test_book_a->cancel_order(op.id);
                bool resB = test_book_b->cancel_order(op.id);
                bool resC = test_book_c->cancel_order(op.id);
                if (resA != resB || resB != resC) {
                    std::cerr << "FAIL at op " << i << " (Cancel ID " << op.id 
                              << "): resA=" << resA << ", resB=" << resB << ", resC=" << resC << "\n";
                    return false;
                }
                break;
            }
            case OpType::GetBBO: {
                BBO curr_bboA = test_book_a->get_bbo();
                BBO curr_bboB = test_book_b->get_bbo();
                BBO curr_bboC = test_book_c->get_bbo();
                if (!(curr_bboA == curr_bboB) || !(curr_bboB == curr_bboC)) {
                    std::cerr << "FAIL at op " << i << " (GetBBO mismatch):\n"
                              << "A: Bid(" << curr_bboA.has_bid << ", " << curr_bboA.bid_price << ", " << curr_bboA.bid_qty << ") "
                              << "Ask(" << curr_bboA.has_ask << ", " << curr_bboA.ask_price << ", " << curr_bboA.ask_qty << ")\n"
                              << "B: Bid(" << curr_bboB.has_bid << ", " << curr_bboB.bid_price << ", " << curr_bboB.bid_qty << ") "
                              << "Ask(" << curr_bboB.has_ask << ", " << curr_bboB.ask_price << ", " << curr_bboB.ask_qty << ")\n"
                              << "C: Bid(" << curr_bboC.has_bid << ", " << curr_bboC.bid_price << ", " << curr_bboC.bid_qty << ") "
                              << "Ask(" << curr_bboC.has_ask << ", " << curr_bboC.ask_price << ", " << curr_bboC.ask_qty << ")\n";
                    return false;
                }
                break;
            }
        }

        if ((i + 1) % 100 == 0) {
            auto bA = test_book_a->get_bids(10);
            auto bB = test_book_b->get_bids(10);
            auto bC = test_book_c->get_bids(10);
            if (!(bA == bB) || !(bB == bC)) {
                std::cerr << "FAIL at op " << i << ": get_bids(10) mismatch\n";
                return false;
            }

            auto aA = test_book_a->get_asks(10);
            auto aB = test_book_b->get_asks(10);
            auto aC = test_book_c->get_asks(10);
            if (!(aA == aB) || !(aB == aC)) {
                std::cerr << "FAIL at op " << i << ": get_asks(10) mismatch\n";
                return false;
            }
        }
    }

    std::cout << "Implementation A: PASS\n";
    std::cout << "Implementation B: PASS\n";
    std::cout << "Implementation C: PASS\n\n";
    std::cout << "All implementations produce identical results.\n";
    return true;
}

int main(int argc, char* argv[]) {
    std::string mode = "";
    char impl_choice = ' ';

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--verify") {
            mode = "verify";
        } else if (arg == "--impl" && i + 1 < argc) {
            mode = "bench";
            impl_choice = argv[++i][0];
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage:\n"
                      << "  " << argv[0] << " --verify           Run correctness test across all implementations\n"
                      << "  " << argv[0] << " --impl <A|B|C>     Run 5 iterations benchmark on specified implementation\n";
            return 0;
        }
    }

    if (mode.empty()) {
        std::cerr << "Error: Please specify either --verify or --impl <A|B|C>\n";
        std::cerr << "Use --help for usage information.\n";
        return 1;
    }

    std::cout << "Generating standard workload (1,000,000 operations, seed=42)..." << std::endl;
    auto ops = generate_workload(1000000, 42);
    std::cout << "Workload ready.\n" << std::endl;

    if (mode == "verify") {
        bool ok = run_verification(ops);
        return ok ? 0 : 1;
    }

    if (impl_choice != 'A' && impl_choice != 'B' && impl_choice != 'C') {
        std::cerr << "Error: Invalid implementation '" << impl_choice << "'. Must be A, B, or C.\n";
        return 1;
    }

    std::cout << "Benchmarking Implementation " << impl_choice << " (5 iterations)...\n";
    std::vector<BenchmarkResult> results;
    results.reserve(5);

    for (int run = 1; run <= 5; ++run) {
        std::cout << "  Run " << run << "/5 ... " << std::flush;
        auto res = run_single_benchmark(impl_choice, ops);
        std::cout << std::fixed << std::setprecision(2) << res.total_time_ms << " ms" << std::endl;
        results.push_back(res);
    }

    // Extract medians
    std::vector<double> total_times;
    std::vector<double> add_times;
    std::vector<double> cancel_times;
    std::vector<double> bbo_times;

    for (const auto& r : results) {
        total_times.push_back(r.total_time_ms);
        add_times.push_back(r.ns_per_add);
        cancel_times.push_back(r.ns_per_cancel);
        bbo_times.push_back(r.ns_per_bbo);
    }

    std::sort(total_times.begin(), total_times.end());
    std::sort(add_times.begin(), add_times.end());
    std::sort(cancel_times.begin(), cancel_times.end());
    std::sort(bbo_times.begin(), bbo_times.end());

    double median_total = total_times[2];
    double median_add = add_times[2];
    double median_cancel = cancel_times[2];
    double median_bbo = bbo_times[2];

    const auto& last_res = results.back();

    std::cout << "\n================ Benchmark Summary (Implementation " << impl_choice << ") ================\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total time (ms):           " << median_total << " ms (median of 5 runs)\n";
    std::cout << "Average ns / add_order:    " << median_add << " ns\n";
    std::cout << "Average ns / cancel_order: " << median_cancel << " ns\n";
    std::cout << "Average ns / get_bbo:      " << median_bbo << " ns\n";
    std::cout << "------------------------------------------------------------------------\n";
    std::cout << "Final BBO:\n";
    if (last_res.final_bbo.has_bid) {
        std::cout << "  Bid: " << last_res.final_bbo.bid_price << " x " << last_res.final_bbo.bid_qty << "\n";
    } else {
        std::cout << "  Bid: None\n";
    }
    if (last_res.final_bbo.has_ask) {
        std::cout << "  Ask: " << last_res.final_bbo.ask_price << " x " << last_res.final_bbo.ask_qty << "\n";
    } else {
        std::cout << "  Ask: None\n";
    }
    std::cout << "\nChecksum: " << last_res.checksum << "\n";
    std::cout << "========================================================================\n";

    return 0;
}
