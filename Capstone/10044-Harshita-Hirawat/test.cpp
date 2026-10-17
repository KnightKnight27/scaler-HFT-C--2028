// checks that A, B and C give the same results
// ./test
#include <map>
#include <unordered_map>
#include "workload.h"
#include "pq_book.h"
#include "vector_book.h"
#include "array_book.h"

// very simple book to compare with
class RefBook {
public:
  void add_order(uint64_t id, Side side, int64_t price, int64_t qty) {
    if (side == Side::Buy) mBids[-price] += qty; // -price so begin() is the highest bid
    else mAsks[price] += qty;
    mOrders[id] = {side, price, qty};
  }

  bool cancel_order(uint64_t id) {
    if (mOrders.count(id) == 0) return false;

    Order o = mOrders[id];
    mOrders.erase(id);

    if (o.side == Side::Buy) {
      mBids[-o.price] -= o.qty;
      if (mBids[-o.price] == 0) mBids.erase(-o.price);
    } else {
      mAsks[o.price] -= o.qty;
      if (mAsks[o.price] == 0) mAsks.erase(o.price);
    }

    return true;
  }

  BBO get_bbo() const {
    BBO b{};

    if (!mBids.empty()) {
      b.has_bid = true;
      b.bid_price = -mBids.begin()->first;
      b.bid_qty = mBids.begin()->second;
    }

    if (!mAsks.empty()) {
      b.has_ask = true;
      b.ask_price = mAsks.begin()->first;
      b.ask_qty = mAsks.begin()->second;
    }

    return b;
  }

  std::vector<Level> get_bids(int depth = 10) const {
    std::vector<Level> out;
    for (auto it = mBids.begin(); it != mBids.end() && (int)out.size() < depth; it++)
      out.push_back({-it->first, it->second});
    return out;
  }

  std::vector<Level> get_asks(int depth = 10) const {
    std::vector<Level> out;
    for (auto it = mAsks.begin(); it != mAsks.end() && (int)out.size() < depth; it++)
      out.push_back({it->first, it->second});
    return out;
  }

private:
  struct Order {
    Side side;
    int64_t price;
    int64_t qty;
  };

  std::map<int64_t, int64_t> mBids;
  std::map<int64_t, int64_t> mAsks;
  std::unordered_map<uint64_t, Order> mOrders;
};

bool sameBbo(BBO a, BBO b) {
  if (a.has_bid != b.has_bid || a.has_ask != b.has_ask) return false;
  if (a.has_bid && (a.bid_price != b.bid_price || a.bid_qty != b.bid_qty)) return false;
  if (a.has_ask && (a.ask_price != b.ask_price || a.ask_qty != b.ask_qty)) return false;
  return true;
}

bool sameLevels(const std::vector<Level>& a, const std::vector<Level>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++)
    if (a[i].price != b[i].price || a[i].qty != b[i].qty) return false;
  return true;
}

// small cases
template <typename Book>
bool edgeCases() {
  Book b;
  if (b.get_bbo().has_bid || b.get_bbo().has_ask) return false; // empty book
  if (b.cancel_order(12345)) return false;                        // unknown id
  if (!b.get_bids(10).empty()) return false;

  b.add_order(1, Side::Buy, 9990, 10);
  b.add_order(2, Side::Buy, 9990, 5); // same price, qty adds up
  b.add_order(3, Side::Buy, 9980, 7);
  b.add_order(4, Side::Buy, 9970, 1);
  b.add_order(5, Side::Sell, 10010, 20);
  std::vector<Level> bids = b.get_bids(10); // only 3 levels exist

  if (bids.size() != 3 || bids[0].price != 9990 || bids[0].qty != 15) return false;
  if (b.get_asks(10).size() != 1) return false;

  if (!b.cancel_order(1)) return false;
  if (b.cancel_order(1)) return false; // already cancelled
  if (b.get_bbo().bid_qty != 5) return false;
  if (!b.cancel_order(2)) return false; // best level is empty now
  if (b.get_bbo().bid_price != 9980) return false;
  if (!b.cancel_order(5)) return false;
  if (b.get_bbo().has_ask) return false;

  return true;
}

