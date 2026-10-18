#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "array_book.hpp"
#include "common.hpp"
#include "heap_book.hpp"
#include "map_book.hpp"

const int NUM_OPS = 1000000;
const int RUNS = 5;

enum OpType { ADD, CANCEL, QUERY };

struct Op {
    OpType type;
    uint64_t id;
    Side side;
    int price;
    long qty;
};

std::vector<Op> make_workload() {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> percent(1, 100);
    std::uniform_int_distribution<int> coin(0, 1);
    std::uniform_int_distribution<int> qty_dist(1, 100);
    std::uniform_int_distribution<int> bid_dist(MIN_BID, MAX_BID);
    std::uniform_int_distribution<int> ask_dist(MIN_ASK, MAX_ASK);

    std::vector<Op> ops;
    ops.reserve(NUM_OPS);
    std::vector<uint64_t> live;
    uint64_t next_id = 1;

    for (int i = 0; i < NUM_OPS; i++) {
        int r = percent(rng);
        Op op = {QUERY, 0, Side::Buy, 0, 0};

        if (r <= 70 || (r <= 90 && live.empty())) {
            op.type = ADD;
            op.id = next_id++;
            op.side = coin(rng) == 0 ? Side::Buy : Side::Sell;
            op.price = op.side == Side::Buy ? bid_dist(rng) : ask_dist(rng);
            op.qty = qty_dist(rng);
            live.push_back(op.id);
        } else if (r <= 90) {
            std::uniform_int_distribution<size_t> pick(0, live.size() - 1);
            size_t k = pick(rng);
            op.type = CANCEL;
            op.id = live[k];
            live[k] = live.back();
            live.pop_back();
        }
        ops.push_back(op);
    }
    return ops;
}

void print_price(int ticks) { std::printf("%d.%02d", ticks / 100, ticks % 100); }

struct Result {
    double total_ms;
    double add_ns;
    double cancel_ns;
    double bbo_ns;
    BBO final_bbo;
    uint64_t checksum;
};

template <typename Book>
Result run_once(const std::vector<Op>& ops) {
    typedef std::chrono::steady_clock clock;
    Book* book = new Book();
    long long add_ns = 0, cancel_ns = 0, bbo_ns = 0;
    long adds = 0, cancels = 0, bbos = 0;
    uint64_t checksum = 0;

    clock::time_point start = clock::now();
    for (int i = 0; i < (int)ops.size(); i++) {
        const Op& op = ops[i];
        clock::time_point t0 = clock::now();
        if (op.type == ADD) {
            book->add_order(op.id, op.side, op.price, op.qty);
            add_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - t0).count();
            adds++;
        } else if (op.type == CANCEL) {
            checksum += book->cancel_order(op.id);
            cancel_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - t0).count();
            cancels++;
        } else {
            BBO b = book->get_bbo();
            bbo_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - t0).count();
            bbos++;
            checksum += b.bid_price + b.bid_qty + b.ask_price + b.ask_qty;
        }

        if (i % 100 == 99) {
            std::vector<Level> b = book->get_bids(10);
            std::vector<Level> a = book->get_asks(10);
            for (size_t j = 0; j < b.size(); j++) checksum += b[j].price * b[j].qty;
            for (size_t j = 0; j < a.size(); j++) checksum += a[j].price * a[j].qty;
        }
    }
    clock::time_point end = clock::now();

    Result r;
    r.total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    r.add_ns = (double)add_ns / adds;
    r.cancel_ns = (double)cancel_ns / cancels;
    r.bbo_ns = (double)bbo_ns / bbos;
    r.final_bbo = book->get_bbo();
    r.checksum = checksum;
    delete book;
    return r;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <typename Book>
void benchmark(const char* name, const std::vector<Op>& ops) {
    std::printf("Benchmarking Implementation %s (%d runs)...\n", name, RUNS);
    std::vector<double> total, add, cancel, bbo;
    Result last;
    for (int i = 0; i < RUNS; i++) {
        last = run_once<Book>(ops);
        std::printf("Run %d: %.2f ms | add: %.2f ns | cancel: %.2f ns | BBO: %.2f ns\n", i + 1,
                    last.total_ms, last.add_ns, last.cancel_ns, last.bbo_ns);
        total.push_back(last.total_ms);
        add.push_back(last.add_ns);
        cancel.push_back(last.cancel_ns);
        bbo.push_back(last.bbo_ns);
    }

    std::printf("\nMedian Result:\n");
    std::printf("Total time: %.2f ms\n", median(total));
    std::printf("ns / add:    %.2f\n", median(add));
    std::printf("ns / cancel: %.2f\n", median(cancel));
    std::printf("ns / BBO:    %.2f\n", median(bbo));

    std::printf("\nFinal BBO:\nBid: ");
    print_price(last.final_bbo.bid_price);
    std::printf(" x %ld\nAsk: ", last.final_bbo.bid_qty);
    print_price(last.final_bbo.ask_price);
    std::printf(" x %ld\n\nChecksum: %llu\n", last.final_bbo.ask_qty,
                (unsigned long long)last.checksum);
}

