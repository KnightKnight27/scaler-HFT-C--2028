#pragma once
#include "order_book.hpp"
#include <array>
#include <list>
#include <map>
#include <queue>
#include <unordered_map>

namespace orderbook
{

    // A: ordered price tree, FIFO order list per level, and direct ID-to-list locator.
    class TreeBook final : public OrderBook
    {
        struct Resting
        {
            std::uint64_t id;
            long qty;
        };
        using Queue = std::list<Resting>;
        struct Bucket
        {
            Queue orders;
            long qty{};
        };
        using Levels = std::map<int, Bucket>;
        struct Locator
        {
            Side side;
            int tick;
            Queue::iterator order;
        };
        Levels bids_, asks_;
        std::unordered_map<std::uint64_t, Locator> ids_;
        Levels &levels(Side s) { return s == Side::Buy ? bids_ : asks_; }
        const Levels &levels(Side s) const { return s == Side::Buy ? bids_ : asks_; }
        std::vector<Level> read(const Levels &m, bool reverse, int depth) const;

    public:
        void add_order(std::uint64_t, Side, double, long) override;
        bool cancel_order(std::uint64_t) override;
        BBO get_bbo() const override;
        std::vector<Level> get_bids(int d = 10) const override { return read(bids_, true, d); }
        std::vector<Level> get_asks(int d = 10) const override { return read(asks_, false, d); }
    };

    // B: sorted contiguous vectors of aggregate levels; the ID hash stores cancellation data.
    class VectorBook final : public OrderBook
    {
        struct PriceLevel
        {
            int tick;
            long qty;
        };
        struct OrderRef
        {
            Side side;
            int tick;
            long qty;
        };
        std::vector<PriceLevel> bids_, asks_;
        std::unordered_map<std::uint64_t, OrderRef> ids_;
        std::vector<PriceLevel> &levels(Side s) { return s == Side::Buy ? bids_ : asks_; }
        const std::vector<PriceLevel> &levels(Side s) const { return s == Side::Buy ? bids_ : asks_; }
        static std::vector<Level> read(const std::vector<PriceLevel> &, bool, int);

    public:
        void add_order(std::uint64_t, Side, double, long) override;
        bool cancel_order(std::uint64_t) override;
        BBO get_bbo() const override;
        std::vector<Level> get_bids(int d = 10) const override { return read(bids_, true, d); }
        std::vector<Level> get_asks(int d = 10) const override { return read(asks_, false, d); }
    };

    // C: fixed tick-indexed quantity arrays, ID hash, and generation-checked lazy heaps for BBO.
    class ArrayBook final : public OrderBook
    {
        static constexpr int kFirst = 5000, kLast = 15000, kSize = kLast - kFirst + 1;
        struct LevelState
        {
            long qty{};
            std::uint64_t generation{};
        };
        struct HeapEntry
        {
            int tick;
            std::uint64_t generation;
        };
        struct BetterBid
        {
            bool operator()(HeapEntry a, HeapEntry b) const { return a.tick < b.tick; }
        };
        struct BetterAsk
        {
            bool operator()(HeapEntry a, HeapEntry b) const { return a.tick > b.tick; }
        };
        struct OrderRef
        {
            Side side;
            int tick;
            long qty;
        };
        std::array<LevelState, kSize> bids_{}, asks_{};
        std::unordered_map<std::uint64_t, OrderRef> ids_;
        mutable std::priority_queue<HeapEntry, std::vector<HeapEntry>, BetterBid> bid_heap_;
        mutable std::priority_queue<HeapEntry, std::vector<HeapEntry>, BetterAsk> ask_heap_;
        static int index(int tick) { return tick - kFirst; }
        LevelState &state(Side s, int tick) { return (s == Side::Buy ? bids_ : asks_)[index(tick)]; }
        const LevelState &state(Side s, int tick) const { return (s == Side::Buy ? bids_ : asks_)[index(tick)]; }
        void discard_stale() const;
        static std::vector<Level> read(const std::array<LevelState, kSize> &, bool, int);

    public:
        void add_order(std::uint64_t, Side, double, long) override;
        bool cancel_order(std::uint64_t) override;
        BBO get_bbo() const override;
        std::vector<Level> get_bids(int d = 10) const override { return read(bids_, true, d); }
        std::vector<Level> get_asks(int d = 10) const override { return read(asks_, false, d); }
    };

} // namespace orderbook
