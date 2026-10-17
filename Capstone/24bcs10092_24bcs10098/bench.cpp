#include "impl_a/orderbook_a.hpp"
#include "impl_b/orderbook_b.hpp"
#include "impl_c/orderbook_c.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#ifdef __linux__
#include <sys/resource.h>
#include <sys/utsname.h>
#endif

namespace {
using namespace orderbook;
using Clock = std::chrono::steady_clock;
enum class Kind { add, cancel, bbo };
struct Op { Kind kind; Order order; };
struct Counts { std::uint64_t adds, cancels, bbos, depth; };
struct Config { bool profile_only = false; char impl = 0; std::uint64_t ops = 1000000, runs = 5, seed = 42; };
constexpr std::uint64_t hash_start = 14695981039346656037ULL;
void mix(std::uint64_t& hash, std::uint64_t value) { hash = (hash ^ value) * 1099511628211ULL; }
void consume(std::uint64_t& hash, const Bbo& bbo) {
    for (const auto& level : {bbo.bid, bbo.ask}) {
        mix(hash, level.has_value());
        if (level) { mix(hash, static_cast<std::uint64_t>(level->price)); mix(hash, level->total_quantity); }
    }
}
void consume(std::uint64_t& hash, const DepthSnapshot& levels) {
    mix(hash, levels.size());
    for (const auto& level : levels) { mix(hash, static_cast<std::uint64_t>(level.price)); mix(hash, level.total_quantity); }
}
void usage(std::ostream& out) {
    out << "bench --impl A|B|C [--ops N] [--runs N] [--seed N] [--profile-only]\n"
           "Defaults: ops=1000000 runs=5 seed=42. Limits: ops=10..2000000, runs=1..25.\n"
           "Counts: adds=floor(7*N/10), cancels=floor(2*N/10), BBO=remainder.\n"
           "Both depth(10) calls occur after every 100 primary operations.\n"
           "Standard mode validates A/B/C untimed before selected implementation runs.\n"
           "Profile-only: selected implementation only; requires ops=1000000 seed=42 runs=1\n"
           "(runs defaults to 1 in this mode); checks the fixed benchmark fixture.\n";
}
std::uint64_t number(std::string_view text) {
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::invalid_argument("invalid unsigned integer: " + std::string(text));
    return value;
}
Config parse(int argc, char** argv) {
    Config config;
    std::unordered_set<std::string> seen;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--profile-only") {
            if (!seen.insert(arg).second) throw std::invalid_argument("duplicate option: " + arg);
            config.profile_only = true; continue;
        }
        if (arg != "--impl" && arg != "--ops" && arg != "--runs" && arg != "--seed")
            throw std::invalid_argument("unknown option: " + arg);
        if (!seen.insert(arg).second) throw std::invalid_argument("duplicate option: " + arg);
        if (++i == argc) throw std::invalid_argument("missing value: " + arg);
        const std::string_view value = argv[i];
        if (arg == "--impl") {
            if (value.size() != 1 || (value[0] != 'A' && value[0] != 'B' && value[0] != 'C'))
                throw std::invalid_argument("--impl must be A, B or C");
            config.impl = value[0];
        } else if (arg == "--ops") config.ops = number(value);
        else if (arg == "--runs") config.runs = number(value);
        else config.seed = number(value);
    }
    if (!config.impl) throw std::invalid_argument("--impl is required");
    if (config.ops < 10 || config.ops > 2000000) throw std::invalid_argument("--ops must be 10..2000000");
    if (config.runs < 1 || config.runs > 25) throw std::invalid_argument("--runs must be 1..25");
    if (config.profile_only) {
        if (seen.count("--runs") == 0) config.runs = 1;
        if (config.ops != 1000000 || config.seed != 42 || config.runs != 1)
            throw std::invalid_argument("profile-only supports only the required ops=1000000 seed=42 runs=1 fixture");
    }
    return config;
}
Counts counts(std::uint64_t n) { return {n * 7 / 10, n * 2 / 10, n - n * 7 / 10 - n * 2 / 10, n / 100}; }

