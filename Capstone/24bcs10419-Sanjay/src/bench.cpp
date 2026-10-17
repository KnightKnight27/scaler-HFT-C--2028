#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include <cmath>

#include "orderbook_a.h"
#include "orderbook_b.h"
#include "orderbook_c.h"

static constexpr int TOTAL_OPS = 1000000;
static constexpr int BENCHMARK_RUNS = 5;
static constexpr int DEPTH_INTERVAL = 100;
static constexpr int DEFAULT_DEPTH = 10;
static constexpr int SEED = 42;

struct WorkloadOp {
    enum Kind { Add, Cancel, GetBBO, GetDepth };
    Kind kind;
    uint64_t id;
    Side side;
    double price;
    long qty;
};

static std::vector<WorkloadOp> generate_workload() {
    std::mt19937 rng(SEED);

    std::vector<WorkloadOp> ops;
    ops.reserve(TOTAL_OPS + TOTAL_OPS / DEPTH_INTERVAL * 2);

    std::vector<uint64_t> live_ids;
    uint64_t next_id = 1;

    auto rand_side = [&]() -> Side {
        return (rng() % 2 == 0) ? Side::Buy : Side::Sell;
    };

    auto rand_qty = [&]() -> long {
        return static_cast<long>(rng() % 100 + 1);
    };

    auto rand_bid_price = [&]() -> double {
        int tick = static_cast<int>(MIN_BID_TICK + rng() % (MAX_BID_TICK - MIN_BID_TICK + 1));
        return tick_to_price(tick);
    };

    auto rand_ask_price = [&]() -> double {
        int tick = static_cast<int>(MIN_ASK_TICK + rng() % (MAX_ASK_TICK - MIN_ASK_TICK + 1));
        return tick_to_price(tick);
    };

    for (int i = 0; i < TOTAL_OPS; ++i) {
        int r = static_cast<int>(rng() % 100);

        if (r < 70) {
            WorkloadOp op{};
            op.kind = WorkloadOp::Add;
            op.id = next_id++;
            op.side = rand_side();
            op.price = (op.side == Side::Buy) ? rand_bid_price() : rand_ask_price();
            op.qty = rand_qty();
            ops.push_back(op);
            live_ids.push_back(op.id);
        } else if (r < 90) {
            if (live_ids.empty()) {
                WorkloadOp op{};
                op.kind = WorkloadOp::Add;
                op.id = next_id++;
                op.side = rand_side();
                op.price = (op.side == Side::Buy) ? rand_bid_price() : rand_ask_price();
                op.qty = rand_qty();
                ops.push_back(op);
                live_ids.push_back(op.id);
            } else {
                size_t idx = rng() % live_ids.size();
                WorkloadOp op{};
                op.kind = WorkloadOp::Cancel;
                op.id = live_ids[idx];
                ops.push_back(op);
                live_ids[idx] = live_ids.back();
                live_ids.pop_back();
            }
        } else {
            WorkloadOp op{};
            op.kind = WorkloadOp::GetBBO;
            ops.push_back(op);
        }

        if ((i + 1) % DEPTH_INTERVAL == 0) {
            WorkloadOp ob{};
            ob.kind = WorkloadOp::GetDepth;
            ops.push_back(ob);
        }
    }
    return ops;
}

struct TimingResult {
    double total_ms;
    double ns_per_add;
    double ns_per_cancel;
    double ns_per_bbo;
    long checksum;
};

