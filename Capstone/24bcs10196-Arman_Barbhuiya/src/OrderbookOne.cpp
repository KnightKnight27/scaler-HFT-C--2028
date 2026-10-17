#include "../common/OrderbookTypes.h"
#include "../include/OrderbookOne.h"

#include <cstdint>
#include <vector>

//mostly basic stl should be self explanatory
OrderbookOne::OrderbookOne(){
    id_map.reserve(1e6); // max number of operations
}

void OrderbookOne::add_order(uint32_t id, Side side, uint16_t price, uint8_t qty){
    std::pmr::map<u16, u32>* arr[2] = {&bids, &asks}; // why pmr? explained in readme (hopefully)
    std::pmr::map<u16, u32>* curr = arr[(side!=Side::Buy)]; 
    
    auto it = curr->find(price);
    if(it==curr->end()){
        auto result = curr->insert({price,qty});
        id_map.insert({id,{side,result.first,qty}});
    }else{
        it->second+=qty;
        id_map.insert({id,{side,it,qty}});
    }


};

bool OrderbookOne::cancel_order(uint32_t id){
    auto id_map_it = id_map.find(id);

    if(id_map_it!=id_map.end()){

        auto mp_it = id_map_it->second.it;
        auto quantity = id_map_it->second.quantity;
        auto side = id_map_it->second.side;
        mp_it->second-=quantity;
        if(mp_it->second==0){
            if(side==Side::Buy){
                bids.erase(mp_it);
            }else{
                asks.erase(mp_it);
            }
        }

        id_map.erase(id_map_it);
        return true;
    }
    return false;
};

BBO OrderbookOne::get_bbo(){
    bool has_ask = asks.size()!=0;
    bool has_bid = bids.size()!=0;
    auto bid_it = prev(bids.end());
    auto asks_it = asks.begin();

    BBO temp = {
        has_bid,
        (has_bid)?(bid_it->first):(static_cast<u16>(0)),
        (has_bid)?(bid_it->second):(static_cast<u32>(0)),

        has_ask,
        (has_ask)?(asks_it->first):(static_cast<u16>(0)),
        (has_ask)?(asks_it->second):(static_cast<u8>(0)),

    };
    return temp;
};

std::vector<Level> OrderbookOne::get_bids(int depth) {
    int count = std::min(bids.size(),static_cast<size_t>(depth));
    std::vector<Level> temp(count);
    auto it = bids.rbegin();
    for(int i =0;i<count;i++){
        temp[i] = {it->first, it->second};
        it++;
    }

    return temp;
};

std::vector<Level> OrderbookOne::get_asks(int depth) {
    int count = std::min(asks.size(),static_cast<size_t>(depth));
    std::vector<Level> temp(count);
    auto it = asks.begin();
    for(int i =0;i<count;i++){
        temp[i] = {it->first, it->second};
        it++;
    }

    return temp;
};