// Ordered Map -> Red-Black Tree (typically)
// tc-> search / insertion / deletion = O(log N)
// OrderBook -> Maps ( Bids, Asks, Orders)
// for each price map -> price as distinct keys since
// bids: range is [50.00,99.99]
// bids: No. of price levels would be 5000
// asks: range is [100.01,150.00]
// asks: No. of price levels = 5000
// single threaded design with no order matching / no trade generation

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <map>
#include <algorithm>

enum class Side{ Buy, Sell};
struct Level {
  int price; // avoiding extra work by not choosing double
  long qty;
};

struct BBO{
  bool has_bid;
  int bid_price;
  long bid_qty;

  bool has_ask;
  int ask_price;
  long ask_qty;
};

struct Order{
  std::uint64_t id;
  Side side;
  int price;
  long qty;
};

class OrderBook{
  public:
    std::map<int,long> bids;
    std::map<int,long> asks;
    std::unordered_map<std::uint64_t,Order> orders;

    void add_order(uint64_t id,Side side, int price, long qty){
      // create an instance not heap allocation like using with new in java ...etc
      // for that price level add it or upsert it as i would say in the given side map

      // using emplace to construct the order entry in the map
      auto result = orders.emplace(id, Order{id,side,price,qty});
      if(!result.second)return;

      if(side == Side::Buy){
        // bids map
        // update the aggregated quantity -> not storing the orders per distinct price levels as no trade/ order matching is needed over here
        bids[price] += qty;
      }else{
        // asks map
        asks[price] += qty;
      }
    }

    bool cancel_order(uint64_t id){
      // edge case:
      // if order has already been cancelled
      auto it = orders.find(id);
      if(it == orders.end())return false;

      // over here simply remove the element from the orders map and also reduce its qty contribution from the price level aggregate qty
      Order cancelled_order = it->second;

      if(cancelled_order.side == Side::Buy){
        // bids map
        auto level = bids.find(cancelled_order.price);
        if(level != bids.end()){
          level->second -= cancelled_order.qty;
          if(level->second == 0)bids.erase(level);
        }

      }else{
        // asks map
        auto level = asks.find(cancelled_order.price);
        if(level != asks.end()){
          level->second -= cancelled_order.qty;
          if(level->second == 0)asks.erase(level);
        }
      }

      orders.erase(it);
      return true;
    }

    BBO get_bbo()const{
      BBO bbo{false,0,0,false,0,0};

      if(!bids.empty()){
        // map keeps keys sorted, so the highest bid is at rbegin()
        auto best_bid = bids.rbegin();

        bbo.has_bid = true;
        bbo.bid_price = best_bid->first;
        bbo.bid_qty = best_bid->second;
      }

      if(!asks.empty()){
        // map keeps keys sorted, so the lowest ask is at begin()
        auto best_ask = asks.begin();

        bbo.has_ask = true;
        bbo.ask_price = best_ask->first;
        bbo.ask_qty = best_ask->second;
      }
      return bbo;
    }

    std::vector<Level> get_bids(int depth)const{
      // if depth exceeds the current size of the orderbook / bids & asks return only existing levels
      std::vector<Level> bids_upto_depth;
      if(depth <= 0)return bids_upto_depth;
      bids_upto_depth.reserve(std::min(depth, (int)bids.size()));

      // reverse iteration gives the highest bid prices first
      for(auto it = bids.rbegin(); it != bids.rend() && bids_upto_depth.size() < static_cast<std::size_t>(depth); ++it){
        bids_upto_depth.push_back(Level{it->first,it->second});
      }

      return bids_upto_depth;
    }

    std::vector<Level> get_asks(int depth)const{
      std::vector<Level> asks_upto_depth;
      if(depth <= 0)return asks_upto_depth;
      asks_upto_depth.reserve(std::min(depth, (int)asks.size()));

      // forward iteration gives the lowest ask prices first
      for(auto it = asks.begin(); it != asks.end() && asks_upto_depth.size() < static_cast<std::size_t>(depth); ++it){
        asks_upto_depth.push_back(Level{it->first,it->second});
      }

      return asks_upto_depth;
    }
};

int main(){
  // initialization / entry point
  return 0;
}
