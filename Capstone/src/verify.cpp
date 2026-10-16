#include "verify.h"

#include <cinttypes>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "book_a.h"
#include "book_b.h"
#include "book_c.h"
#include "oracle.h"
#include "workload.h"

namespace {

constexpr std::size_t kThinBookOps = 200'000;
constexpr int kFullDepth = kNumTicks;  // more levels than a side can ever hold

std::string format_op(const Op& op) {
    char buf[128];
    switch (op.type) {
        case OpType::Add:
            std::snprintf(buf, sizeof(buf), "Add id=%" PRIu64 " side=%s price=%.2f qty=%ld", op.id,
                          op.side == Side::Buy ? "Buy" : "Sell", op.price, op.qty);
            break;
        case OpType::Cancel:
            std::snprintf(buf, sizeof(buf), "Cancel id=%" PRIu64, op.id);
            break;
        case OpType::Bbo:
            std::snprintf(buf, sizeof(buf), "BBO query");
            break;
    }
    return buf;
}

std::string format_side(bool present, double price, long qty) {
    if (!present) return "none";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f x %ld", price, qty);
    return buf;
}

std::string format_bbo(const BBO& b) {
    return "bid=" + format_side(b.has_bid, b.bid_price, b.bid_qty) +
           " ask=" + format_side(b.has_ask, b.ask_price, b.ask_qty);
}

// Price fields are only meaningful when the matching has_* flag is set.
bool same_bbo(const BBO& a, const BBO& b) {
    if (a.has_bid != b.has_bid || a.has_ask != b.has_ask) return false;
    if (a.has_bid && (a.bid_price != b.bid_price || a.bid_qty != b.bid_qty)) return false;
    if (a.has_ask && (a.ask_price != b.ask_price || a.ask_qty != b.ask_qty)) return false;
    return true;
}

// Returns an empty string if the two level lists are identical, otherwise a description
// of the first differing position. Only that position is printed: a full-depth list can
// hold 5,000 levels.
std::string diff_levels(const char* query, const std::vector<Level>& expected, const std::vector<Level>& actual) {
    const std::size_t common = std::min(expected.size(), actual.size());
    std::size_t k = 0;
    while (k < common && expected[k].price == actual[k].price && expected[k].qty == actual[k].qty) ++k;
    if (k == common && expected.size() == actual.size()) return "";

    const auto level_at = [](const std::vector<Level>& v, std::size_t i) {
        return i < v.size() ? format_side(true, v[i].price, v[i].qty) : std::string("(missing)");
    };
    return std::string(query) + ", first difference at level #" + std::to_string(k) +
           "\n    expected: " + level_at(expected, k) + "  (" + std::to_string(expected.size()) + " levels)" +
           "\n    actual:   " + level_at(actual, k) + "  (" + std::to_string(actual.size()) + " levels)";
}

template <typename Book>
std::string diff_depth(const Book& book, const OracleBook& oracle, int depth) {
    const std::string bids = diff_levels("get_bids", oracle.get_bids(depth), book.get_bids(depth));
    if (!bids.empty()) return bids;
    return diff_levels("get_asks", oracle.get_asks(depth), book.get_asks(depth));
}

// C pre-sizes its hash table to 2M slots, so under the graded workload it never grows
// and its probe chains stay short. Starting from 16 slots forces repeated growth and
// long probe chains, exercising grow() and backward-shift deletion. Verification only.
struct BookCSmallTable : BookC {
    BookCSmallTable() : BookC(16) {}
};

struct Workload {
    std::string label;
    std::vector<Op> ops;
    std::size_t deep_check_interval;  // full-depth compare + check_invariants() every N ops
};

// Replays one workload through Book and the oracle in lockstep. Returns an empty
// string on success, otherwise a description of the first mismatch.
template <typename Book>
std::string replay(const Workload& workload) {
    Book book;
    OracleBook oracle;
    const std::vector<Op>& ops = workload.ops;

    for (std::size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        std::string diff;

        switch (op.type) {
            case OpType::Add:
                book.add_order(op.id, op.side, op.price, op.qty);
                oracle.add_order(op.id, op.side, op.price, op.qty);
                break;
            case OpType::Cancel: {
                const bool actual = book.cancel_order(op.id);
                const bool expected = oracle.cancel_order(op.id);
                if (actual != expected) {
                    diff = std::string("cancel_order result\n    expected: ") + (expected ? "true" : "false") +
                           "\n    actual:   " + (actual ? "true" : "false");
                }
                break;
            }
            case OpType::Bbo:
                break;  // the BBO comparison below runs after every op anyway
        }

        if (diff.empty()) {
            const BBO actual = book.get_bbo();
            const BBO expected = oracle.get_bbo();
            if (!same_bbo(actual, expected)) {
                diff = "get_bbo\n    expected: " + format_bbo(expected) + "\n    actual:   " + format_bbo(actual);
            }
        }
        if (diff.empty()) diff = diff_depth(book, oracle, kDepthQueryLevels);

        const bool deep_check = (i + 1) % workload.deep_check_interval == 0 || i + 1 == ops.size();
        if (diff.empty() && deep_check) {
            diff = diff_depth(book, oracle, kFullDepth);
            if (diff.empty() && !book.check_invariants()) diff = "check_invariants() returned false";
        }

        if (!diff.empty()) {
            return workload.label + " workload, op " + std::to_string(i) + ": " + format_op(op) +
                   "\n  mismatch in " + diff;
        }
    }
    return "";
}

template <typename Book>
bool verify_one(const char* name, const std::vector<Workload>& workloads) {
    for (const Workload& workload : workloads) {
        const std::string failure = replay<Book>(workload);
        if (!failure.empty()) {
            std::printf("Implementation %s: FAIL\n  %s\n", name, failure.c_str());
            return false;
        }
    }
    std::printf("Implementation %s: PASS\n", name);
    return true;
}

// Runs the oracle alone and prints facts about the workload's book shape.
// These come from a deterministic replay, not from timing.
//
// "Best level emptied" counts cancels that removed the last order at the best price,
// i.e. the event that forces C's bitmap search and B's erase at the back of its vector.
void print_workload_profile(const Workload& workload, const WorkloadSpec& spec, std::size_t fallback_adds) {
    const std::vector<Op>& ops = workload.ops;
    const WorkloadStats stats = count_ops(ops);
    std::printf("[%s] %zu ops, seed %" PRIu32 ", %s ids (adds=%zu, cancels=%zu, bbo=%zu, fallback adds=%zu)\n",
                workload.label.c_str(), ops.size(), spec.seed, spec.scrambled_ids ? "scrambled" : "sequential",
                stats.adds, stats.cancels, stats.bbos, fallback_adds);
    std::printf("  workload hash: %016" PRIx64 "\n", hash_workload(ops));

    OracleBook oracle;
    long best_bid_emptied = 0;
    long best_ask_emptied = 0;
    long emptied_in_second_half = 0;
    std::size_t last_emptied_op = 0;

    for (std::size_t i = 0; i < ops.size(); ++i) {
        const Op& op = ops[i];
        if (op.type == OpType::Add) oracle.add_order(op.id, op.side, op.price, op.qty);
        if (op.type != OpType::Cancel) continue;

        const BBO before = oracle.get_bbo();
        const long emptied_before = oracle.levels_emptied();
        oracle.cancel_order(op.id);
        if (oracle.levels_emptied() == emptied_before) continue;

        last_emptied_op = i;
        if (i >= ops.size() / 2) ++emptied_in_second_half;
        const BBO after = oracle.get_bbo();
        if (before.has_bid && (!after.has_bid || after.bid_price != before.bid_price)) ++best_bid_emptied;
        if (before.has_ask && (!after.has_ask || after.ask_price != before.ask_price)) ++best_ask_emptied;
    }
    if (!oracle.check_invariants()) {
        std::printf("ORACLE SELF-CHECK FAILED\n");
        std::exit(1);
    }
    std::printf("  book at end: %zu live orders, non-empty levels bid=%d/5000 ask=%d/5000\n",
                oracle.live_orders(), oracle.non_empty_levels(Side::Buy), oracle.non_empty_levels(Side::Sell));
    std::printf("  levels: %ld created, %ld emptied (%ld in second half of ops, last at op %zu)\n",
                oracle.levels_created(), oracle.levels_emptied(), emptied_in_second_half, last_emptied_op);
    std::printf("  best level emptied by a cancel: bid %ld times, ask %ld times\n", best_bid_emptied,
                best_ask_emptied);
}

}  // namespace

