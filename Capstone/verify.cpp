#include "order_book.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace capstone {
std::unique_ptr<OrderBook> createMapOrderBook();
std::unique_ptr<OrderBook> createHeapOrderBook();
std::unique_ptr<OrderBook> createArrayOrderBook();
}

using namespace capstone;

static bool sameDouble(double a, double b) {
    return std::abs(a - b) < 1e-9;
}

static bool sameBBO(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid &&
           a.has_ask == b.has_ask &&
           (!a.has_bid ||
            (sameDouble(a.bid_price, b.bid_price) &&
             a.bid_qty == b.bid_qty)) &&
           (!a.has_ask ||
            (sameDouble(a.ask_price, b.ask_price) &&
             a.ask_qty == b.ask_qty));
}

static bool sameLevels(
    const std::vector<Level>& a,
    const std::vector<Level>& b
) {
    if (a.size() != b.size()) return false;

    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!sameDouble(a[i].price, b[i].price) ||
            a[i].qty != b[i].qty) {
            return false;
        }
    }

    return true;
}

int main() {
    auto a = createMapOrderBook();
    auto b = createHeapOrderBook();
    auto c = createArrayOrderBook();

    std::vector<OrderBook*> books{a.get(), b.get(), c.get()};
    std::mt19937_64 rng(123456789);
    std::uniform_int_distribution<int> sideDist(0, 1);
    std::uniform_int_distribution<int> qtyDist(1, 100);
    std::uniform_int_distribution<int> bidDist(5000, 9999);
    std::uniform_int_distribution<int> askDist(10001, 15000);

    std::vector<std::uint64_t> liveIds;
    std::uint64_t nextId = 1;
    constexpr int OPERATIONS = 50000;

    for (int i = 0; i < OPERATIONS; ++i) {
        const int choice = static_cast<int>(rng() % 100);

        if (choice < 70 || liveIds.empty()) {
            const Side side =
                sideDist(rng) == 0 ? Side::Buy : Side::Sell;
            const Price price =
                side == Side::Buy ? bidDist(rng) : askDist(rng);

            Order order{
                nextId++,
                side,
                price,
                qtyDist(rng),
                static_cast<std::uint64_t>(i + 1)
            };

            for (auto* book : books) {
                book->addOrder(order);
            }
            liveIds.push_back(order.id);
        } else if (choice < 90) {
            const std::size_t index =
                static_cast<std::size_t>(rng() % liveIds.size());
            const auto id = liveIds[index];

            for (auto* book : books) {
                if (!book->removeOrder(id)) {
                    throw std::runtime_error("Cancel result mismatch");
                }
            }

            liveIds[index] = liveIds.back();
            liveIds.pop_back();
        } else {
            // Query below checks BBO and depth against every implementation.
        }

        const BBO bboA = a->getBBO();
        const BBO bboB = b->getBBO();
        const BBO bboC = c->getBBO();

        if (!sameBBO(bboA, bboB) || !sameBBO(bboA, bboC)) {
            std::cerr << "BBO mismatch at operation " << i << '\n';
            return 1;
        }

        if (i % 100 == 0) {
            if (!sameLevels(a->getBids(10), b->getBids(10)) ||
                !sameLevels(a->getBids(10), c->getBids(10)) ||
                !sameLevels(a->getAsks(10), b->getAsks(10)) ||
                !sameLevels(a->getAsks(10), c->getAsks(10))) {
                std::cerr << "Depth mismatch at operation " << i << '\n';
                return 1;
            }
        }
    }

    std::cout << "Correctness verifier: PASS\n";
    std::cout << "Implementations compared: A, B, C\n";
    std::cout << "Operations checked: " << OPERATIONS << '\n';
    return 0;
}
