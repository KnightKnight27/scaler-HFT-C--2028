#include "../common/OrderbookTypes.h"
#include "../include/OrderbookTwo.h"

#include <cstdint>
#include <vector>


OrderbookTwo::OrderbookTwo(){
    arr = std::vector<uint16_t>(10001); // range on buy and sell prices
    id_map.reserve(1e6); // max number of operations

}

void OrderbookTwo::add_order(uint32_t id, Side side, uint16_t price, uint8_t qty){
    id_map.insert({id,{price,qty}});
    size_t arr_idx = price-5000;
    if(arr[arr_idx]==0){
        if(side==Side::Buy){
            bid_heap.push(price);
        }else{
            ask_heap.push(price);
        }
    }
    arr[arr_idx]+=qty;
    
};

bool OrderbookTwo::cancel_order(uint32_t id){
    auto it = id_map.find(id);
    if(it!=id_map.end()){
        int price = it->second.price;
        int qty = it->second.qty;
        size_t arr_idx = price-5000;
        arr[arr_idx]-=qty;
        id_map.erase(it);
        return true;
    }
    return false;
};

BBO OrderbookTwo::get_bbo(){

    while(bid_heap.size()>0 && arr[bid_heap.top()-5000]==0){
        bid_heap.pop();
    }
    while(ask_heap.size()>0 && arr[ask_heap.top()-5000]==0){
        ask_heap.pop();
    }
    bool has_bid = bid_heap.size()>0;
    bool has_ask = ask_heap.size()>0;
    BBO temp = {
        has_bid,
        (has_bid)?(bid_heap.top()):(static_cast<uint16_t>(0)),
        (has_bid)?(arr[bid_heap.top()-5000]):(static_cast<uint16_t>(0)),
        has_ask,
        (has_ask)?(ask_heap.top()):(static_cast<uint16_t>(0)),
        (has_ask)?(arr[ask_heap.top()-5000]):(static_cast<uint16_t>(0)),

    };
    return temp;
};

std::vector<Level> OrderbookTwo::get_bids(int depth) {
    std::vector<Level>temp;
    temp.reserve(depth);
    int count = 0;
    while(bid_heap.size()>0 && count<depth){
        uint16_t x = bid_heap.top();
        if(arr[x-5000]>0){
            count++;
            temp.push_back({x, arr[x-5000]});
        }
        bid_heap.pop();
    }

    for(auto[p,q]:temp){
        bid_heap.push(p);
    }
    return temp;

};
std::vector<Level> OrderbookTwo::get_asks(int depth) {
    std::vector<Level>temp;
    temp.reserve(depth);
    int count = 0;
    while(ask_heap.size()>0 && count<depth){
        uint16_t x = ask_heap.top();
        if(arr[x-5000]>0){
            count++;
            temp.push_back({x, arr[x-5000]});
        }
        ask_heap.pop();
    }

    for(auto[p,q]:temp){
        ask_heap.push(p);
    }
    return temp;
};