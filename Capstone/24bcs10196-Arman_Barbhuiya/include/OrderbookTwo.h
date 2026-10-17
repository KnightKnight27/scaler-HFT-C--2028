#pragma once

#include "../common/OrderbookTypes.h"
#include "./Orderbook.h"

#include <cstdint>
#include <functional>
#include <vector>
#include <unordered_map>
#include <queue>
#include <memory_resource>

struct umap_obj{
    uint16_t price;
    uint8_t qty;
};


class OrderbookTwo : public IOrderbook{
    private:

    std::vector<uint16_t>arr;
    std::pmr::monotonic_buffer_resource pool;
    std::pmr::unordered_map<uint32_t,umap_obj>id_map{&pool};

    std::priority_queue<uint16_t, std::vector<uint16_t>> bid_heap;

    std::priority_queue<uint16_t,std::vector<uint16_t>,std::greater<uint16_t>>ask_heap;

    public:
    OrderbookTwo();
    void add_order(uint32_t id, Side side, uint16_t price, uint8_t qty) override;
    bool cancel_order(uint32_t id) override;
    BBO get_bbo() override;
    std::vector<Level> get_bids(int depth = 10)  override;
    std::vector<Level> get_asks(int depth = 10)  override;
};