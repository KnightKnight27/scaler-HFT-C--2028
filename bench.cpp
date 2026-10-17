#include "implementation/array_order_book.hpp"
#include "implementation/map_order_book.hpp"
#include "implementation/vector_order_book.hpp"
#include "verification.hpp"
#include "workload.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
struct Result {
    double total_ms = 0;
    std::array<double, 3> ns{};
    std::uint64_t checksum = 0;
    BBO final_bbo{};
};

void consume(std::uint64_t &sum, std::uint64_t value) {
    sum ^= value + 0x9e3779b97f4a7c15ULL + (sum << 6) + (sum >> 2);
}
void consume(std::uint64_t &sum, const BBO &bbo) {
    consume(sum, bbo.has_bid);
    consume(sum, static_cast<std::uint64_t>(bbo.bid_price_ticks));
    consume(sum, static_cast<std::uint64_t>(bbo.bid_qty));
    consume(sum, bbo.has_ask);
    consume(sum, static_cast<std::uint64_t>(bbo.ask_price_ticks));
    consume(sum, static_cast<std::uint64_t>(bbo.ask_qty));
}
void consume(std::uint64_t &sum, const std::vector<Level> &levels) {
    consume(sum, levels.size());
    for (const auto &level : levels) {
        consume(sum, static_cast<std::uint64_t>(level.price_ticks));
        consume(sum, static_cast<std::uint64_t>(level.qty));
    }
}

template <class Book, bool Instrumented> Result replay(const Workload &workload) {
    Book book;
    Result out;
    std::array<std::int64_t, 3> elapsed{};
    const auto start = Clock::now();
    for (std::size_t i = 0; i < workload.operations.size(); ++i) {
        const auto &op = workload.operations[i];
        Clock::time_point before;
        if (op.kind == Kind::Add) {
            if constexpr (Instrumented) {
                before = Clock::now();
            }
            book.add_order(op.id, op.side, op.price_ticks, op.qty);
            if constexpr (Instrumented) {
                elapsed[0] +=
                    std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - before)
                        .count();
            }
        } else if (op.kind == Kind::Cancel) {
            if constexpr (Instrumented) {
                before = Clock::now();
            }
            const bool cancelled = book.cancel_order(op.id);
            if constexpr (Instrumented) {
                elapsed[1] +=
                    std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - before)
                        .count();
            }
            if (!cancelled) {
                throw std::runtime_error("benchmark cancelled a non-live order");
            }
            consume(out.checksum, cancelled);
        } else {
            if constexpr (Instrumented) {
                before = Clock::now();
            }
            const auto bbo = book.get_bbo();
            if constexpr (Instrumented) {
                elapsed[2] +=
                    std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - before)
                        .count();
            }
            consume(out.checksum, bbo);
        }
        if ((i + 1) % 100 == 0) {
            consume(out.checksum, book.get_bids(10));
            consume(out.checksum, book.get_asks(10));
        }
    }
    out.total_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    out.final_bbo = book.get_bbo();
    consume(out.checksum, out.final_bbo);
    if constexpr (Instrumented) {
        const std::array<std::size_t, 3> counts{workload.adds, workload.cancels, workload.queries};
        for (std::size_t i = 0; i < counts.size(); ++i) {
            out.ns[i] = counts[i] == 0
                            ? 0
                            : static_cast<double>(elapsed[i]) / static_cast<double>(counts[i]);
        }
    }
    return out;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto mid = values.size() / 2;
    return values.size() % 2 ? values[mid] : (values[mid - 1] + values[mid]) / 2;
}

