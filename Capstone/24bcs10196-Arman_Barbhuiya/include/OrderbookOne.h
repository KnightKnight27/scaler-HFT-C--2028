#pragma once

#include "../common/OrderbookTypes.h"
#include "./Orderbook.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <memory_resource>

using u32 = std::uint32_t;
using u16 = std::uint16_t;
using u8 = std::uint8_t;

struct id_map_val{
    Side side;
    std::pmr::map<u16, u32>::iterator it;
    u8 quantity;
};

class OrderbookOne : public IOrderbook{
    private:

    std::pmr::monotonic_buffer_resource pool;
    
    std::pmr::map<u16, u32> bids{&pool};
    std::pmr::map<u16, u32> asks{&pool};
    std::pmr::unordered_map<u32, id_map_val> id_map{&pool};
    

    public:
    OrderbookOne();
    void add_order(uint32_t id, Side side, uint16_t price, uint8_t qty) override;
    bool cancel_order(uint32_t id) override;
    BBO get_bbo() override;
    std::vector<Level> get_bids(int depth = 10)  override;
    std::vector<Level> get_asks(int depth = 10)  override;
};