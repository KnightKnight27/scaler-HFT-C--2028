#pragma once

#include "order_book_types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

class ArrayOrderBook {
public:
    ArrayOrderBook();

    void add_order(std::uint64_t id, Side side, double price, long qty);
    bool cancel_order(std::uint64_t id);
    BBO get_bbo() const;
    std::vector<Level> get_bids(int depth = 10) const;
    std::vector<Level> get_asks(int depth = 10) const;

private:
    struct Order {
        std::uint64_t id{};
        int price{};
        long qty{};
        Side side{Side::Buy};
        std::uint8_t state{}; // 0 = empty, 1 = occupied, 2 = deleted
    };

    class OrderTable {
    public:
        OrderTable();
        bool insert(std::uint64_t id, Side side, int price, long qty);
        Order* find(std::uint64_t id);
        bool erase(std::uint64_t id);

    private:
        std::vector<Order> slots_;
        std::size_t size_{};
        std::size_t deleted_{};

        static std::uint64_t hash(std::uint64_t value) noexcept;
        void rehash(std::size_t capacity);
    };

    static int level_index(Side side, int price) noexcept;
    static int index_price(int index) noexcept;
    void repair_best_bid();
    void repair_best_ask();

    std::vector<long> levels_;
    OrderTable orders_;
    int best_bid_{-1};
    int best_ask_{-1};
};
