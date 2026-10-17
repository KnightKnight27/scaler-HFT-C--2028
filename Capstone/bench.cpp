#include "common.h"
#include "bookA.h"
#include "bookB.h"
#include "bookC.h"

// usage:
//   ./bench --impl A        (or B or C)
//   ./bench --verify
// extra options: --runs N (default 5), --ops N (default 1000000)

#define OP_ADD 0
#define OP_CANCEL 1
#define OP_BBO 2

struct Op {
    int type;
    uint64_t id;
    Side side;
    double price;
    long qty;
};

// makes the random workload. same seed every time so A B C get the exact same ops
vector<Op> makeWorkload(int n) {
    mt19937 rng(42);
    vector<Op> ops;
    ops.reserve(n);
    vector<uint64_t> live; // ids that are still in the book
    uint64_t nextId = 1;

    for (int i = 0; i < n; i++) {
        Op op;
        op.id = 0;
        op.side = Side::Buy;
        op.price = 0;
        op.qty = 0;

        int r = rng() % 100;
        if (r < 70 || (r < 90 && live.empty())) {
            op.type = OP_ADD;
            op.id = nextId++;
            if (rng() % 2 == 0) {
                op.side = Side::Buy;
                op.price = toPrice(MIN_BID + rng() % 5000);
            } else {
                op.side = Side::Sell;
                op.price = toPrice(MIN_ASK + rng() % 5000);
            }
            op.qty = 1 + rng() % 100;
            live.push_back(op.id);
        } else if (r < 90) {
            op.type = OP_CANCEL;
            int idx = rng() % live.size();
            op.id = live[idx];
            live[idx] = live.back(); // swap with last and pop, order doesnt matter
            live.pop_back();
        } else {
            op.type = OP_BBO;
        }
        ops.push_back(op);
    }
    return ops;
}

OrderBookBase *makeBook(char c) {
    if (c == 'A') return new OrderBookA();
    if (c == 'B') return new OrderBookB();
    if (c == 'C') return new OrderBookC();
    return NULL;
}

string implName(char c) {
    if (c == 'A') return "std::map levels + list per level + unordered_map ids";
    if (c == 'B') return "sorted vector levels + std::map ids";
    return "price indexed array + cached best + vector by id";
}

typedef chrono::steady_clock Clock;

long long nsBetween(Clock::time_point a, Clock::time_point b) {
    return chrono::duration_cast<chrono::nanoseconds>(b - a).count();
}

void printBBO(const BBO &b) {
    cout << fixed << setprecision(2);
    if (b.has_bid) cout << "Bid: " << b.bid_price << " x " << b.bid_qty << "\n";
    else cout << "Bid: (none)\n";
    if (b.has_ask) cout << "Ask: " << b.ask_price << " x " << b.ask_qty << "\n";
    else cout << "Ask: (none)\n";
}

// ------------------------------------------------------------------
// benchmark
// ------------------------------------------------------------------

struct RunResult {
    double totalMs;
    double addNs;
    double cancelNs;
    double bboNs;
    long long checksum;
    BBO last;
};