std::vector<Op> generate(const Config& config) {
    const auto total = counts(config.ops);
    auto remaining = total;
    std::mt19937_64 rng(config.seed);
    auto draw = [&](std::uint64_t bound) {
        const auto threshold = (std::uint64_t{0} - bound) % bound;
        std::uint64_t value;
        do { value = rng(); } while (value < threshold);
        return value % bound;
    };
    std::vector<Op> trace;
    trace.reserve(static_cast<std::size_t>(config.ops));
    std::vector<OrderId> live;
    live.reserve(static_cast<std::size_t>(total.adds));
    OrderId id = 0;
    while (trace.size() < config.ops) {
        const auto choice = draw(remaining.adds + remaining.cancels + remaining.bbos);
        Kind kind = choice < remaining.adds ? Kind::add :
                    choice < remaining.adds + remaining.cancels ? Kind::cancel : Kind::bbo;
        // Reorder an impossible cancellation into a remaining add; exact quotas
        // stay unchanged. More adds than cancels guarantees an add is available.
        if (kind == Kind::cancel && live.empty()) {
            if (remaining.adds == 0) throw std::logic_error("no live cancel or remaining add");
            kind = Kind::add;
        }
        if (kind == Kind::add) {
            const auto side = draw(2) == 0 ? Side::bid : Side::ask;
            const auto price = (side == Side::bid ? 5000 : 10001) + static_cast<PriceTicks>(draw(5000));
            const auto qty = static_cast<Quantity>(draw(100) + 1);
            trace.push_back({kind, {id, side, price, qty}});
            live.push_back(id++); --remaining.adds;
        } else if (kind == Kind::cancel) {
            const auto pos = static_cast<std::size_t>(draw(live.size()));
            trace.push_back({kind, {live[pos], Side::bid, 0, 0}});
            live[pos] = live.back(); live.pop_back(); --remaining.cancels;
        } else {
            trace.push_back({kind, {0, Side::bid, 0, 0}}); --remaining.bbos;
        }
    }
    return trace;
}

std::uint64_t validate_trace(const std::vector<Op>& trace, const Counts& expected) {
    std::unordered_set<OrderId> live;
    OrderId next_id = 0;
    Counts actual{};
    std::uint64_t hash = hash_start;
    std::uint64_t bids = 0, asks = 0;
    for (const auto& op : trace) {
        mix(hash, static_cast<std::uint64_t>(op.kind));
        mix(hash, op.order.id);
        if (op.kind == Kind::add) {
            const auto& order = op.order;
            const bool bid = order.side == Side::bid;
            if ((order.side != Side::bid && order.side != Side::ask) || order.id != next_id++ ||
                order.price < (bid ? 5000 : 10001) || order.price > (bid ? 9999 : 15000) ||
                order.quantity < 1 || order.quantity > 100 || !live.insert(order.id).second)
                throw std::logic_error("invalid generated add");
            mix(hash, static_cast<std::uint64_t>(order.side));
            mix(hash, order.price); mix(hash, order.quantity);
            ++actual.adds; if (bid) ++bids; else ++asks;
        } else if (op.kind == Kind::cancel) {
            if (live.erase(op.order.id) != 1) throw std::logic_error("generated cancel is not live");
            ++actual.cancels;
        } else ++actual.bbos;
    }
    if (actual.adds != expected.adds || actual.cancels != expected.cancels || actual.bbos != expected.bbos)
        throw std::logic_error("wrong trace quotas");
    std::cout << "trace adds=" << actual.adds << " cancels=" << actual.cancels
              << " bbo=" << actual.bbos << " bid_adds=" << bids << " ask_adds=" << asks
              << " depth_calls_per_side=" << expected.depth << " final_live=" << live.size()
              << " fingerprint=" << hash << '\n';
    return hash;
}

