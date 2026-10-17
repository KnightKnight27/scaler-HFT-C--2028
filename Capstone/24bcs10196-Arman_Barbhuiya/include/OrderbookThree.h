#pragma once

#include "../common/OrderbookTypes.h"
#include "./Orderbook.h"

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <memory_resource>

using u64 = std::uint64_t;
using u32 = std::uint32_t;
using u16 = std::uint16_t;
using u8 = std::uint8_t;

struct Umap_obj{
    u16 price;
    u8 qty;
    Side side;
  
};

class OrderbookThree : public IOrderbook{
    private:
    std::vector<u64>bids;
    std::vector<u64>asks;
    std::vector<u32>quantities;

    std::pmr::monotonic_buffer_resource pool;
    std::pmr::unordered_map<u32, Umap_obj>id_map{&pool};
    BBO curr_best_bbo;
    bool valid_best;
    u8 last_set_bit(u64 qword);
    u8 first_set_bit(u64 qword);

    public:
    OrderbookThree();
    void add_order(uint32_t id, Side side, uint16_t price, uint8_t qty) override;
    bool cancel_order(uint32_t id) override;
    BBO get_bbo() override;
    std::vector<Level> get_bids(int depth = 10)  override;
    std::vector<Level> get_asks(int depth = 10)  override;
};