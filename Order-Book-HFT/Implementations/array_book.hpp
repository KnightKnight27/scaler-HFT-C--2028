#pragma once

#include "types.hpp"
#include <algorithm>
#include <array>
#include <limits>

namespace ob {
class OrderTable {
    enum class State : std::uint8_t { Empty, Live, Deleted };
    struct Slot {
        OrderId id = 0;
        Order order{};
        State state = State::Empty;
    };
    std::vector<Slot> slots_;
    std::size_t live_ = 0;
    std::size_t used_ = 0;

    std::size_t bucket(OrderId id) const {
        const auto hash = (id ^ (id >> 32)) * UINT64_C(11400714819323198485);
        return static_cast<std::size_t>(hash) & (slots_.size() - 1);
    }

    void insert_unchecked(OrderId id, Order order) {
        auto i = bucket(id);
        while (slots_[i].state == State::Live) i = (i + 1) & (slots_.size() - 1);
        if (slots_[i].state == State::Empty) ++used_;
        slots_[i] = {id, order, State::Live};
        ++live_;
    }

    void rehash(std::size_t capacity) {
        std::vector<Slot> old;
        old.swap(slots_);
        slots_.resize(capacity);
        live_ = used_ = 0;
        for (const auto& slot : old)
            if (slot.state == State::Live) insert_unchecked(slot.id, slot.order);
    }

public:
    explicit OrderTable(std::size_t expected = 0) {
        std::size_t capacity = 16;
        while (capacity / 10 * 7 < expected) {
            if (capacity > std::numeric_limits<std::size_t>::max() / 2)
                throw std::length_error("order capacity too large");
            capacity *= 2;
        }
        slots_.resize(capacity);
    }

    const Order* find(OrderId id) const {
        auto i = bucket(id);
        while (slots_[i].state != State::Empty) {
            if (slots_[i].state == State::Live && slots_[i].id == id)
                return &slots_[i].order;
            i = (i + 1) & (slots_.size() - 1);
        }
        return nullptr;
    }

    void insert(OrderId id, Order order) {
        if (find(id)) throw std::invalid_argument("duplicate live order ID");
        if (used_ + 1 > slots_.size() / 10 * 7) {
            auto capacity = slots_.size();
            if (live_ + 1 > capacity / 2) {
                if (capacity > std::numeric_limits<std::size_t>::max() / 2)
                    throw std::length_error("order capacity too large");
                capacity *= 2;
            }
            rehash(capacity);
        }
        insert_unchecked(id, order);
    }

    bool erase(OrderId id, Order& order) {
        auto i = bucket(id);
        while (slots_[i].state != State::Empty) {
            if (slots_[i].state == State::Live && slots_[i].id == id) {
                order = slots_[i].order;
                slots_[i].state = State::Deleted;
                --live_;
                return true;
            }
            i = (i + 1) & (slots_.size() - 1);
        }
        return false;
    }
};

class ArrayBook {
    OrderTable orders_;
    std::array<Quantity, price_count> bids_{};
    std::array<Quantity, price_count> asks_{};
    BBO best_;

public:
    explicit ArrayBook(std::size_t capacity = 0) : orders_(capacity) {}

    void add_order(OrderId id, Side side, Price price, Quantity qty) {
        validate(side, price, qty);
        orders_.insert(id, {side, price, qty});
        if (side == Side::Buy) {
            bids_[price - 5000] += qty;
            if (!best_.has_bid || price >= best_.bid_price) {
                best_.has_bid = true;
                best_.bid_price = price;
                best_.bid_qty = bids_[price - 5000];
            }
        } else {
            asks_[price - 10001] += qty;
            if (!best_.has_ask || price <= best_.ask_price) {
                best_.has_ask = true;
                best_.ask_price = price;
                best_.ask_qty = asks_[price - 10001];
            }
        }
    }

    bool cancel_order(OrderId id) {
        Order order{};
        if (!orders_.erase(id, order)) return false;
        if (order.side == Side::Buy) {
            int i = order.price - 5000;
            bids_[i] -= order.qty;
            if (order.price == best_.bid_price) {
                while (i >= 0 && bids_[i] == 0) --i;
                best_.has_bid = i >= 0;
                best_.bid_price = i >= 0 ? i + 5000 : 0;
                best_.bid_qty = i >= 0 ? bids_[i] : 0;
            }
        } else {
            int i = order.price - 10001;
            asks_[i] -= order.qty;
            if (order.price == best_.ask_price) {
                while (i < price_count && asks_[i] == 0) ++i;
                best_.has_ask = i < price_count;
                best_.ask_price = i < price_count ? i + 10001 : 0;
                best_.ask_qty = i < price_count ? asks_[i] : 0;
            }
        }
        return true;
    }

    BBO get_bbo() const { return best_; }

    std::vector<Level> get_bids(int count = 10) const {
        std::vector<Level> result;
        if (count <= 0 || !best_.has_bid) return result;
        result.reserve(std::min(count, price_count));
        for (int i = best_.bid_price - 5000; i >= 0 && count > 0; --i)
            if (bids_[i] != 0) { result.push_back({i + 5000, bids_[i]}); --count; }
        return result;
    }

    std::vector<Level> get_asks(int count = 10) const {
        std::vector<Level> result;
        if (count <= 0 || !best_.has_ask) return result;
        result.reserve(std::min(count, price_count));
        for (int i = best_.ask_price - 10001; i < price_count && count > 0; ++i)
            if (asks_[i] != 0) { result.push_back({i + 10001, asks_[i]}); --count; }
        return result;
    }
};
}
