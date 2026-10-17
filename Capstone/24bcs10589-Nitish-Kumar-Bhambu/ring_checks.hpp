#pragma once

void verify_ring() {
    bool rejected = false;
    try {
        RingOrderBook zero(0);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "zero ring capacity rejected");

    RingOrderBook single(1);
    for (unsigned long long id = 1; id <= 30; id++) {
        single.add_order(id, Side::Buy, 9999, 7);
        rejected = false;
        try {
            single.add_order(1000 + id, Side::Sell, 10001, 1);
        } catch (const std::length_error&) {
            rejected = true;
        }
        check(rejected && !single.cancel_order(1000 + id), "full ring must not overwrite");
        check(single.get_bbo().bid_qty == 7 && !single.get_bbo().has_ask, "full rejection preserves book");
        check(single.cancel_order(id) && !single.cancel_order(id), "return each slot only once");
        check(!single.get_bbo().has_bid, "single-slot ring drained");
    }

    RingOrderBook failures(2);
    failures.add_order(1, Side::Buy, 9999, std::numeric_limits<long>::max());
    rejected = false;
    try {
        failures.add_order(2, Side::Buy, 9999, 1);
    } catch (const std::overflow_error&) {
        rejected = true;
    }
    check(rejected && !failures.cancel_order(2), "overflow does not insert an ID");
    rejected = false;
    try {
        failures.add_order(3, Side::Sell, 10000, 1);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "invalid ring addition rejected");
    failures.add_order(4, Side::Sell, 10001, 5);
    check(failures.get_bbo().ask_qty == 5, "rejected additions do not consume slots");

    RingOrderBook ring(7);
    OrderBook reference;
    std::vector<unsigned long long> live;
    std::mt19937 rng(2026);
    unsigned long long nextId = 1;
    for (int i = 0; i < 3000; i++) {
        if (live.empty() || rng() % 2 == 0) {
            unsigned long long id = nextId++;
            Side side = rng() % 2 == 0 ? Side::Buy : Side::Sell;
            int price = side == Side::Buy ? 9999 - rng() % 5 : 10001 + rng() % 5;
            long quantity = 1 + rng() % 100;
            if (live.size() == 7) {
                rejected = false;
                try {
                    ring.add_order(id, side, price, quantity);
                } catch (const std::length_error&) {
                    rejected = true;
                }
                check(rejected && !ring.cancel_order(id), "tiny pool capacity enforced");
            } else {
                ring.add_order(id, side, price, quantity);
                reference.add_order(id, side, price, quantity);
                live.push_back(id);
            }
        } else {
            std::size_t index = rng() % live.size();
            auto id = live[index];
            check(ring.cancel_order(id) && reference.cancel_order(id), "tiny pool random cancellation");
            check(!ring.cancel_order(id), "tiny pool repeated cancellation");
            live[index] = live.back();
            live.pop_back();
        }
        check(same_bbo(ring.get_bbo(), reference.get_bbo()), "ring wrap BBO");
        check(same_levels(ring.get_bids(5000), reference.get_bids(5000)), "ring wrap bids");
        check(same_levels(ring.get_asks(5000), reference.get_asks(5000)), "ring wrap asks");
    }
    for (auto id : live) {
        check(ring.cancel_order(id), "tiny pool final drain");
    }
    check(!ring.get_bbo().has_bid && !ring.get_bbo().has_ask, "tiny pool empty at end");
    std::cout << "Ring capacity, slot reuse and wraparound checks: PASS\n";
}
