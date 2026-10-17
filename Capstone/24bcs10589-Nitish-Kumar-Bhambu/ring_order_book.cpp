#include "ring_order_book.hpp"

#include <limits>
#include <stdexcept>

RingOrderBook::RingOrderBook(std::size_t capacity)
    : slots(capacity), freeSlots(capacity), freeCount(capacity) {
    if (capacity == 0) {
        throw std::invalid_argument("Capacity must be positive");
    }
    for (std::size_t i = 0; i < capacity; i++) {
        freeSlots[i] = i;
    }
}

void RingOrderBook::add_order(unsigned long long id, Side side, int price, long quantity) {
    if (side != Side::Buy && side != Side::Sell) {
        throw std::invalid_argument("Side must be Buy or Sell");
    }
    if (quantity <= 0) {
        throw std::invalid_argument("Quantity must be positive");
    }
    if (side == Side::Buy && (price < 5000 || price > 9999)) {
        throw std::invalid_argument("Buy price must be between 5000 and 9999 ticks");
    }
    if (side == Side::Sell && (price < 10001 || price > 15000)) {
        throw std::invalid_argument("Sell price must be between 10001 and 15000 ticks");
    }
    if (orders.find(id) != orders.end()) {
        throw std::invalid_argument("This order ID is already live");
    }

    int index;
    long previousQuantity;
    if (side == Side::Buy) {
        index = 9999 - price;
        previousQuantity = bids[index];
    } else {
        index = price - 10001;
        previousQuantity = asks[index];
    }
    if (quantity > std::numeric_limits<long>::max() - previousQuantity) {
        throw std::overflow_error("Total quantity would be too large");
    }
    if (freeCount == 0) {
        throw std::length_error("No free order slots");
    }
    std::size_t slot = freeSlots[head];
    orders.emplace(id, slot);
    Order next{};
    next.id = id;
    next.side = side;
    next.price = price;
    next.quantity = quantity;
    slots[slot] = next;
    head++;
    if (head == freeSlots.size()) {
        head = 0;
    }
    freeCount--;

    if (side == Side::Buy) {
        bids[index] += quantity;
        if (index < bestBid) {
            bestBid = index;
        }
    } else {
        asks[index] += quantity;
        if (index < bestAsk) {
            bestAsk = index;
        }
    }
}

bool RingOrderBook::cancel_order(unsigned long long id) {
    auto found = orders.find(id);
    if (found == orders.end()) {
        return false;
    }
    std::size_t slot = found->second;
    const Order& current = slots[slot];
    if (current.side == Side::Buy) {
        int index = 9999 - current.price;
        bids[index] -= current.quantity;
        while (bestBid < 5000 && bids[bestBid] == 0) {
            bestBid++;
        }
    } else {
        int index = current.price - 10001;
        asks[index] -= current.quantity;
        while (bestAsk < 5000 && asks[bestAsk] == 0) {
            bestAsk++;
        }
    }
    orders.erase(found);
    slots[slot].quantity = 0;
    freeSlots[tail] = slot;
    tail++;
    if (tail == freeSlots.size()) {
        tail = 0;
    }
    freeCount++;
    return true;
}

BBO RingOrderBook::get_bbo() const {
    BBO result{};
    if (bestBid < 5000) {
        result.has_bid = true;
        result.bid_price = 9999 - bestBid;
        result.bid_qty = bids[bestBid];
    }
    if (bestAsk < 5000) {
        result.has_ask = true;
        result.ask_price = 10001 + bestAsk;
        result.ask_qty = asks[bestAsk];
    }
    return result;
}

std::vector<Level> RingOrderBook::get_bids(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) {
        return result;
    }
    for (int i = bestBid; i < 5000; i++) {
        if (bids[i] > 0) {
            result.push_back(Level{9999 - i, bids[i]});
            if (result.size() == static_cast<std::size_t>(depth)) {
                break;
            }
        }
    }
    return result;
}

std::vector<Level> RingOrderBook::get_asks(int depth) const {
    std::vector<Level> result;
    if (depth <= 0) {
        return result;
    }
    for (int i = bestAsk; i < 5000; i++) {
        if (asks[i] > 0) {
            result.push_back(Level{10001 + i, asks[i]});
            if (result.size() == static_cast<std::size_t>(depth)) {
                break;
            }
        }
    }
    return result;
}