bool same_bbo(const BBO& a, const BBO& b) {
    if (a.has_bid != b.has_bid || a.has_ask != b.has_ask)
        return false;
    if (a.has_bid && (a.bid_price != b.bid_price || a.bid_qty != b.bid_qty))
        return false;
    if (a.has_ask && (a.ask_price != b.ask_price || a.ask_qty != b.ask_qty))
        return false;
    return true;
}

bool same_levels(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].price != b[i].price || a[i].qty != b[i].qty)
            return false;
    return true;
}

template <typename Book>
bool edge_cases() {
    Book book;
    BBO b = book.get_bbo();
    if (b.has_bid || b.has_ask)
        return false;
    if (book.cancel_order(12345))
        return false;

    book.add_order(1, Side::Buy, 9900, 10);
    book.add_order(2, Side::Buy, 9900, 5);
    book.add_order(3, Side::Buy, 9800, 7);
    book.add_order(4, Side::Buy, 9700, 1);
    book.add_order(5, Side::Sell, 10100, 3);
    book.add_order(6, Side::Buy, 4000, 3);   // bad price, ignored
    book.add_order(7, Side::Sell, 9000, 3);  // bad price, ignored

    b = book.get_bbo();
    if (!b.has_bid || b.bid_price != 9900 || b.bid_qty != 15)
        return false;
    if (!b.has_ask || b.ask_price != 10100 || b.ask_qty != 3)
        return false;
    if (book.get_bids(10).size() != 3 || book.get_asks(10).size() != 1)
        return false;

    if (!book.cancel_order(1) || book.cancel_order(1))
        return false;
    if (book.get_bbo().bid_qty != 5)
        return false;
    if (!book.cancel_order(2) || book.get_bbo().bid_price != 9800)
        return false;
    if (!book.cancel_order(5) || book.get_bbo().has_ask)
        return false;

    book.add_order(8, Side::Buy, MIN_BID, 2);
    if (!book.cancel_order(3) || !book.cancel_order(4) || book.get_bbo().bid_price != MIN_BID)
        return false;
    if (!book.cancel_order(8) || book.get_bbo().has_bid || !book.get_bids(10).empty())
        return false;
    return true;
}

int verify(const std::vector<Op>& ops) {
    std::printf("Running correctness test...\n\n");
    bool edge_a = edge_cases<MapBook>();
    bool edge_b = edge_cases<HeapBook>();
    bool edge_c = edge_cases<ArrayBook>();

    MapBook* a = new MapBook();
    HeapBook* b = new HeapBook();
    ArrayBook* c = new ArrayBook();
    bool match_b = true, match_c = true;

    for (int i = 0; i < (int)ops.size(); i++) {
        const Op& op = ops[i];
        if (op.type == ADD) {
            a->add_order(op.id, op.side, op.price, op.qty);
            b->add_order(op.id, op.side, op.price, op.qty);
            c->add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == CANCEL) {
            bool ra = a->cancel_order(op.id);
            if (b->cancel_order(op.id) != ra) match_b = false;
            if (c->cancel_order(op.id) != ra) match_c = false;
        }

        BBO ba = a->get_bbo();
        std::vector<Level> bids = a->get_bids(10);
        std::vector<Level> asks = a->get_asks(10);
        if (!same_bbo(ba, b->get_bbo()) || !same_levels(bids, b->get_bids(10)) ||
            !same_levels(asks, b->get_asks(10)))
            match_b = false;
        if (!same_bbo(ba, c->get_bbo()) || !same_levels(bids, c->get_bids(10)) ||
            !same_levels(asks, c->get_asks(10)))
            match_c = false;

        if (!match_b && !match_c)
            break;
    }
    delete a;
    delete b;
    delete c;

    bool pass_a = edge_a && match_b && match_c;
    bool pass_b = edge_b && match_b;
    bool pass_c = edge_c && match_c;
    std::printf("Implementation A: %s\n", pass_a ? "PASS" : "FAIL");
    std::printf("Implementation B: %s\n", pass_b ? "PASS" : "FAIL");
    std::printf("Implementation C: %s\n", pass_c ? "PASS" : "FAIL");

    if (pass_a && pass_b && pass_c) {
        std::printf("\nAll implementations produce identical results.\n");
        return 0;
    }
    std::printf("\nImplementations do NOT agree.\n");
    return 1;
}

void usage() { std::printf("usage: ./bench --verify | --impl A|B|C\n"); }

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    std::vector<Op> ops = make_workload();

    if (std::strcmp(argv[1], "--verify") == 0)
        return verify(ops);

    if (std::strcmp(argv[1], "--impl") == 0 && argc >= 3) {
        if (std::strcmp(argv[2], "A") == 0)
            benchmark<MapBook>("A (Map)", ops);
        else if (std::strcmp(argv[2], "B") == 0)
            benchmark<HeapBook>("B (Heap)", ops);
        else if (std::strcmp(argv[2], "C") == 0)
            benchmark<ArrayBook>("C (Array)", ops);
        else {
            usage();
            return 1;
        }
        return 0;
    }

    usage();
    return 1;
}
