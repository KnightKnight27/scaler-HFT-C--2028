// Small demo, pick the implementation on the command line (default A).
//
//   ./demo A|B|C

#include <iostream>
#include <string>
#include <cctype>

#include "order.h"
#include "impl_a/orderbook_a.h"
#include "impl_b/orderbook_b.h"
#include "impl_c/orderbook_c.h"

using namespace std;

void print_bbo(OrderBook& book, const string& tag) {
    BBO b = book.get_bbo();
    cout << tag << " BBO: ";
    if (b.has_bid) {
        cout << "bid " << b.bid_price << " x " << b.bid_qty;
    } else {
        cout << "bid none";
    }
    cout << "  |  ";
    if (b.has_ask) {
        cout << "ask " << b.ask_price << " x " << b.ask_qty;
    } else {
        cout << "ask none";
    }
    cout << endl;
}

int main(int argc, char** argv) {
    char which = 'A';
    if (argc > 1) {
        which = (char)toupper(argv[1][0]);
    }

    OrderBook* book = NULL;
    if (which == 'A') book = new OrderBookA();
    else if (which == 'B') book = new OrderBookB();
    else if (which == 'C') book = new OrderBookC();
    else {
        cout << "pick A, B or C" << endl;
        return 1;
    }

    cout << "=== demo, implementation " << which << " ===" << endl;

    book->add_order(1, Side::Buy, 99.50, 100);
    book->add_order(2, Side::Buy, 99.50, 50);
    book->add_order(3, Side::Buy, 99.00, 200);
    book->add_order(4, Side::Sell, 100.50, 150);
    book->add_order(5, Side::Sell, 101.00, 300);

    print_bbo(*book, "after adds:");

    cout << "top bids:" << endl;
    vector<Level> bids = book->get_bids(3);
    for (size_t i = 0; i < bids.size(); ++i) {
        cout << "  " << bids[i].price << " : " << bids[i].qty << endl;
    }

    cout << "top asks:" << endl;
    vector<Level> asks = book->get_asks(3);
    for (size_t i = 0; i < asks.size(); ++i) {
        cout << "  " << asks[i].price << " : " << asks[i].qty << endl;
    }

    cout << "cancelling order 1 ..." << endl;
    book->cancel_order(1);
    print_bbo(*book, "after cancel:");

    cout << "cancelling an unknown order 999 -> "
         << (book->cancel_order(999) ? "true" : "false") << endl;

    delete book;
    return 0;
}
