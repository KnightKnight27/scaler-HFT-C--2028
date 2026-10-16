// Sorted Vector

#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <iomanip>
#include <chrono>
#include <random>
#include <algorithm>
#include <cstdint>

using namespace std;

struct Order {
    int id;
    bool isBuy;
    int priceTicks;
    int quantity;
};

struct Level {
    int priceTicks;
    long long quantity;
};

struct BBO {                
    bool hasBid = false;
    bool hasAsk = false;
    Level bestBid{0, 0};
    Level bestAsk{0, 0};
};

class OrderBookB {
private:
    vector<Level> bids;
    vector<Level> asks;
    unordered_map<int, Order> liveOrders;
    unordered_set<int> usedIDs;

    // Add quantity to a price level or insert a new level.
    void add_to_levels(vector<Level>& levels,
                       int priceTicks,
                       int quantity,
                       bool isBuy) {
        for (size_t i = 0; i < levels.size(); i++) {
            if (levels[i].priceTicks == priceTicks) {
                levels[i].quantity += quantity;
                return;
            }

            // Buy Prices: Highest -> Lowest
            // Sell Prices: Lowest -> Highest
            if (isBuy && levels[i].priceTicks < priceTicks) {
                levels.insert(levels.begin() + i,
                              {priceTicks, quantity});
                return;
            }

            if (!isBuy && levels[i].priceTicks > priceTicks) {
                levels.insert(levels.begin() + i,
                              {priceTicks, quantity});
                return;
            }
        }

        // Add the level at the end if it belongs there.
        levels.push_back({priceTicks, quantity});
    }

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
            add_to_levels(bids, priceTicks, quantity, true);
        } else {
            add_to_levels(asks, priceTicks, quantity, false);
        }

        return true;
    }

    bool cancel_order(int id) {
        auto found = liveOrders.find(id);
        if (found == liveOrders.end()) return false;

        Order order = found->second;

        // Find the price level and subtract the cancelled quantity.
        vector<Level>& levels = order.isBuy ? bids : asks;

        for (size_t i = 0; i < levels.size(); i++) {
            if (levels[i].priceTicks == order.priceTicks) {
                levels[i].quantity -= order.quantity;

                // Remove the level when no quantity remains.
                if (levels[i].quantity == 0) {
                    levels.erase(levels.begin() + i);
                }

                break;
            }
        }

        liveOrders.erase(found);
        return true;
    }

    BBO get_bbo() const {
        BBO result;

        // Since vector is sorted, the first value is the highest
        if (!bids.empty()) {
            result.hasBid = true;
            result.bestBid = bids.front();
        }

        if (!asks.empty()) {
            result.hasAsk = true;
            result.bestAsk = asks.front();
        }

        return result;
    }

    vector<Level> get_bids(int depth = 10) const {
        vector<Level> result;

        if (depth <= 0) return result;

        // Copy only the requested number of top buy levels.
        for (size_t i = 0; i < bids.size() &&
             i < static_cast<size_t>(depth); i++) {
            result.push_back(bids[i]);
        }

        return result;
    }

    vector<Level> get_asks(int depth = 10) const {
        vector<Level> result;

        if (depth <= 0) return result;

        // Copy only the requested number of top sell levels.
        for (size_t i = 0; i < asks.size() &&
             i < static_cast<size_t>(depth); i++) {
            result.push_back(asks[i]);
        }

        return result;
    }
};

// Run the same workload to measure the order book's performance.
template <typename Book>
double run_one(int seed, uint64_t& checksum) {
    Book book;

    // Use a fixed seed so each implementation gets the same workload.
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

    // Mix operation results into a checksum for comparison.
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

        // Add orders for roughly 70% of operations.
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
            // Select an active order at random for cancellation.
            uniform_int_distribution<size_t> activeDist(
                0, activeIDs.size() - 1
            );

            size_t index = activeDist(rng);
            int id = activeIDs[index];

            bool cancelled = book.cancel_order(id);
            mix(cancelled);

            if (cancelled) {
                // Replace the cancelled ID with the last ID to avoid shifting the vector.
                int lastID = activeIDs.back();

                activeIDs[index] = lastID;
                activeIndex[lastID] = index;

                activeIDs.pop_back();
                activeIndex.erase(id);
            }
        } else {
            // Perform a best bid and ask query.
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

        double elapsed = run_one<OrderBookB>(
            12345, checksum
        );

        times.push_back(elapsed);
        finalChecksum = checksum;

        cout << "Run " << run << ": "
             << fixed << setprecision(2)
             << elapsed << " ms"
             << ", checksum: " << checksum << endl;
    }

    // Sort the times to find the median.
    sort(times.begin(), times.end());

    cout << "\nMedian time: "
         << fixed << setprecision(2)
         << times[2] << " ms" << endl;

    cout << "Final checksum: "
         << finalChecksum << endl;

    return 0;
}