RunResult runOnce(char impl, const vector<Op> &ops) {
    OrderBookBase *book = makeBook(impl);
    long long addT = 0, cancelT = 0, bboT = 0;
    long long addN = 0, cancelN = 0, bboN = 0;
    long long checksum = 0;

    Clock::time_point start = Clock::now();
    for (size_t i = 0; i < ops.size(); i++) {
        const Op &op = ops[i];
        if (op.type == OP_ADD) {
            Clock::time_point t1 = Clock::now();
            book->add_order(op.id, op.side, op.price, op.qty);
            Clock::time_point t2 = Clock::now();
            addT += nsBetween(t1, t2);
            addN++;
        } else if (op.type == OP_CANCEL) {
            Clock::time_point t1 = Clock::now();
            bool ok = book->cancel_order(op.id);
            Clock::time_point t2 = Clock::now();
            cancelT += nsBetween(t1, t2);
            cancelN++;
            if (ok) checksum += 1;
        } else {
            Clock::time_point t1 = Clock::now();
            BBO b = book->get_bbo();
            Clock::time_point t2 = Clock::now();
            bboT += nsBetween(t1, t2);
            bboN++;
            // use the result so the compiler cant throw the call away
            checksum += b.bid_qty + b.ask_qty + toTicks(b.bid_price) + toTicks(b.ask_price);
        }

        if ((i + 1) % 100 == 0) {
            vector<Level> bb = book->get_bids(10);
            vector<Level> aa = book->get_asks(10);
            for (size_t k = 0; k < bb.size(); k++) checksum += bb[k].qty;
            for (size_t k = 0; k < aa.size(); k++) checksum += aa[k].qty;
        }
    }
    Clock::time_point end = Clock::now();

    RunResult r;
    r.totalMs = nsBetween(start, end) / 1e6;
    r.addNs = addN ? (double)addT / addN : 0;
    r.cancelNs = cancelN ? (double)cancelT / cancelN : 0;
    r.bboNs = bboN ? (double)bboT / bboN : 0;
    r.checksum = checksum;
    r.last = book->get_bbo();
    delete book;
    return r;
}

double median(vector<double> v) {
    sort(v.begin(), v.end());
    int n = v.size();
    if (n % 2 == 1) return v[n / 2];
    return (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

int runBench(char impl, int nOps, int runs) {
    vector<Op> ops = makeWorkload(nOps);
    int adds = 0, cancels = 0, bbos = 0;
    for (size_t i = 0; i < ops.size(); i++) {
        if (ops[i].type == OP_ADD) adds++;
        else if (ops[i].type == OP_CANCEL) cancels++;
        else bbos++;
    }

    cout << "Implementation " << impl << " (" << implName(impl) << ")\n";
    cout << "ops: " << nOps << "  (add " << adds << ", cancel " << cancels << ", bbo " << bbos
         << ", + get_bids/get_asks every 100 ops)\n";
    cout << "runs: " << runs << "\n\n";

    vector<double> tot, add, can, bbo;
    RunResult last;
    for (int r = 0; r < runs; r++) {
        last = runOnce(impl, ops);
        tot.push_back(last.totalMs);
        add.push_back(last.addNs);
        can.push_back(last.cancelNs);
        bbo.push_back(last.bboNs);
        cout << fixed << setprecision(2);
        cout << "run " << r + 1 << ": total " << last.totalMs << " ms | add " << last.addNs
             << " ns | cancel " << last.cancelNs << " ns | bbo " << last.bboNs << " ns\n";
    }

    cout << "\nMedian of " << runs << " runs:\n";
    cout << "Total time (ms): " << median(tot) << "\n";
    cout << "ns / add:        " << median(add) << "\n";
    cout << "ns / cancel:     " << median(can) << "\n";
    cout << "ns / BBO:        " << median(bbo) << "\n";

    cout << "\nFinal BBO:\n";
    printBBO(last.last);
    cout << "\nChecksum: " << last.checksum << "\n";
    return 0;
}

// ------------------------------------------------------------------
// correctness check
// ------------------------------------------------------------------

bool sameBBO(const BBO &a, const BBO &b) {
    if (a.has_bid != b.has_bid || a.has_ask != b.has_ask) return false;
    if (a.has_bid && (a.bid_price != b.bid_price || a.bid_qty != b.bid_qty)) return false;
    if (a.has_ask && (a.ask_price != b.ask_price || a.ask_qty != b.ask_qty)) return false;
    return true;
}

bool sameLevels(const vector<Level> &a, const vector<Level> &b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].price != b[i].price || a[i].qty != b[i].qty) return false;
    }
    return true;
}