int run_verify(const std::string& impls, std::size_t num_ops, uint32_t seed) {
    std::printf("Running correctness test...\n");

    // 1. The graded workload.
    // 2. A thin-book stress workload (verification only) with scrambled ids. It keeps
    //    emptying levels and the best price, which the graded workload almost never does,
    //    and its ids collide in hash tables, which sequential ids almost never do.
    WorkloadSpec graded_spec;
    graded_spec.num_ops = num_ops;
    graded_spec.seed = seed;

    WorkloadSpec thin_spec;
    thin_spec.num_ops = kThinBookOps;
    thin_spec.seed = seed + 1;
    thin_spec.mix = kThinBookMix;
    thin_spec.scrambled_ids = true;

    std::size_t graded_fallbacks = 0;
    std::size_t thin_fallbacks = 0;
    std::vector<Workload> workloads;
    workloads.push_back({"graded", generate_workload(graded_spec, &graded_fallbacks), 10'000});
    workloads.push_back({"thin-book", generate_workload(thin_spec, &thin_fallbacks), 1'000});
    print_workload_profile(workloads[0], graded_spec, graded_fallbacks);
    print_workload_profile(workloads[1], thin_spec, thin_fallbacks);

    bool all_pass = true;
    for (const char name : impls) {
        switch (name) {
            case 'A': all_pass &= verify_one<BookA>("A", workloads); break;
            case 'B': all_pass &= verify_one<BookB>("B", workloads); break;
            case 'C':
                all_pass &= verify_one<BookC>("C", workloads);
                all_pass &= verify_one<BookCSmallTable>("C (16-slot initial table)", workloads);
                break;
            default: std::printf("Unknown implementation '%c'\n", name); return 2;
        }
    }

    // Each implementation was compared exactly against the same oracle on every op,
    // so passing all three means they produced identical results to each other.
    if (all_pass && impls == "ABC") std::printf("All implementations produce identical results.\n");
    return all_pass ? 0 : 1;
}