template <typename Book>
TimingResult run_benchmark(const std::vector<WorkloadOp>& ops) {
    Book book;

    long add_count = 0;
    long cancel_count = 0;
    long bbo_count = 0;

    using Clock = std::chrono::steady_clock;
    using Ns = std::chrono::nanoseconds;

    Ns total_add{0}, total_cancel{0}, total_bbo{0};
    long checksum = 0;

    auto t_start = Clock::now();

    for (const auto& op : ops) {
        switch (op.kind) {
        case WorkloadOp::Add: {
            auto t0 = Clock::now();
            book.add_order(op.id, op.side, op.price, op.qty);
            auto t1 = Clock::now();
            total_add += std::chrono::duration_cast<Ns>(t1 - t0);
            ++add_count;
            break;
        }
        case WorkloadOp::Cancel: {
            auto t0 = Clock::now();
            bool ok = book.cancel_order(op.id);
            auto t1 = Clock::now();
            total_cancel += std::chrono::duration_cast<Ns>(t1 - t0);
            ++cancel_count;
            checksum += ok ? 1 : 0;
            break;
        }
        case WorkloadOp::GetBBO: {
            auto t0 = Clock::now();
            BBO bbo = book.get_bbo();
            auto t1 = Clock::now();
            total_bbo += std::chrono::duration_cast<Ns>(t1 - t0);
            ++bbo_count;
            if (bbo.has_bid) checksum += static_cast<long>(bbo.bid_price * 100);
            if (bbo.has_ask) checksum += static_cast<long>(bbo.ask_price * 100);
            break;
        }
        case WorkloadOp::GetDepth: {
            auto bids = book.get_bids(DEFAULT_DEPTH);
            auto asks = book.get_asks(DEFAULT_DEPTH);
            for (const auto& lv : bids) checksum += lv.qty;
            for (const auto& lv : asks) checksum += lv.qty;
            break;
        }
        }
    }

    auto t_end = Clock::now();

    TimingResult res{};
    res.total_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    res.ns_per_add = (add_count > 0) ? static_cast<double>(total_add.count()) / add_count : 0;
    res.ns_per_cancel = (cancel_count > 0) ? static_cast<double>(total_cancel.count()) / cancel_count : 0;
    res.ns_per_bbo = (bbo_count > 0) ? static_cast<double>(total_bbo.count()) / bbo_count : 0;
    res.checksum = checksum;
    return res;
}

static double median(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return (n % 2 == 1) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

template <typename Book>
void benchmark_impl(const std::string& name, const std::vector<WorkloadOp>& ops) {
    std::cout << "\n===== Benchmark: Implementation " << name << " =====\n";

    std::vector<double> totals, adds, cancels, bbos;
    long checksum = 0;

    for (int run = 0; run < BENCHMARK_RUNS; ++run) {
        auto res = run_benchmark<Book>(ops);
        totals.push_back(res.total_ms);
        adds.push_back(res.ns_per_add);
        cancels.push_back(res.ns_per_cancel);
        bbos.push_back(res.ns_per_bbo);
        checksum = res.checksum;
        std::cout << "  Run " << (run + 1) << ": " << std::fixed << std::setprecision(2)
                  << res.total_ms << " ms\n";
    }

    std::cout << "\n  Median total time : " << std::fixed << std::setprecision(2) << median(totals) << " ms\n";
    std::cout << "  Median ns/add     : " << std::fixed << std::setprecision(1) << median(adds) << " ns\n";
    std::cout << "  Median ns/cancel  : " << std::fixed << std::setprecision(1) << median(cancels) << " ns\n";
    std::cout << "  Median ns/BBO     : " << std::fixed << std::setprecision(1) << median(bbos) << " ns\n";

    Book book;
    for (const auto& op : ops) {
        if (op.kind == WorkloadOp::Add) {
            book.add_order(op.id, op.side, op.price, op.qty);
        } else if (op.kind == WorkloadOp::Cancel) {
            book.cancel_order(op.id);
        }
    }
    BBO bbo = book.get_bbo();
    std::cout << "\n  Final BBO:\n";
    if (bbo.has_bid) {
        std::cout << "    Bid: " << std::fixed << std::setprecision(2) << bbo.bid_price << " x " << bbo.bid_qty << "\n";
    } else {
        std::cout << "    Bid: (empty)\n";
    }
    if (bbo.has_ask) {
        std::cout << "    Ask: " << std::fixed << std::setprecision(2) << bbo.ask_price << " x " << bbo.ask_qty << "\n";
    } else {
        std::cout << "    Ask: (empty)\n";
    }
    std::cout << "  Checksum: " << checksum << "\n";
}

static bool levels_equal(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i].price - b[i].price) > 1e-9 || a[i].qty != b[i].qty) {
            return false;
        }
    }
    return true;
}

