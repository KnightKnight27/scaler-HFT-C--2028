// Fixed Size Array

#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
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

class OrderBookC {
private:
    // Each array index represents a price
    long long bids[10000] = {};
    long long asks[15001] = {};

    unordered_map<int, Order> liveOrders;
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

        // Update the quantity directly at the given price index
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

        // Subtract the cancelled order's quantity from its price level
        if (order.isBuy) {
            bids[order.priceTicks] -= order.quantity;
        } else {
            asks[order.priceTicks] -= order.quantity;
        }

        liveOrders.erase(found);
        return true;
    }

    BBO get_bbo() const {
        BBO result;

        // Search from the highest buy price to find the best bid
        for (int price = 9999; price >= 5000; price--) {
            if (bids[price] > 0) {
                result.hasBid = true;
                result.bestBid = {price, bids[price]};
                break;
            }
        }

        // Search from the lowest sell price to find the best ask
        for (int price = 10001; price <= 15000; price++) {
            if (asks[price] > 0) {
                result.hasAsk = true;
                result.bestAsk = {price, asks[price]};
                break;
            }
        }

        return result;
    }

    vector<Level> get_bids(int depth = 10) const {
        vector<Level> result;

        if (depth <= 0) return result;

        // Skip empty price levels and collect the top buy prices
        for (int price = 9999;
             price >= 5000 && static_cast<int>(result.size()) < depth;
             price--) {
            if (bids[price] > 0) {
                result.push_back({price, bids[price]});
            }
        }

        return result;
    }

    vector<Level> get_asks(int depth = 10) const {
        vector<Level> result;

        if (depth <= 0) return result;

        // Skip empty price levels and collect the top sell prices.
        for (int price = 10001;
             price <= 15000 && static_cast<int>(result.size()) < depth;
             price++) {
            if (asks[price] > 0) {
                result.push_back({price, asks[price]});
            }
        }

        return result;
    }
};

template <typename Book>
double run_one(int seed, uint64_t& checksum) {
    Book book;

    // Use a fixed seed so the workload is repeatable.
    mt19937 rng(seed);
    uniform_int_distribution<int> operationDist(0, 99);
    uniform_int_distribution<int> sideDist(0, 1);
    uniform_int_distribution<int> buyPriceDist(5000, 9999);
    uniform_int_distribution<int> sellPriceDist(10001, 15000);
    uniform_int_distribution<int> quantityDist(1, 100);

    vector<int> activeIDs;
    unordered_map<int, size_t> activeIndex;

    int nextID = 1;
    checksum = 0;

    auto mix = [&](uint64_t value) {
        checksum = checksum * 1315423911ULL + value + 1;
    };

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

    auto start = chrono::steady_clock::now();

    // Run one million operations using the same workload as A and B.
    for (int i = 0; i < 1000000; i++) {
        int operation = operationDist(rng);

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
            checkBBO();
        }

        // Include the top 10 levels in the checksum every 100 operations.
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

    checkBBO();

    auto finish = chrono::steady_clock::now();

    return chrono::duration<double, milli>(
        finish - start
    ).count();
}

int main() {
    vector<double> times;
    uint64_t finalChecksum = 0;

    for (int run = 1; run <= 5; run++) {
        uint64_t checksum = 0;

        double elapsed = run_one<OrderBookC>(
            12345, checksum
        );

        times.push_back(elapsed);
        finalChecksum = checksum;

        cout << "Run " << run << ": "
             << fixed << setprecision(2)
             << elapsed << " ms"
             << ", checksum: " << checksum << endl;
    }

    sort(times.begin(), times.end());

    cout << "\nMedian time: "
         << fixed << setprecision(2)
         << times[2] << " ms" << endl;

    cout << "Final checksum: "
         << finalChecksum << endl;

    return 0;
}
