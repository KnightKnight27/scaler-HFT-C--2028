#include "order_book.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
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
using Clock = std::chrono::steady_clock;

struct Stats {
    std::uint64_t adds = 0;
    std::uint64_t cancels = 0;
    std::uint64_t bboQueries = 0;
    std::uint64_t addNs = 0;
    std::uint64_t cancelNs = 0;
    std::uint64_t bboNs = 0;
    std::uint64_t checksum = 0;
};

static std::unique_ptr<OrderBook> makeBook(char impl) {
    switch (impl) {
        case 'A': return createMapOrderBook();
        case 'B': return createHeapOrderBook();
        case 'C': return createArrayOrderBook();
        default:
            throw std::invalid_argument("Implementation must be A, B, or C");
    }
}

int main(int argc, char** argv) {
    char impl = 'A';

    if (argc == 3 && std::string(argv[1]) == "--impl") {
        impl = argv[2][0];
    } else {
        std::cerr << "Usage: " << argv[0] << " --impl A|B|C\n";
        return 2;
    }

    auto book = makeBook(impl);

    constexpr int OPERATIONS = 1'000'000;
    std::mt19937_64 rng(123456789);
    std::uniform_int_distribution<int> qtyDist(1, 100);
    std::uniform_int_distribution<int> bidDist(5000, 9999);
    std::uniform_int_distribution<int> askDist(10001, 15000);

    std::vector<std::uint64_t> liveIds;
    liveIds.reserve(OPERATIONS);

    std::uint64_t nextId = 1;
    Stats stats{};

    const auto start = Clock::now();

    for (int i = 0; i < OPERATIONS; ++i) {
        const int choice = static_cast<int>(rng() % 100);

        if (choice < 70 || liveIds.empty()) {
            const Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
            const Price price =
                side == Side::Buy ? bidDist(rng) : askDist(rng);

            Order order{
                nextId++,
                side,
                price,
                qtyDist(rng),
                static_cast<std::uint64_t>(i + 1)
            };

            const auto t0 = Clock::now();
            book->addOrder(order);
            const auto t1 = Clock::now();

            stats.addNs += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    t1 - t0
                ).count()
            );
            ++stats.adds;
            liveIds.push_back(order.id);
            stats.checksum += order.id + order.priceTicks + order.qty;
        } else if (choice < 90) {
            const std::size_t index =
                static_cast<std::size_t>(rng() % liveIds.size());
            const auto id = liveIds[index];

            const auto t0 = Clock::now();
            const bool removed = book->removeOrder(id);
            const auto t1 = Clock::now();

            stats.cancelNs += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    t1 - t0
                ).count()
            );

            if (!removed) {
                std::cerr << "Failed to cancel live order " << id << '\n';
                return 1;
            }

            ++stats.cancels;
            stats.checksum ^= id;

            liveIds[index] = liveIds.back();
            liveIds.pop_back();
        } else {
            const auto t0 = Clock::now();
            const BBO bbo = book->getBBO();
            const auto t1 = Clock::now();

            stats.bboNs += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    t1 - t0
                ).count()
            );

            ++stats.bboQueries;
            if (bbo.has_bid) {
                stats.checksum += static_cast<std::uint64_t>(
                    bbo.bid_price * 100
                ) + static_cast<std::uint64_t>(bbo.bid_qty);
            }
            if (bbo.has_ask) {
                stats.checksum += static_cast<std::uint64_t>(
                    bbo.ask_price * 100
                ) + static_cast<std::uint64_t>(bbo.ask_qty);
            }
        }

        if (i % 100 == 0) {
            const auto bids = book->getBids(10);
            const auto asks = book->getAsks(10);

            for (const auto& level : bids) {
                stats.checksum += static_cast<std::uint64_t>(
                    level.price * 100
                ) + static_cast<std::uint64_t>(level.qty);
            }
            for (const auto& level : asks) {
                stats.checksum += static_cast<std::uint64_t>(
                    level.price * 100
                ) + static_cast<std::uint64_t>(level.qty);
            }
        }
    }

    const auto end = Clock::now();
    const auto totalNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start
        ).count();

    const double totalMs = static_cast<double>(totalNs) / 1e6;

    std::cout << "Implementation: " << impl << '\n';
    std::cout << "Operations: " << OPERATIONS << '\n';
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total time: " << totalMs << " ms\n";
    std::cout << "Adds: " << stats.adds
              << ", average: "
              << (stats.adds ? stats.addNs / stats.adds : 0)
              << " ns/add\n";
    std::cout << "Cancels: " << stats.cancels
              << ", average: "
              << (stats.cancels ? stats.cancelNs / stats.cancels : 0)
              << " ns/cancel\n";
    std::cout << "BBO queries: " << stats.bboQueries
              << ", average: "
              << (stats.bboQueries
                      ? stats.bboNs / stats.bboQueries : 0)
              << " ns/query\n";

    const BBO finalBbo = book->getBBO();
    std::cout << "Final BBO: ";

    if (finalBbo.has_bid) {
        std::cout << "bid=" << finalBbo.bid_price
                  << " qty=" << finalBbo.bid_qty;
    } else {
        std::cout << "bid=empty";
    }

    std::cout << ", ";

    if (finalBbo.has_ask) {
        std::cout << "ask=" << finalBbo.ask_price
                  << " qty=" << finalBbo.ask_qty;
    } else {
        std::cout << "ask=empty";
    }

    std::cout << "\nChecksum: " << stats.checksum << '\n';
    return 0;
}
