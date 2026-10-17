#include "book_a.hpp"
#include "book_b.hpp"
#include "book_c.hpp"
#include "workload.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

void verify(std::size_t count, std::uint64_t seed);
namespace {
using Clock = std::chrono::steady_clock;
using namespace ob;
struct Result {
    double total_ms = 0, add_ns = 0, cancel_ns = 0, bbo_ns = 0;
    std::size_t successful_cancels = 0;
    std::uint64_t checksum = 14695981039346656037ULL;
    BBO final;
};
template<class Book> Result replay(const Trace& trace, bool timed) {
    Book book; // construction, zero initialization, destruction outside timing
    Result r;
    const auto begin = Clock::now();
    for (std::size_t i = 0; i < trace.ops.size(); ++i) {
        const auto& op = trace.ops[i];
        Clock::time_point start;
        if (timed) start = Clock::now();
        if (op.kind == Kind::Add) {
            const auto& o = op.order;
            book.add_order(o.id, o.side, o.price, o.qty);
            if (timed) r.add_ns += std::chrono::duration<double, std::nano>(Clock::now()-start).count();
        } else if (op.kind == Kind::Cancel) {
            const bool ok = book.cancel_order(op.order.id);
            if (timed) r.cancel_ns += std::chrono::duration<double, std::nano>(Clock::now()-start).count();
            if (!ok) throw std::runtime_error("scheduled live cancellation failed at " + std::to_string(i));
            ++r.successful_cancels;
        } else {
            const auto b = book.get_bbo();
            if (timed) r.bbo_ns += std::chrono::duration<double, std::nano>(Clock::now()-start).count();
            consume(r.checksum, b); // after per-call timer, within total replay
        }
        if ((i+1) % 100 == 0) {
            consume(r.checksum, book.get_bids(10));
            consume(r.checksum, book.get_asks(10));
        }
    }
    const auto end = Clock::now();
    r.total_ms = std::chrono::duration<double, std::milli>(end-begin).count();
    r.final = book.get_bbo();
    consume(r.checksum, r.final);
    if (trace.adds) r.add_ns /= trace.adds;
    if (trace.cancels) r.cancel_ns /= trace.cancels;
    if (trace.queries) r.bbo_ns /= trace.queries;
    return r;
}
std::uint64_t number(const std::string& s) {
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("expected unsigned decimal integer: " + s);
    std::size_t used = 0;
    auto n = std::stoull(s, &used);
    if (used != s.size()) throw std::invalid_argument("invalid integer");
    return n;
}
}
int main(int argc, char** argv) {
    try {
        std::string impl = "A", mode = "instrumented";
        std::uint64_t seed = 42;
        std::size_t count = 1000000;
        bool check = false, warmup = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") {
                std::cout << "Usage: bench [--impl A|B|C] [--ops N] [--seed N] [--verify]\n"
                          << "             [--mode instrumented|bulk] [--warmup]\n"
                          << "Defaults: 1,000,000 primary ops, seed 42, A, one instrumented replay.\n"
                          << "Verify: edge tests + bounded full-depth suites + N-op reference replay.\n"
                          << "Warmup: one identical untabulated replay on a separate fresh book.\n"
                          << "Bulk: supplementary total-only replay without per-call clocks.\n";
                return 0;
            }
            if (arg == "--verify") { check = true; continue; }
            if (arg == "--warmup") { warmup = true; continue; }
            if (arg != "--impl" && arg != "--seed" && arg != "--ops" && arg != "--mode")
                throw std::invalid_argument("unknown argument: " + arg);
            if (++i == argc) throw std::invalid_argument("missing value for " + arg);
            const std::string value = argv[i];
            if (arg == "--impl") impl = value;
            if (arg == "--mode") mode = value;
            if (arg == "--seed") seed = number(value);
            if (arg == "--ops") {
                const auto n = number(value);
                if (n == 0 || n > 100000000) throw std::invalid_argument("ops must be 1..100000000");
                count = static_cast<std::size_t>(n);
            }
        }
        if (impl != "A" && impl != "B" && impl != "C") throw std::invalid_argument("impl must be A, B or C");
        if (mode != "instrumented" && mode != "bulk") throw std::invalid_argument("invalid mode");
        if (check) { verify(count, seed); return 0; }
        const Trace trace = generate(count, seed);
        const auto run = [&]() {
            if (impl == "A") return replay<BookA>(trace, mode == "instrumented");
            if (impl == "B") return replay<BookB>(trace, mode == "instrumented");
            return replay<BookC>(trace, mode == "instrumented");
        };
        if (warmup) {
            const auto w = run();
            std::cout << "Warmup checksum: " << w.checksum << '\n';
        }
        const auto r = run();
        std::cout << "Final BBO (cents): bid=" << r.final.has_bid << ':' << r.final.bid_price << 'x' << r.final.bid_qty
                  << " ask=" << r.final.has_ask << ':' << r.final.ask_price << 'x' << r.final.ask_qty << '\n';
        std::cout << "impl,mode,seed,ops,adds,cancels,bbo_queries,buy_adds,depth_calls,empty_fallbacks,trace_hash,successful_cancels,checksum,total_ms,ns_add,ns_cancel,ns_bbo\n";
        std::cout << impl << ',' << mode << ',' << seed << ',' << count << ',' << trace.adds << ',' << trace.cancels
                  << ',' << trace.queries << ',' << trace.buys << ',' << (count/100)*2 << ',' << trace.empty_fallbacks
                  << ',' << trace.hash << ',' << r.successful_cancels << ',' << r.checksum << ',' << std::fixed
                  << std::setprecision(3) << r.total_ms << ',' << r.add_ns << ',' << r.cancel_ns << ',' << r.bbo_ns << '\n';
    } catch (const std::exception& e) { std::cerr << "ERROR: " << e.what() << '\n'; return 1; }
}