// small hand made tests with known answers
bool basicTests(char impl) {
    OrderBookBase *b = makeBook(impl);
    bool ok = true;

    BBO x = b->get_bbo();
    if (x.has_bid || x.has_ask) { cout << "  [" << impl << "] empty book bbo is wrong\n"; ok = false; }
    if (b->get_bids(10).size() != 0 || b->get_asks(10).size() != 0) { cout << "  [" << impl << "] empty book has levels\n"; ok = false; }
    if (b->cancel_order(12345)) { cout << "  [" << impl << "] cancel unknown id returned true\n"; ok = false; }

    b->add_order(1, Side::Buy, 99.50, 10);
    b->add_order(2, Side::Buy, 99.50, 15); // same price as 1
    b->add_order(3, Side::Buy, 99.40, 5);
    b->add_order(4, Side::Buy, 98.00, 7);
    b->add_order(5, Side::Sell, 100.05, 20);
    b->add_order(6, Side::Sell, 100.01, 3);

    x = b->get_bbo();
    if (!x.has_bid || x.bid_price != 99.50 || x.bid_qty != 25) { cout << "  [" << impl << "] best bid wrong\n"; ok = false; }
    if (!x.has_ask || x.ask_price != 100.01 || x.ask_qty != 3) { cout << "  [" << impl << "] best ask wrong\n"; ok = false; }

    // only 3 bid levels but asking for 10
    vector<Level> bids = b->get_bids(10);
    if (bids.size() != 3) { cout << "  [" << impl << "] get_bids(10) should give 3 levels\n"; ok = false; }
    else if (bids[0].price != 99.50 || bids[0].qty != 25 || bids[1].price != 99.40 || bids[1].qty != 5 ||
             bids[2].price != 98.00 || bids[2].qty != 7) { cout << "  [" << impl << "] bid levels wrong\n"; ok = false; }

    vector<Level> asks = b->get_asks(10);
    if (asks.size() != 2 || asks[0].price != 100.01 || asks[1].price != 100.05 || asks[1].qty != 20) {
        cout << "  [" << impl << "] ask levels wrong\n"; ok = false;
    }
    if (b->get_bids(1).size() != 1) { cout << "  [" << impl << "] get_bids(1) wrong\n"; ok = false; }

    // cancel twice, 2nd time must be false
    if (!b->cancel_order(6)) { cout << "  [" << impl << "] cancel 6 failed\n"; ok = false; }
    if (b->cancel_order(6)) { cout << "  [" << impl << "] cancel 6 again returned true\n"; ok = false; }
    x = b->get_bbo();
    if (!x.has_ask || x.ask_price != 100.05 || x.ask_qty != 20) { cout << "  [" << impl << "] ask after cancel wrong\n"; ok = false; }

    b->cancel_order(1);
    x = b->get_bbo();
    if (x.bid_price != 99.50 || x.bid_qty != 15) { cout << "  [" << impl << "] partial level cancel wrong\n"; ok = false; }
    b->cancel_order(2);
    x = b->get_bbo();
    if (x.bid_price != 99.40 || x.bid_qty != 5) { cout << "  [" << impl << "] next best bid wrong\n"; ok = false; }

    b->cancel_order(3);
    b->cancel_order(4);
    b->cancel_order(5);
    x = b->get_bbo();
    if (x.has_bid || x.has_ask) { cout << "  [" << impl << "] book should be empty again\n"; ok = false; }

    delete b;
    return ok;
}

// when the 3 answers dont all match, figure out who is the odd one out
void blame(bool e01, bool e02, bool e12, bool bad[3]) {
    if (e01 && e02 && e12) return;
    if (e12) bad[0] = true;
    else if (e02) bad[1] = true;
    else if (e01) bad[2] = true;
    else bad[0] = bad[1] = bad[2] = true;
}

