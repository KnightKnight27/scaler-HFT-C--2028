
 // Direct-Indexed Arrays + Occupancy Bitmap
 // tc-> add/cancel = average O(1), finding best price = O(1) in fixed-size bounds
 // OrderBook -> Arrays (Bids, Asks), Bitmaps (Bid, Ask), Orders (Hash Table)
 // bids: range is [50.00,99.99] -> ticks [5000,9999] -> 5000 price levels
 // asks: range is [100.01,150.00] -> ticks [10001,15000] -> 5000 price levels
 // single threaded design with no order matching / no trade generation

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <array>
#include <algorithm>

enum class Side{ Buy, Sell};

struct Level {
  int price; // price stored as integer ticks
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
  private:
    static constexpr int LEVELS = 5000;
    static constexpr int WORDS = (LEVELS + 63) / 64;

    // each index represents one possible price level
    std::array<long, LEVELS> bids{};
    std::array<long, LEVELS> asks{};

    // each bit represents whether a price level is active
    // 1 -> active price level, 0 -> no active orders at that price
    std::array<std::uint64_t, WORDS> bid_bitmap{};
    std::array<std::uint64_t, WORDS> ask_bitmap{};

    // order ID lookup remains a hash table for cancellation
    std::unordered_map<std::uint64_t,Order> orders;

    void set_bit(std::array<std::uint64_t, WORDS>& bitmap, int index){
      bitmap[index / 64] |= (std::uint64_t{1} << (index % 64));
    }

    void clear_bit(std::array<std::uint64_t, WORDS>& bitmap, int index){
      bitmap[index / 64] &= ~(std::uint64_t{1} << (index % 64));
    }

  public:
    void add_order(std::uint64_t id,Side side, int price, long qty){
      // convert the price tick into its direct array index
      int index;

      if(side == Side::Buy){
        if(price < 5000 || price > 9999 || qty <= 0)return;
        index = price - 5000;
      }else{
        if(price < 10001 || price > 15000 || qty <= 0)return;
        index = price - 10001;
      }

      // create the order entry; duplicate IDs are not accepted
      auto result = orders.emplace(id, Order{id,side,price,qty});
      if(!result.second)return;

      if(side == Side::Buy){
        // bids array
        // set the bit only when activating a previously empty price level
        if(bids[index] == 0)set_bit(bid_bitmap, index);
        bids[index] += qty;
      }else{
        // asks array
        if(asks[index] == 0)set_bit(ask_bitmap, index);
        asks[index] += qty;
      }
    }

    bool cancel_order(std::uint64_t id){
      // edge case:
      // if order has already been cancelled or never existed
      auto it = orders.find(id);
      if(it == orders.end())return false;

      // save the order details before removing its entry
      Order cancelled_order = it->second;

      if(cancelled_order.side == Side::Buy){
        // bids array
        int index = cancelled_order.price - 5000;
        bids[index] -= cancelled_order.qty;

        // if no quantity remains, deactivate this price level
        if(bids[index] == 0)clear_bit(bid_bitmap, index);

      }else{
        // asks array
        int index = cancelled_order.price - 10001;
        asks[index] -= cancelled_order.qty;

        if(asks[index] == 0)clear_bit(ask_bitmap, index);
      }

      orders.erase(it);
      return true;
    }

    BBO get_bbo()const{
      BBO bbo{false,0,0,false,0,0};

      // find the highest active bid using the bitmap
      for(int word = WORDS - 1; word >= 0; --word){
        if(bid_bitmap[word] == 0)continue;

        for(int bit = 63; bit >= 0; --bit){
          if(bid_bitmap[word] & (std::uint64_t{1} << bit)){
            int index = word * 64 + bit;

            if(index < LEVELS){
              bbo.has_bid = true;
              bbo.bid_price = index + 5000;
              bbo.bid_qty = bids[index];
              break;
            }
          }
        }

        if(bbo.has_bid)break;
      }

      // find the lowest active ask using the bitmap
      for(int word = 0; word < WORDS; ++word){
        if(ask_bitmap[word] == 0)continue;

        for(int bit = 0; bit < 64; ++bit){
          if(ask_bitmap[word] & (std::uint64_t{1} << bit)){
            int index = word * 64 + bit;

            if(index < LEVELS){
              bbo.has_ask = true;
              bbo.ask_price = index + 10001;
              bbo.ask_qty = asks[index];
              break;
            }
          }
        }

        if(bbo.has_ask)break;
      }

      return bbo;
    }

    std::vector<Level> get_bids(int depth)const{
      // return only existing levels, highest bid first
      std::vector<Level> bids_upto_depth;
      if(depth <= 0)return bids_upto_depth;

      bids_upto_depth.reserve(std::min(depth, LEVELS));

      for(int index = LEVELS - 1; index >= 0 && bids_upto_depth.size() < static_cast<std::size_t>(depth); --index){
        if(bids[index] > 0){
          bids_upto_depth.push_back(Level{index + 5000, bids[index]});
        }
      }

      return bids_upto_depth;
    }

    std::vector<Level> get_asks(int depth)const{
      // return only existing levels, lowest ask first
      std::vector<Level> asks_upto_depth;
      if(depth <= 0)return asks_upto_depth;

      asks_upto_depth.reserve(std::min(depth, LEVELS));

      for(int index = 0; index < LEVELS && asks_upto_depth.size() < static_cast<std::size_t>(depth); ++index){
        if(asks[index] > 0){
          asks_upto_depth.push_back(Level{index + 10001, asks[index]});
        }
      }

      return asks_upto_depth;
    }
};

int main(){
  // initialization / entry point
  return 0;
}

