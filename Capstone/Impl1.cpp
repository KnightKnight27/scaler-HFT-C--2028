// Unsorted Map -> hashtable 
// tc-> best/average = O(1), worst case = O(N)
// OrderBook -> Maps ( Bids, Asks, Orders)
// for each map -> price as distinct keys since
// bids: range is [50.00,99.99]
// bids: No. of price levels would be 5000
// asks: range is [100.01,150.00]
// asks: No. of price levels = 5000
// single threaded design with no order matching / no trade generation

#include <cstdint>
#include <vector>
#include <unordered_map>
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
    std::unordered_map<int,long> bids;
    std::unordered_map<int,long> asks;
    std::unordered_map<std::uint64_t,Order> orders;

    void add_order(uint64_t id,Side side, int price, long qty){
      // create an instance not heap allocation like using with new in java ...etc
      // for that price level add it or upsert it as i would say in the given side map
      
      // using move semantics optimizing memory at low level , avoiding unnecessary copying
      auto result = orders.emplace(id, Order{id,side,price,qty} ); // figured out that emplace is the best way to construct directly in map's memory -> zero copies , zero moves
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
      // if order haas already been cancelled
      auto it = orders.find(id);
      if(it == orders.end())return false;
      
      // over here simply remove the element from the orders map and also reduce its qty contribution from the price level aggregate qty
      Order cancelled_order = it->second; // honestly this is the syntax i got to know while i was coding, so dont think the whole code was generated
      
      if(cancelled_order.side == Side::Buy){
        // bids map
        auto level = bids.find(cancelled_order.price);
        if(level != bids.end()){
          level->second -= cancelled_order.qty;
          if(level->second == 0)bids.erase(level);
        }
        
      }else{
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
        auto best_bid = bids.begin();
        for(auto it = bids.begin(); it!= bids.end(); it++){
          if(it->first > best_bid->first)best_bid = it;
        }
        bbo.has_bid = true;
        bbo.bid_price = best_bid->first;
        bbo.bid_qty = best_bid->second;
      }

      if(!asks.empty()){
        auto best_ask = asks.begin();
        for(auto it = asks.begin(); it!=asks.end(); it++){
          if(it->first < best_ask->first)best_ask = it;
        }
        bbo.has_ask = true;
        bbo.ask_price = best_ask->first;
        bbo.ask_qty = best_ask->second;
      }
      return bbo;
      
    }
    std::vector<Level> get_bids(int depth)const{
      // if depth exceeds the current size of the orderbook / bids & ask return only existing levels
      std::vector<Level> bids_upto_depth;
      if(depth <= 0)return bids_upto_depth;
      bids_upto_depth.reserve(std::min(depth, (int)bids.size()));

      for(const auto& [price,qty] : bids){
         bids_upto_depth.push_back(Level{price,qty});
      }
      std::sort(bids_upto_depth.begin(), bids_upto_depth.end(),
          [](const Level& a, const Level& b) {
              return a.price > b.price;
       });
      if(bids_upto_depth.size() > static_cast<std::size_t>(depth))
         bids_upto_depth.resize(depth);
      return bids_upto_depth;   
    }
    
    std::vector<Level> get_asks(int depth)const{
      std::vector<Level> asks_upto_depth;
      if(depth <= 0)return asks_upto_depth;
      asks_upto_depth.reserve(std::min(depth, (int)asks.size()));

      for(const auto& [price,qty] : asks){
         asks_upto_depth.push_back(Level{price,qty});
      }
      std::sort(asks_upto_depth.begin(), asks_upto_depth.end(),
          [](const Level& a, const Level& b) {
              return a.price < b.price;
       });
      if(asks_upto_depth.size() > static_cast<std::size_t>(depth))
        asks_upto_depth.resize(depth);
      return asks_upto_depth;    
    }
};

int main(){
  // initialization / entry point
 return 0; 
}
