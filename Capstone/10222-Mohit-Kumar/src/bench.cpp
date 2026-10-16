#include "orderbook/interleaved_book.hpp"
#include "orderbook/tree_book.hpp"
#include "orderbook/vector_book.hpp"
#include "orderbook/workload.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

using Clock = std::chrono::steady_clock;
// A monotonic interval clock. C++ does not prescribe its OS API or CPU counter.
static_assert(Clock::is_steady, "benchmark requires a steady clock");
std::string display_price(ob::Tick price) {
    const auto whole = price / 100, cents = price % 100;
    return std::to_string(whole) + '.' + (cents < 10 ? "0" : "") + std::to_string(cents);
}
struct Sample {
    double total_ms = 0, add_ns = 0, cancel_ns = 0, bbo_ns = 0;
    std::uint64_t checksum = 0;
    ob::BBO final;
};
template<class Book> Sample replay(const ob::Workload& workload, bool instrument) {
    Book book;
    std::array<std::int64_t, 3> elapsed{};
    std::uint64_t checksum = 0;
    const auto start = Clock::now();
    for (std::size_t i = 0; i < workload.events.size(); ++i) {
        const auto& event = workload.events[i];
        if (instrument) {
            Clock::time_point before, after;
            std::uint64_t observed = 0;
            switch (event.kind) {
                case ob::Kind::Add:
                    before = Clock::now();
                    book.add_order(event.id, event.side, event.price, event.qty);
                    after = Clock::now();
                    break;
                case ob::Kind::Cancel: {
                    before = Clock::now();
                    const bool cancelled = book.cancel_order(event.id);
                    after = Clock::now();
                    if (!cancelled) throw std::logic_error("non-live cancellation");
                    observed = 1;
                    break;
                }
                case ob::Kind::Query: {
                    before = Clock::now();
                    // Observable result materialization prevents checksum arithmetic being
                    // scheduled into the interval by the optimizing compiler.
                    const volatile auto bbo = book.get_bbo();
                    after = Clock::now();
                    observed = ob::consume(ob::BBO{bbo.has_bid, bbo.bid_price, bbo.bid_qty,
                                                  bbo.has_ask, bbo.ask_price, bbo.ask_qty});
                    break;
                }
            }
            elapsed[static_cast<std::size_t>(event.kind)] +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(after - before).count();
            checksum += observed;
        } else checksum += ob::apply(book, event);
        if ((i + 1) % 100 == 0) {
            checksum += ob::consume(book.get_bids(10));
            checksum += ob::consume(book.get_asks(10));
        }
    }
    const auto finish = Clock::now();
    Sample s;
    s.total_ms = std::chrono::duration<double, std::milli>(finish - start).count();
    s.add_ns = workload.adds ? static_cast<double>(elapsed[0]) / workload.adds : 0;
    s.cancel_ns = workload.cancels ? static_cast<double>(elapsed[1]) / workload.cancels : 0;
    s.bbo_ns = workload.queries ? static_cast<double>(elapsed[2]) / workload.queries : 0;
    s.final = book.get_bbo(); s.checksum = checksum + ob::consume(s.final);
    return s; // Book destruction occurs after the finish timestamp.
}
double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}
template<class Book> void benchmark(const ob::Workload& w, char impl, int runs, bool total_only) {
    std::vector<double> totals, adds, cancels, bbos;
    std::uint64_t checksum = 0;
    bool have_checksum = false;
    std::cout << "run,total_ms,add_ns,cancel_ns,bbo_ns,checksum\n" << std::fixed << std::setprecision(3);
    for (int run = 1; run <= runs; ++run) {
        const auto total = replay<Book>(w, false);
        const auto measured = total_only ? Sample{} : replay<Book>(w, true);
        if ((!total_only && measured.checksum != total.checksum) ||
            (have_checksum && total.checksum != checksum))
            throw std::logic_error("replay checksums differ");
        checksum = total.checksum; have_checksum = true;
        totals.push_back(total.total_ms); adds.push_back(measured.add_ns);
        cancels.push_back(measured.cancel_ns); bbos.push_back(measured.bbo_ns);
        std::cout << run << ',' << total.total_ms << ',' << measured.add_ns << ','
                  << measured.cancel_ns << ',' << measured.bbo_ns << ',' << checksum << '\n';
        if (run == runs) std::cout << "final_bbo=" << display_price(total.final.bid_price) << 'x' << total.final.bid_qty
            << '/' << display_price(total.final.ask_price) << 'x' << total.final.ask_qty
            << " has_bid=" << total.final.has_bid << " has_ask=" << total.final.has_ask << '\n';
    }
    std::cout << "median impl=" << impl << " total_ms=" << median(totals);
    if (!total_only) std::cout << " add_ns=" << median(adds) << " cancel_ns=" << median(cancels)
                              << " bbo_ns=" << median(bbos);
    std::cout << " checksum=" << checksum << '\n';
}
std::uint64_t number(const std::string& arg) {
    if (arg.empty() || arg[0] == '-') throw std::invalid_argument("expected nonnegative integer");
    std::size_t parsed = 0;
    const auto result = std::stoull(arg, &parsed);
    if (parsed != arg.size()) throw std::invalid_argument("invalid numeric argument");
    return result;
}
int main(int argc, char** argv) {
    try {
        char impl = 'A';
        std::size_t count = 1000000;
        std::uint32_t seed = 42;
        int runs = 5;
        bool total_only = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") {
                std::cout << "bench --impl A|B|C [--runs 5] [--ops 1000000] [--seed 42] [--mode both|total]\n";
                return 0;
            }
            if (i + 1 == argc) throw std::invalid_argument("missing option value");
            const std::string value = argv[++i];
            if (arg == "--impl" && value.size() == 1 && value[0] >= 'A' && value[0] <= 'C') impl = value[0];
            else if (arg == "--runs") {
                const auto n = number(value);
                if (n == 0 || n > 99 || n % 2 == 0) throw std::invalid_argument("runs must be odd, 1..99");
                runs = static_cast<int>(n);
            } else if (arg == "--ops") {
                const auto n = number(value);
                if (n == 0 || n > 100000000) throw std::invalid_argument("ops must be 1..100000000");
                count = static_cast<std::size_t>(n);
            } else if (arg == "--seed") {
                const auto n = number(value);
                if (n > std::numeric_limits<std::uint32_t>::max()) throw std::invalid_argument("seed too large");
                seed = static_cast<std::uint32_t>(n);
            } else if (arg == "--mode" && (value == "both" || value == "total")) total_only = value == "total";
            else throw std::invalid_argument("unknown option or invalid value: " + arg);
        }
        // Generation, allocation of the event stream and CSV output stay outside replay timing.
        const auto w = ob::generate(count, seed);
        std::cout << "workload seed=" << seed << " operations=" << count << " adds=" << w.adds
                  << " cancels=" << w.cancels << " queries=" << w.queries
                  << " depth_pairs=" << count / 100 << " peak_live=" << w.peak_live
                  << " hash=" << w.hash << '\n';
        // Empty timing gives scale for instrumentation overhead; do not subtract it.
        std::int64_t clock_ns = 0;
        for (int i = 0; i < 10000; ++i) {
            const auto a = Clock::now(); const auto b = Clock::now();
            clock_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
        }
        std::cout << "empty_clock_interval_ns=" << static_cast<double>(clock_ns) / 10000
                  << " mode=" << (total_only ? "total" : "both") << " runs=" << runs << '\n';
        switch (impl) {
            case 'A': benchmark<ob::TreeBook>(w, impl, runs, total_only); break;
            case 'B': benchmark<ob::VectorBook>(w, impl, runs, total_only); break;
            case 'C': benchmark<ob::InterleavedBook>(w, impl, runs, total_only); break;
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << "error: " << e.what() << '\n'; return 1; }
}
