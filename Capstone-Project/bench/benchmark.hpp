#pragma once

#include "array_order_book.hpp"
#include "heap_order_book.hpp"
#include "map_order_book.hpp"
#include "workload.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

struct BenchmarkResult {
    double total_ms;
    double ns_add;
    double ns_cancel;
    double ns_bbo;
    std::uint64_t checksum;
    BBO final_bbo;
};

inline std::unique_ptr<OrderBook> make_book(char implementation) {
    if (implementation == 'A') return std::make_unique<MapOrderBook>();
    if (implementation == 'B') return std::make_unique<HeapOrderBook>();
    if (implementation == 'C') return std::make_unique<ArrayOrderBook>();
    throw std::invalid_argument("implementation must be A, B, or C");
}

inline BenchmarkResult benchmark_once(char implementation, const std::vector<Operation>& workload) {
    auto book = make_book(implementation);
    using Clock = std::chrono::steady_clock;
    std::uint64_t checksum = 0;
    std::int64_t add_ns = 0, cancel_ns = 0, bbo_ns = 0;
    std::size_t adds = 0, cancels = 0, bbos = 0;
    const auto started = Clock::now();

    for (std::size_t i = 0; i < workload.size(); ++i) {
        const auto& op = workload[i];
        const auto before = Clock::now();
        if (op.type == OperationType::Add) {
            book->add_order(op.id, op.side, op.price, op.quantity);
            add_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - before).count();
            ++adds;
        } else if (op.type == OperationType::Cancel) {
            checksum += book->cancel_order(op.id) ? 1 : 0;
            cancel_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - before).count();
            ++cancels;
        } else {
            const auto bbo = book->get_bbo();
            checksum += bbo.has_bid ? static_cast<std::uint64_t>(bbo.bid_price + bbo.bid_quantity) : 0;
            checksum += bbo.has_ask ? static_cast<std::uint64_t>(bbo.ask_price + bbo.ask_quantity) : 0;
            bbo_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - before).count();
            ++bbos;
        }
        if ((i + 1) % 100 == 0) {
            for (const auto& level : book->get_bids(10)) checksum += static_cast<std::uint64_t>(level.price + level.quantity);
            for (const auto& level : book->get_asks(10)) checksum += static_cast<std::uint64_t>(level.price + level.quantity);
        }
    }

    const auto final_bbo = book->get_bbo();
    checksum += final_bbo.has_bid ? static_cast<std::uint64_t>(final_bbo.bid_price * 31 + final_bbo.bid_quantity) : 0;
    checksum += final_bbo.has_ask ? static_cast<std::uint64_t>(final_bbo.ask_price * 31 + final_bbo.ask_quantity) : 0;
    const auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count();
    return {total_ns / 1'000'000.0,
            adds ? static_cast<double>(add_ns) / adds : 0.0,
            cancels ? static_cast<double>(cancel_ns) / cancels : 0.0,
            bbos ? static_cast<double>(bbo_ns) / bbos : 0.0,
            checksum, final_bbo};
}

inline double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}
