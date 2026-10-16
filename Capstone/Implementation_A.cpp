// Map

#include <iostream>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <iomanip>
#include <chrono>
#include <random>
#include <algorithm>
#include <cstdint>

using namespace std;

// One order placed by one buyer or seller.
struct Order {
    int id;
    bool isBuy;
    // Price stored as integers not floats
    int priceTicks;
    int quantity;
};

// One price level and the quantity associated with that price.
struct Level {
    int priceTicks;
    long long quantity;
};

struct BBO {                 // Best Bid and Offer
    bool hasBid = false;
    bool hasAsk = false;
    Level bestBid{0, 0};
    Level bestAsk{0, 0};
};

class OrderBookA {
private:
    // Best bid is the highest buy price
    map<int, long long, greater<int>> bids;

    // Best ask is lowest sell price
    map<int, long long> asks;

    // Active orders that can be found by id
    unordered_map<int, Order> liveOrders;

    // IDs cannot be reused
    unordered_set<int> usedIDs;

public:
    bool add_order(int id, bool isBuy, int priceTicks, int quantity) {
        if (id <= 0 || quantity <= 0) return false;
        if (usedIDs.count(id) != 0) return false;

        if (isBuy && (priceTicks < 5000 || priceTicks > 9999)) return false;
        if (!isBuy && (priceTicks < 10001 || priceTicks > 15000)) return false;

        Order order{id, isBuy, priceTicks, quantity};
        liveOrders[id] = order;
        usedIDs.insert(id);

        if (isBuy) {
            bids[priceTicks] += quantity;
        } else {
            asks[priceTicks] += quantity;
        }
        return true;
    }

    bool cancel_order(int id) {
        auto found = liveOrders.find(id);
        if (found == liveOrders.end()) return false;

        Order order = found->second;

        if (order.isBuy) {
            bids[order.priceTicks] -= order.quantity;
            if (bids[order.priceTicks] == 0) bids.erase(order.priceTicks);
        } else {
            asks[order.priceTicks] -= order.quantity;
            if (asks[order.priceTicks] == 0) asks.erase(order.priceTicks);
        }

        liveOrders.erase(found);
        return true;
    }

    // Best bid and ask
    BBO get_bbo() const {
        BBO result;

        if (!bids.empty()) {
            result.hasBid = true;
            result.bestBid = {bids.begin()->first, bids.begin()->second};
        }

        if (!asks.empty()) {
            result.hasAsk = true;
            result.bestAsk = {asks.begin()->first, asks.begin()->second};
        }

        return result;
    }

    // Top buy price levels.
    vector<Level> get_bids(int depth = 10) const {
        vector<Level> result;

        if (depth <= 0) {
            return result;
        }

        for (const auto& item : bids) {
            if (static_cast<int>(result.size()) >= depth) {
                break;
            }

            result.push_back({item.first, item.second});
        }

        return result;
    }

    // Top sell price levels.
    vector<Level> get_asks(int depth = 10) const {
        vector<Level> result;

        if (depth <= 0) {
            return result;
        }

        for (const auto& item : asks) {
            if (static_cast<int>(result.size()) >= depth) {
                break;
            }

            result.push_back({item.first, item.second});
        }

        return result;
    }
};

// Run the same workload to measure the order book's performance.
template <typename Book>
double run_one(int seed, uint64_t& checksum) {
    Book book;

    // Generate the same sequence of operations for each run.
    mt19937 rng(seed);
    uniform_int_distribution<int> operationDist(0, 99);
    uniform_int_distribution<int> sideDist(0, 1);
    uniform_int_distribution<int> buyPriceDist(5000, 9999);
    uniform_int_distribution<int> sellPriceDist(10001, 15000);
    uniform_int_distribution<int> quantityDist(1, 100);

    // Keep track of orders that can still be cancelled.
    vector<int> activeIDs;
    unordered_map<int, size_t> activeIndex;

    int nextID = 1;
    checksum = 0;

    // Mix results into a checksum to compare the runs.
    auto mix = [&](uint64_t value) {
        checksum = checksum * 1315423911ULL + value + 1;
    };

    // Check the best bid and ask.
    auto checkBBO = [&]() {
        BBO result = book.get_bbo();

        mix(result.hasBid);
        mix(result.hasAsk);

        if (result.hasBid) {
            mix(result.bestBid.priceTicks);
            mix(result.bestBid.quantity);
        }

        if (result.hasAsk) {
            mix(result.bestAsk.priceTicks);
            mix(result.bestAsk.quantity);
        }
    };

    // Start measuring the time for the workload.
    auto start = chrono::steady_clock::now();

    for (int i = 0; i < 1000000; i++) {
        int operation = operationDist(rng);

        // Add an order about 70% of the time.
        if (operation < 70 || activeIDs.empty()) {
            bool isBuy = (sideDist(rng) == 0);

            int priceTicks = isBuy
                ? buyPriceDist(rng)
                : sellPriceDist(rng);

            int quantity = quantityDist(rng);
            int id = nextID++;

            bool added = book.add_order(
                id, isBuy, priceTicks, quantity
            );

            mix(added);

            if (added) {
                activeIndex[id] = activeIDs.size();
                activeIDs.push_back(id);
            }
        } else if (operation < 90) {
            // Cancel a randomly selected active order.
            uniform_int_distribution<size_t> activeDist(
                0, activeIDs.size() - 1
            );

            size_t index = activeDist(rng);
            int id = activeIDs[index];

            bool cancelled = book.cancel_order(id);
            mix(cancelled);

            if (cancelled) {
                int lastID = activeIDs.back();

                activeIDs[index] = lastID;
                activeIndex[lastID] = index;

                activeIDs.pop_back();
                activeIndex.erase(id);
            }
        } else {
            // Check the best prices for this operation.
            checkBBO();
        }

        // Check the top 10 levels every 100 operations.
        if ((i + 1) % 100 == 0) {
            vector<Level> buyLevels = book.get_bids(10);
            vector<Level> sellLevels = book.get_asks(10);

            for (const auto& level : buyLevels) {
                mix(level.priceTicks);
                mix(level.quantity);
            }

            for (const auto& level : sellLevels) {
                mix(level.priceTicks);
                mix(level.quantity);
            }
        }
    }

    // Include the final best bid and ask in the checksum.
    checkBBO();

    auto finish = chrono::steady_clock::now();

    return chrono::duration<double, milli>(
        finish - start
    ).count();
}

int main() {
    vector<double> times;
    uint64_t finalChecksum = 0;

    // Run the benchmark five times.
    for (int run = 1; run <= 5; run++) {
        uint64_t checksum = 0;

        double elapsed = run_one<OrderBookA>(
            12345, checksum
        );

        times.push_back(elapsed);
        finalChecksum = checksum;

        cout << "Run " << run << ": "
             << fixed << setprecision(2)
             << elapsed << " ms"
             << ", checksum: " << checksum << endl;
    }

    // Sort the times to find the middle result.
    sort(times.begin(), times.end());

    cout << "\nMedian time: "
         << fixed << setprecision(2)
         << times[2] << " ms" << endl;

    cout << "Final checksum: "
         << finalChecksum << endl;

    return 0;
}