int runVerify(int nOps) {
    cout << "Running correctness test...\n\n";
    const char names[3] = {'A', 'B', 'C'};
    bool bad[3] = {false, false, false};

    cout << "Basic tests (empty book, unknown cancel, double cancel, short depth)...\n";
    for (int k = 0; k < 3; k++) {
        if (!basicTests(names[k])) bad[k] = true;
    }

    cout << "Random workload: " << nOps << " ops, seed 42, comparing after every op...\n";
    vector<Op> ops = makeWorkload(nOps);
    OrderBookBase *books[3];
    for (int k = 0; k < 3; k++) books[k] = makeBook(names[k]);

    int mismatches = 0;
    for (size_t i = 0; i < ops.size(); i++) {
        const Op &op = ops[i];
        if (op.type == OP_ADD) {
            for (int k = 0; k < 3; k++) books[k]->add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == OP_CANCEL) {
            bool r[3];
            for (int k = 0; k < 3; k++) r[k] = books[k]->cancel_order(op.id);
            if (!(r[0] == r[1] && r[1] == r[2])) {
                blame(r[0] == r[1], r[0] == r[2], r[1] == r[2], bad);
                if (mismatches++ < 5) cout << "  cancel result mismatch at op " << i << "\n";
            }
        }

        // bbo after every op
        BBO b[3];
        for (int k = 0; k < 3; k++) b[k] = books[k]->get_bbo();
        bool e01 = sameBBO(b[0], b[1]), e02 = sameBBO(b[0], b[2]), e12 = sameBBO(b[1], b[2]);
        if (!(e01 && e02 && e12)) {
            blame(e01, e02, e12, bad);
            if (mismatches++ < 5) cout << "  BBO mismatch at op " << i << "\n";
        }

        // top 10 levels after every op, and the WHOLE book every 10000 ops
        int depth = 10;
        if (i % 10000 == 0 || i + 1 == ops.size()) depth = 100000;
        vector<Level> bl[3], al[3];
        for (int k = 0; k < 3; k++) {
            bl[k] = books[k]->get_bids(depth);
            al[k] = books[k]->get_asks(depth);
        }
        e01 = sameLevels(bl[0], bl[1]) && sameLevels(al[0], al[1]);
        e02 = sameLevels(bl[0], bl[2]) && sameLevels(al[0], al[2]);
        e12 = sameLevels(bl[1], bl[2]) && sameLevels(al[1], al[2]);
        if (!(e01 && e02 && e12)) {
            blame(e01, e02, e12, bad);
            if (mismatches++ < 5) cout << "  level mismatch at op " << i << "\n";
        }
    }

    // cancelling ids that were already cancelled / never existed must be false everywhere
    for (int k = 0; k < 3; k++) {
        if (books[k]->cancel_order(999999999999ULL)) bad[k] = true;
        if (books[k]->cancel_order(0)) bad[k] = true;
        for (size_t i = 0; i < ops.size(); i++) {
            if (ops[i].type == OP_CANCEL) {
                if (books[k]->cancel_order(ops[i].id)) bad[k] = true;
                break;
            }
        }
    }

    BBO fin = books[0]->get_bbo();
    for (int k = 0; k < 3; k++) delete books[k];

    cout << "\n";
    for (int k = 0; k < 3; k++) {
        cout << "Implementation " << names[k] << ": " << (bad[k] ? "FAIL" : "PASS") << "\n";
    }
    cout << "\n";
    if (!bad[0] && !bad[1] && !bad[2]) {
        cout << "All implementations produce identical results.\n";
        cout << "\nFinal BBO:\n";
        printBBO(fin);
        return 0;
    }
    cout << "Implementations DO NOT agree (" << mismatches << " mismatches)\n";
    return 1;
}

int main(int argc, char **argv) {
    char impl = 0;
    bool verify = false;
    int runs = 5;
    int nOps = 1000000;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--verify") == 0) verify = true;
        else if (strcmp(argv[i], "--impl") == 0 && i + 1 < argc) impl = argv[++i][0];
        else if (strcmp(argv[i], "--runs") == 0 && i + 1 < argc) runs = atoi(argv[++i]);
        else if (strcmp(argv[i], "--ops") == 0 && i + 1 < argc) nOps = atoi(argv[++i]);
    }
    if (impl >= 'a' && impl <= 'c') impl = impl - 'a' + 'A'; // allow lower case

    if (verify) return runVerify(nOps);

    if (impl != 'A' && impl != 'B' && impl != 'C') {
        cout << "usage: " << argv[0] << " --impl A|B|C [--runs N] [--ops N]\n";
        cout << "       " << argv[0] << " --verify [--ops N]\n";
        return 1;
    }
    if (runs < 1) runs = 1;
    return runBench(impl, nOps, runs);
}