template <class Book>
void benchmark(const Workload &workload, char impl, int runs, bool profile,
               const std::string &csv_path) {
    std::ofstream csv;
    if (!csv_path.empty()) {
        csv.open(csv_path);
        if (!csv) {
            throw std::runtime_error("cannot write " + csv_path);
        }
        csv << "implementation,run,total_ms,instrumented_ms,ns_add,ns_cancel,ns_bbo,checksum,"
               "workload_hash\n";
        csv << std::setprecision(12);
    }
    std::vector<double> totals, adds, cancels, queries;
    Result final;
    for (int run = 1; run <= runs; ++run) {
        // Separate fresh-book replays: throughput has no per-operation clocks.
        const auto plain = replay<Book, false>(workload);
        const auto timed = profile ? Result{} : replay<Book, true>(workload);
        if (!profile && plain.checksum != timed.checksum) {
            throw std::runtime_error("instrumented replay checksum mismatch");
        }
        if (run > 1 && final.checksum != plain.checksum) {
            throw std::runtime_error("repeat checksum mismatch");
        }
        totals.push_back(plain.total_ms);
        adds.push_back(timed.ns[0]);
        cancels.push_back(timed.ns[1]);
        queries.push_back(timed.ns[2]);
        final = plain;
        std::cout << "Run " << run << ": total_ms=" << plain.total_ms;
        if (!profile) {
            std::cout << " ns/add=" << timed.ns[0] << " ns/cancel=" << timed.ns[1]
                      << " ns/BBO=" << timed.ns[2];
        }
        std::cout << " checksum=" << plain.checksum << '\n';
        if (csv) {
            csv << impl << ',' << run << ',' << plain.total_ms << ',' << timed.total_ms << ','
                << timed.ns[0] << ',' << timed.ns[1] << ',' << timed.ns[2] << ',' << plain.checksum
                << ',' << workload.fingerprint << '\n';
        }
    }
    std::cout << "Median total_ms=" << median(totals);
    if (!profile) {
        std::cout << " ns/add=" << median(adds) << " ns/cancel=" << median(cancels)
                  << " ns/BBO=" << median(queries);
    }
    std::cout << "\nFinal BBO (integer ticks): bid=";
    if (final.final_bbo.has_bid) {
        std::cout << final.final_bbo.bid_price_ticks << " x " << final.final_bbo.bid_qty;
    } else {
        std::cout << "empty";
    }
    std::cout << " ask=";
    if (final.final_bbo.has_ask) {
        std::cout << final.final_bbo.ask_price_ticks << " x " << final.final_bbo.ask_qty;
    } else {
        std::cout << "empty";
    }
    std::cout << "\nChecksum: " << final.checksum << '\n';
    if (csv && !csv.good()) {
        throw std::runtime_error("CSV write failed");
    }
}

std::uint64_t number(const std::string &text, const std::string &option) {
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::invalid_argument("invalid number for " + option);
    }
    return value;
}
} // namespace

int main(int argc, char **argv) {
    try {
        bool verification = false, profile = false, specified_ops = false;
        char impl = 'A';
        std::size_t ops = 1000000;
        std::uint32_t seed = 42;
        int runs = 5;
        std::string csv;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") {
                std::cout
                    << "Usage: ./bench [--impl A|B|C] [--runs 5] [--ops 1000000] [--seed 42]\n"
                       "               [--csv FILE] [--profile]\n"
                       "       ./bench --verify [--ops 20000] [--seed 42]\n"
                       "--profile replays without per-operation clocks; use --runs 1 with perf.\n";
                return 0;
            }
            if (arg == "--verify") {
                verification = true;
                continue;
            }
            if (arg == "--profile") {
                profile = true;
                continue;
            }
            if (arg != "--impl" && arg != "--runs" && arg != "--ops" && arg != "--seed" &&
                arg != "--csv") {
                throw std::invalid_argument("unknown option: " + arg);
            }
            if (i + 1 == argc) {
                throw std::invalid_argument("missing value for " + arg);
            }
            const std::string value = argv[++i];
            if (arg == "--impl") {
                if (value.size() != 1 || value[0] < 'A' || value[0] > 'C') {
                    throw std::invalid_argument("implementation must be A, B or C");
                }
                impl = value[0];
            } else if (arg == "--csv") {
                csv = value;
            } else {
                const auto n = number(value, arg);
                if (arg == "--runs") {
                    if (n < 1 || n > 100) {
                        throw std::invalid_argument("runs must be 1..100");
                    }
                    runs = static_cast<int>(n);
                } else if (arg == "--ops") {
                    if (n < 1 || n > 10000000) {
                        throw std::invalid_argument("ops must be 1..10000000");
                    }
                    ops = static_cast<std::size_t>(n);
                    specified_ops = true;
                } else {
                    if (n > std::numeric_limits<std::uint32_t>::max()) {
                        throw std::invalid_argument("seed exceeds uint32");
                    }
                    seed = static_cast<std::uint32_t>(n);
                }
            }
        }
        if (verification && profile) {
            throw std::invalid_argument("--verify and --profile are mutually exclusive");
        }
        if (verification && !specified_ops) {
            ops = 20000;
        }
        const auto workload = generate_workload(ops, seed);
        std::cout << std::fixed << std::setprecision(3) << "Seed=" << seed << " operations=" << ops
                  << " adds=" << workload.adds << " cancels=" << workload.cancels
                  << " BBO_queries=" << workload.queries << " depth_pairs=" << ops / 100
                  << "\nWorkload hash=" << workload.fingerprint << '\n';
        if (verification) {
            verify(workload);
        } else if (impl == 'A') {
            benchmark<MapOrderBook>(workload, impl, runs, profile, csv);
        } else if (impl == 'B') {
            benchmark<VectorOrderBook>(workload, impl, runs, profile, csv);
        } else {
            benchmark<ArrayOrderBook>(workload, impl, runs, profile, csv);
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
