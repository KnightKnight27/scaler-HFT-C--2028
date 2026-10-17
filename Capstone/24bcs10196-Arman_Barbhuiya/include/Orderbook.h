#pragma once

#include"../common/OrderbookTypes.h"

#include <cstdint>
#include <vector>



class IOrderbook {
    public:
    virtual void add_order(uint32_t id, Side side, uint16_t price, uint8_t qty) = 0;
    virtual bool cancel_order(uint32_t id) = 0;
    virtual BBO get_bbo()  = 0;
    virtual std::vector<Level> get_bids(int depth = 10)  =0;
    virtual std::vector<Level> get_asks(int depth = 10)  =0;
};