static bool bbo_equal(const BBO& a, const BBO& b) {
    if (a.has_bid != b.has_bid || a.has_ask != b.has_ask) return false;
    if (a.has_bid && (std::abs(a.bid_price - b.bid_price) > 1e-9 || a.bid_qty != b.bid_qty)) {
        return false;
    }
    if (a.has_ask && (std::abs(a.ask_price - b.ask_price) > 1e-9 || a.ask_qty != b.ask_qty)) {
        return false;
    }
    return true;
}

static void run_verify(const std::vector<WorkloadOp>& ops) {
    std::cout << "\n===== Correctness Verification =====\n";

    OrderBookA bookA;
    OrderBookB bookB;
    OrderBookC bookC;

    bool a_pass = true;
    bool b_pass = true;
    bool c_pass = true;
    int check_count = 0;

    for (size_t i = 0; i < ops.size(); ++i) {
        const auto& op = ops[i];
        switch (op.kind) {
        case WorkloadOp::Add:
            bookA.add_order(op.id, op.side, op.price, op.qty);
            bookB.add_order(op.id, op.side, op.price, op.qty);
            bookC.add_order(op.id, op.side, op.price, op.qty);
            break;
        case WorkloadOp::Cancel: {
            bool rA = bookA.cancel_order(op.id);
            bool rB = bookB.cancel_order(op.id);
            bool rC = bookC.cancel_order(op.id);
            if (rA != rB) b_pass = false;
            if (rA != rC) c_pass = false;
            break;
        }
        case WorkloadOp::GetBBO:
        case WorkloadOp::GetDepth: {
            BBO bboA = bookA.get_bbo();
            BBO bboB = bookB.get_bbo();
            BBO bboC = bookC.get_bbo();
            if (!bbo_equal(bboA, bboB)) b_pass = false;
            if (!bbo_equal(bboA, bboC)) c_pass = false;

            auto bidsA = bookA.get_bids(DEFAULT_DEPTH);
            auto bidsB = bookB.get_bids(DEFAULT_DEPTH);
            auto bidsC = bookC.get_bids(DEFAULT_DEPTH);
            if (!levels_equal(bidsA, bidsB)) b_pass = false;
            if (!levels_equal(bidsA, bidsC)) c_pass = false;

            auto asksA = bookA.get_asks(DEFAULT_DEPTH);
            auto asksB = bookB.get_asks(DEFAULT_DEPTH);
            auto asksC = bookC.get_asks(DEFAULT_DEPTH);
            if (!levels_equal(asksA, asksB)) b_pass = false;
            if (!levels_equal(asksA, asksC)) c_pass = false;

            ++check_count;
            break;
        }
        }
    }

    std::cout << "  Checked " << check_count << " consistency points.\n\n";
    std::cout << "  Implementation A: " << (a_pass ? "PASS" : "FAIL") << "\n";
    std::cout << "  Implementation B: " << (b_pass ? "PASS" : "FAIL") << "\n";
    std::cout << "  Implementation C: " << (c_pass ? "PASS" : "FAIL") << "\n\n";

    if (a_pass && b_pass && c_pass) {
        std::cout << "  All implementations produce identical results.\n";
    } else {
        std::cout << "  *** MISMATCH DETECTED ***\n";
    }
}

static void print_usage() {
    std::cerr << "Usage:\n"
              << "  ./bench --impl A|B|C     Run benchmark for one implementation\n"
              << "  ./bench --verify         Verify all three produce identical results\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string arg(argv[1]);

    std::cout << "Generating workload (" << TOTAL_OPS << " operations, seed=" << SEED << ")...\n";
    auto ops = generate_workload();
    std::cout << "Workload generated: " << ops.size() << " total ops (including depth queries).\n";

    if (arg == "--verify") {
        run_verify(ops);
        return 0;
    }

    if (arg == "--impl" && argc >= 3) {
        std::string impl(argv[2]);
        if (impl == "A" || impl == "a") {
            benchmark_impl<OrderBookA>("A", ops);
        } else if (impl == "B" || impl == "b") {
            benchmark_impl<OrderBookB>("B", ops);
        } else if (impl == "C" || impl == "c") {
            benchmark_impl<OrderBookC>("C", ops);
        } else {
            std::cerr << "Unknown implementation: " << impl << "\n";
            print_usage();
            return 1;
        }
        return 0;
    }

    print_usage();
    return 1;
}