// a price level that became empty and then gets orders again
// asks in the right order, empty and refill, and the band edges
template <typename Book>
bool reuseCases() {
  Book b;
  b.add_order(1, Side::Buy, 9990, 5);
  b.add_order(2, Side::Buy, 9980, 7);
  b.cancel_order(2);                  // 9980 empty, not the best level
  b.add_order(3, Side::Buy, 9980, 4); // reuse it
  std::vector<Level> bids = b.get_bids(10);

  if (bids.size() != 2 || bids[1].price != 9980 || bids[1].qty != 4) return false;
  b.cancel_order(1);                  // best bid empty

  if (b.get_bbo().bid_price != 9980 || b.get_bbo().bid_qty != 4) return false;
  b.add_order(4, Side::Buy, 9990, 9); // reuse the best level

  if (b.get_bbo().bid_price != 9990 || b.get_bbo().bid_qty != 9) return false;

  b.add_order(10, Side::Sell, 10050, 1);
  b.add_order(11, Side::Sell, 10020, 2);
  b.add_order(12, Side::Sell, 10030, 3);
  std::vector<Level> asks = b.get_asks(10); // lowest ask first

  if (asks.size() != 3 || asks[0].price != 10020 || asks[1].price != 10030 || asks[2].price != 10050) return false;

  b.cancel_order(12);                  // middle ask empty
  b.add_order(13, Side::Sell, 10030, 6);
  b.cancel_order(11);                  // best ask empty

  if (b.get_bbo().ask_price != 10030 || b.get_bbo().ask_qty != 6) return false;
  b.add_order(14, Side::Sell, 10020, 8);

  if (b.get_bbo().ask_price != 10020 || b.get_bbo().ask_qty != 8) return false;

  b.cancel_order(3);
  b.cancel_order(4);
  b.cancel_order(10);
  b.cancel_order(13);
  b.cancel_order(14);

  if (b.get_bbo().has_bid || b.get_bbo().has_ask) return false; // empty again
  if (!b.get_bids(10).empty() || !b.get_asks(10).empty()) return false;

  b.add_order(20, Side::Buy, MIN_BID, 1);
  b.add_order(21, Side::Sell, MAX_ASK, 1);
  b.add_order(22, Side::Buy, MAX_BID, 1);
  b.add_order(23, Side::Sell, MIN_ASK, 1);

  if (b.get_bbo().bid_price != MAX_BID || b.get_bbo().ask_price != MIN_ASK) return false;

  b.cancel_order(22);
  b.cancel_order(23);

  if (b.get_bbo().bid_price != MIN_BID || b.get_bbo().ask_price != MAX_ASK) return false;

  return true;
}

// where a new price lands: empty side, better than all, worse than all
// in between, and same price as an existing level
template <typename Book>
bool searchCases() {
  Book b;
  b.add_order(1, Side::Buy, 9000, 1);  // empty side
  b.add_order(2, Side::Buy, 9500, 2);  // better than all
  b.add_order(3, Side::Buy, 8500, 3);  // worse than all
  b.add_order(4, Side::Buy, 9200, 4);  // in between
  b.add_order(5, Side::Buy, 9200, 5);  // same price
  std::vector<Level> bids = b.get_bids(10);

  if (bids.size() != 4) return false;
  if (bids[0].price != 9500 || bids[1].price != 9200 || bids[2].price != 9000 || bids[3].price != 8500) return false;
  if (bids[1].qty != 9) return false;

  b.add_order(11, Side::Sell, 11000, 1);
  b.add_order(12, Side::Sell, 10500, 2);
  b.add_order(13, Side::Sell, 12000, 3);
  b.add_order(14, Side::Sell, 11500, 4);
  b.add_order(15, Side::Sell, 11500, 5);
  std::vector<Level> asks = b.get_asks(10);

  if (asks.size() != 4) return false;
  if (asks[0].price != 10500 || asks[1].price != 11000 || asks[2].price != 11500 || asks[3].price != 12000) return false;
  if (asks[2].qty != 9) return false;

  // cancel the first and the last level of each side
  b.cancel_order(2);
  b.cancel_order(3);
  b.cancel_order(12);
  b.cancel_order(13);
  bids = b.get_bids(10);
  asks = b.get_asks(10);

  if (bids.size() != 2 || bids[0].price != 9200 || bids[1].price != 9000) return false;
  if (asks.size() != 2 || asks[0].price != 11000 || asks[1].price != 11500) return false;

  return true;
}

// run the workload on the book and on RefBook, compare after every op
template <typename Book>
bool check(const std::vector<Op>& ops) {
  if (!edgeCases<Book>()) {
    std::cout << "  edge cases failed\n";
    return false;
  }

  if (!reuseCases<Book>()) {
    std::cout << "  reuse cases failed\n";
    return false;
  }

  if (!searchCases<Book>()) {
    std::cout << "  search cases failed\n";
    return false;
  }

  Book book;
  RefBook ref;
  for (size_t i = 0; i < ops.size(); i++) {
    const Op& op = ops[i];

    if (op.type == ADD) {
      book.add_order(op.id, op.side, op.price, op.qty);
      ref.add_order(op.id, op.side, op.price, op.qty);
    } else if (op.type == CANCEL) {
      if (book.cancel_order(op.id) != ref.cancel_order(op.id)) {
        std::cout << "  cancel different at op " << i << "\n";
        return false;
      }
      if (book.cancel_order(op.id)) return false; // 2nd cancel must be false
    }

    int depth = 10;
    if (i % 10000 == 0) depth = 100000; // sometimes check the whole book
    if (!sameBbo(book.get_bbo(), ref.get_bbo()) ||
        !sameLevels(book.get_bids(depth), ref.get_bids(depth)) ||
        !sameLevels(book.get_asks(depth), ref.get_asks(depth))) {
      std::cout << "  mismatch at op " << i << "\n";
      return false;
    }
  }
  
  return true;
}

int main() {
  std::cout << "Running correctness test...\n";
  std::vector<Op> ops = makeWorkload(NUM_OPS);
  bool a = check<PQBook>(ops);
  std::cout << "Implementation A: " << (a ? "PASS" : "FAIL") << "\n";
  bool b = check<VectorBook>(ops);
  std::cout << "Implementation B: " << (b ? "PASS" : "FAIL") << "\n";
  bool c = check<ArrayBook>(ops);
  std::cout << "Implementation C: " << (c ? "PASS" : "FAIL") << "\n";

  if (a && b && c) {
    std::cout << "All implementations produce identical results.\n";
    return 0;
  }
  std::cout << "Implementations DO NOT agree.\n";
  return 1;
}