struct Resources {
    double peak_kib = 0, minor = 0, major = 0;
};
Resources resources() {
#ifdef __linux__
    rusage value{};
    if (getrusage(RUSAGE_SELF, &value) != 0) throw std::runtime_error("getrusage failed");
    return {static_cast<double>(value.ru_maxrss), static_cast<double>(value.ru_minflt),
            static_cast<double>(value.ru_majflt)};
#else
    return {}; // Availability is explicitly printed below.
#endif
}
struct Result {
    double ms = 0;
    std::array<double, 5> ns{}; // add, cancel, BBO, bid depth, ask depth totals
    std::uint64_t checksum = hash_start;
    Bbo final_bbo;
    DepthSnapshot final_bids, final_asks;
    Resources os;
};
double nanoseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::nano>(Clock::now() - start).count();
}
std::unique_ptr<OrderBook> make_book(char impl) {
    if (impl == 'A') return std::make_unique<OrderBookA>();
    if (impl == 'B') return std::make_unique<OrderBookB>();
    return std::make_unique<OrderBookC>();
}
Result replay(OrderBook& book, const std::vector<Op>& trace, bool timed) {
    Result result;
    // Result recording space is allocated before the replay timer; validate it
    // afterwards instead of doing correctness comparisons inside timing.
    std::vector<unsigned char> codes(trace.size());
    const auto before = resources();
    const auto overall = Clock::now();
    for (std::size_t i = 0; i < trace.size(); ++i) {
        const auto& op = trace[i];
        mix(result.checksum, static_cast<std::uint64_t>(op.kind));
        if (op.kind == Kind::add) {
            const auto start = timed ? Clock::now() : Clock::time_point{};
            const auto value = book.add_order(op.order);
            if (timed) result.ns[0] += nanoseconds(start);
            codes[i] = static_cast<unsigned char>(value);
            mix(result.checksum, codes[i]);
        } else if (op.kind == Kind::cancel) {
            const auto start = timed ? Clock::now() : Clock::time_point{};
            const auto value = book.cancel_order(op.order.id);
            if (timed) result.ns[1] += nanoseconds(start);
            codes[i] = static_cast<unsigned char>(value);
            mix(result.checksum, codes[i]);
        } else {
            const auto start = timed ? Clock::now() : Clock::time_point{};
            const auto value = book.get_bbo();
            if (timed) result.ns[2] += nanoseconds(start);
            consume(result.checksum, value);
        }
        if ((i + 1) % 100 == 0) {
            const auto bid_start = timed ? Clock::now() : Clock::time_point{};
            const auto bids = book.get_bids(10);
            if (timed) result.ns[3] += nanoseconds(bid_start);
            const auto ask_start = timed ? Clock::now() : Clock::time_point{};
            const auto asks = book.get_asks(10);
            if (timed) result.ns[4] += nanoseconds(ask_start);
            consume(result.checksum, bids); consume(result.checksum, asks);
        }
    }
    result.ms = std::chrono::duration<double, std::milli>(Clock::now() - overall).count();
    const auto after = resources();
    result.os = {after.peak_kib, after.minor - before.minor, after.major - before.major};
    for (std::size_t i = 0; i < trace.size(); ++i) {
        if (trace[i].kind != Kind::bbo && codes[i] != 0)
            throw std::runtime_error("replay rejected operation index=" + std::to_string(i + 1) +
                " kind=" + (trace[i].kind == Kind::add ? "add" : "cancel") +
                " id=" + std::to_string(trace[i].order.id) + " result_enum=" + std::to_string(codes[i]));
    }
    result.final_bbo = book.get_bbo();
    result.final_bids = book.get_bids(5000); result.final_asks = book.get_asks(5000);
    consume(result.checksum, result.final_bbo);
    consume(result.checksum, result.final_bids); consume(result.checksum, result.final_asks);
    return result;
}
bool same_levels(const DepthSnapshot& a, const DepthSnapshot& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].price != b[i].price || a[i].total_quantity != b[i].total_quantity) return false;
    return true;
}
bool same_quote(const std::optional<PriceLevel>& a, const std::optional<PriceLevel>& b) {
    return a.has_value() == b.has_value() && (!a || (a->price == b->price && a->total_quantity == b->total_quantity));
}
void validate_outcome(const Result& expected, const Result& actual) {
    if (expected.checksum != actual.checksum || !same_quote(expected.final_bbo.bid, actual.final_bbo.bid) ||
        !same_quote(expected.final_bbo.ask, actual.final_bbo.ask) ||
        !same_levels(expected.final_bids, actual.final_bids) || !same_levels(expected.final_asks, actual.final_asks))
        throw std::runtime_error("checksum/final BBO/full-depth mismatch");
}
void print_quote(const Bbo& bbo) {
    for (const auto& level : {bbo.bid, bbo.ask}) {
        if (level) std::cout << level->price << ':' << level->total_quantity << ' ';
        else std::cout << "absent ";
    }
}
double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
}
void report(const char* label, const Result& result, const Counts& count, double depth_ms = -1) {
    std::cout << label << " overall_ms=" << result.ms
              << " add_mean_ns=" << result.ns[0] / count.adds
              << " cancel_mean_ns=" << result.ns[1] / count.cancels
              << " bbo_mean_ns=" << result.ns[2] / count.bbos;
    if (count.depth) std::cout << " bid_depth_mean_ns=" << result.ns[3] / count.depth
                             << " ask_depth_mean_ns=" << result.ns[4] / count.depth;
    else std::cout << " bid_depth_mean_ns=n/a ask_depth_mean_ns=n/a";
    std::cout << " depth_total_ms=" << (depth_ms >= 0 ? depth_ms : (result.ns[3] + result.ns[4]) / 1000000)
              << " process_peak_rss_kib=" << result.os.peak_kib << " replay_minor_faults=" << result.os.minor
              << " replay_major_faults=" << result.os.major << " checksum=" << result.checksum << " final_bbo=";
    print_quote(result.final_bbo); std::cout << '\n' << std::flush;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") { usage(std::cout); return 0; }
        const auto config = parse(argc, argv);
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "config impl=" << config.impl << " ops=" << config.ops << " runs=" << config.runs
                  << " seed=" << config.seed << " C++=" << __cplusplus << " compiler=" << __VERSION__ << '\n';
#ifdef __linux__
        utsname os{};
        if (uname(&os) == 0) std::cout << "os=" << os.sysname << ' ' << os.release << ' ' << os.machine << '\n';
        std::cout << "RSS scope: process lifetime high-water KiB, includes trace and setup/validation; "
                     "faults: per-replay deltas, construction/destruction excluded.\n";
#else
        std::cout << "OS resource counters unavailable; printed resource fields are zero.\n";
#endif
        std::cout << "clock steady=" << Clock::is_steady << " nominal_tick_ns="
                  << 1e9 * Clock::period::num / Clock::period::den << '\n';
        const auto trace = generate(config); // Once per invocation, outside all replay timers.
        const auto count = counts(config.ops);
        const auto fingerprint = validate_trace(trace, count);
        if (config.profile_only) {
            // Use only the fixed seed-42 workload; other traces require full cross-validation.
            if (fingerprint != 1002121172284863879ULL)
                throw std::runtime_error("profile trace differs from fixed benchmark fixture");
            std::cout << "PROFILE_ONLY selected implementation; whole-process perf includes setup, replay, final checks and destruction.\n";
            auto book = make_book(config.impl);
            const auto result = replay(*book, trace, true);
            if (result.checksum != 12750139682901687748ULL ||
                !same_quote(result.final_bbo.bid, PriceLevel{9999, 2988}) ||
                !same_quote(result.final_bbo.ask, PriceLevel{10001, 2344}))
                throw std::runtime_error("profile result differs from fixed benchmark fixture");
            report("profile_run=1", result, count);
            std::cout << "PROFILE_FIXTURE_VALIDATION=PASS\n";
            return 0;
        }
        Result expected;
        for (char impl : {'A', 'B', 'C'}) {
            auto book = make_book(impl);
            const auto result = replay(*book, trace, false);
            if (impl == 'A') expected = result; else validate_outcome(expected, result);
            std::cout << "untimed_validation impl=" << impl << " PASS checksum=" << result.checksum
                      << " final_bbo="; print_quote(result.final_bbo); std::cout << '\n' << std::flush;
        }
        std::vector<Result> results;
        results.reserve(static_cast<std::size_t>(config.runs));
        for (std::uint64_t run = 0; run < config.runs; ++run) {
            auto book = make_book(config.impl); // Construction and destruction are outside replay timing.
            auto result = replay(*book, trace, true);
            validate_outcome(expected, result);
            const auto label = "run=" + std::to_string(run + 1);
            report(label.c_str(), result, count);
            results.push_back(std::move(result));
        }
        auto metric = [&](auto field) {
            std::vector<double> values;
            for (const auto& result : results) values.push_back(field(result));
            return median(std::move(values));
        };
        Result middle = expected;
        middle.ms = metric([](const Result& r) { return r.ms; });
        for (std::size_t i = 0; i < 5; ++i) middle.ns[i] = metric([i](const Result& r) { return r.ns[i]; });
        middle.os.peak_kib = metric([](const Result& r) { return r.os.peak_kib; });
        middle.os.minor = metric([](const Result& r) { return r.os.minor; });
        middle.os.major = metric([](const Result& r) { return r.os.major; });
        // Summed depth medians need not equal median(total depth); print it explicitly.
        const auto depth_ms = metric([](const Result& r) { return (r.ns[3] + r.ns[4]) / 1e6; });
        report("median", middle, count, depth_ms);
        std::cout << "OUTCOME_CONSISTENCY=PASS\n";
    } catch (const std::exception& e) {
        std::cerr << "Benchmark error: " << e.what() << '\n'; usage(std::cerr); return 1;
    }
}
