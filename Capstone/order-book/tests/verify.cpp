#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "../src/order_book_a.hpp"
#include "../src/order_book_b.hpp"
#include "../src/order_book_c.hpp"
#include "../src/workload.hpp"

bool same(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.has_ask == b.has_ask && a.bid_price == b.bid_price && a.bid_qty == b.bid_qty &&
           a.ask_price == b.ask_price && a.ask_qty == b.ask_qty;
}
bool same(const std::vector<Level>& a, const std::vector<Level>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].price != b[i].price || a[i].qty != b[i].qty) return false;
    return true;
}
template <class Book>
std::string edge_tests() {
    Book b;
    BBO e = b.get_bbo();
    if (e.has_bid || e.has_ask) return "empty book must have no bid/ask";
    if (b.cancel_order(42)) return "cancel of unknown id must be false";
    b.add_order(1, Side::Buy, 99.99, 5);
    b.add_order(2, Side::Buy, 99.99, 7);
    b.add_order(3, Side::Sell, 100.01, 3);
    BBO x = b.get_bbo();
    if (!x.has_bid || x.bid_price != 99.99 || x.bid_qty != 12) return "orders at one price must add up";
    if (!x.has_ask || x.ask_price != 100.01 || x.ask_qty != 3) return "wrong best ask";
    if (!b.cancel_order(1)) return "cancel of live order must be true";
    if (b.cancel_order(1)) return "cancel twice must be false";
    if (b.get_bbo().bid_qty != 7) return "qty after cancel wrong";
    b.cancel_order(2);
    if (b.get_bbo().has_bid) return "bid side must be empty again";
    b.add_order(10, Side::Buy, 99.50, 1);
    b.add_order(11, Side::Buy, 99.00, 2);
    b.add_order(12, Side::Buy, 98.00, 3);
    auto bids = b.get_bids(10);
    if (bids.size() != 3) return "get_bids(10) with 3 levels must return 3";
    if (bids[0].price != 99.50 || bids[2].price != 98.00) return "bids must be sorted best first";
    if (b.get_bids(2).size() != 2) return "get_bids(2) must return 2";
    b.add_order(20, Side::Buy, 100.00, 1);   // outside bid band
    b.add_order(21, Side::Sell, 99.99, 1);   // outside ask band
    b.add_order(22, Side::Buy, 99.00, 0);    // qty 0
    if (b.cancel_order(20) || b.cancel_order(21) || b.cancel_order(22)) return "invalid orders must be rejected";
    b.add_order(10, Side::Buy, 99.99, 100);  // duplicate id
    if (b.get_bbo().bid_price != 99.50) return "duplicate id must be ignored";
    return "";
}

int run_verify(size_t ops, unsigned seed) {
    std::cout << "Running correctness test...\n";
    std::string err[3] = {edge_tests<OrderBookA>(), edge_tests<OrderBookB>(), edge_tests<OrderBookC>()};

    std::vector<Op> w = make_workload(ops, seed);
    OrderBookA A; OrderBookB B; OrderBookC C;
    for (size_t i = 0; i < w.size(); ++i) {
        const Op& op = w[i];
        if (op.type == OP_ADD) {
            A.add_order(op.id, op.side, op.price, op.qty);
            B.add_order(op.id, op.side, op.price, op.qty);
            C.add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == OP_CANCEL) {
            bool a = A.cancel_order(op.id), b = B.cancel_order(op.id), c = C.cancel_order(op.id);
            if (a != b && err[1].empty()) err[1] = "cancel result differs at op " + std::to_string(i);
            if (a != c && err[2].empty()) err[2] = "cancel result differs at op " + std::to_string(i);
            // cancelling the same id again must fail everywhere
            if ((A.cancel_order(op.id) || B.cancel_order(op.id) || C.cancel_order(op.id)) && err[0].empty())
                err[0] = "double cancel returned true at op " + std::to_string(i);
        } else {
            BBO a = A.get_bbo();
            if (!same(a, B.get_bbo()) && err[1].empty()) err[1] = "BBO differs at op " + std::to_string(i);
            if (!same(a, C.get_bbo()) && err[2].empty()) err[2] = "BBO differs at op " + std::to_string(i);
        }
        if ((i + 1) % 100 == 0) {                        
            int d = ((i + 1) % 10000 == 0) ? 20000 : 10;
            auto ba = A.get_bids(d), aa = A.get_asks(d);
            if (!(same(ba, B.get_bids(d)) && same(aa, B.get_asks(d))) && err[1].empty())
                err[1] = "bid/ask levels differ at op " + std::to_string(i);
            if (!(same(ba, C.get_bids(d)) && same(aa, C.get_asks(d))) && err[2].empty())
                err[2] = "bid/ask levels differ at op " + std::to_string(i);
        }
    }
    const char* names[3] = {"A", "B", "C"};
    bool all = true;
    for (int k = 0; k < 3; ++k) {
        std::cout << "Implementation " << names[k] << ": " << (err[k].empty() ? "PASS" : "FAIL (" + err[k] + ")") << "\n";
        all = all && err[k].empty();
    }
    std::cout << (all ? "All implementations produce identical results.\n" : "MISMATCH DETECTED.\n");
    return all ? 0 : 1;
}


int main(int argc, char** argv) {
    size_t ops = 1000000;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--ops" && i + 1 < argc) ops = (size_t)std::atoll(argv[++i]);
    return run_verify(ops, 42);
}