#include "implementations.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace orderbook;
namespace
{
    constexpr std::size_t kOperations = 1'000'000;
    struct Op
    {
        enum Kind
        {
            Add,
            Cancel,
            Bbo
        } kind;
        std::uint64_t id{};
        Side side{};
        double price{};
        long qty{};
    };
    std::vector<Op> workload()
    {
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> pct(0, 99), qty(1, 100), buy(5000, 9999), sell(10001, 15000);
        std::vector<Op> ops;
        ops.reserve(kOperations);
        std::vector<std::uint64_t> live;
        live.reserve(kOperations);
        std::uint64_t next = 1;
        for (std::size_t i = 0; i < kOperations; ++i)
        {
            int kind = pct(rng);
            if (kind < 70 || live.empty())
            {
                Side s = (pct(rng) < 50) ? Side::Buy : Side::Sell;
                ops.push_back({Op::Add, next, s, tick_to_price(s == Side::Buy ? buy(rng) : sell(rng)), qty(rng)});
                live.push_back(next++);
            }
            else if (kind < 90)
            {
                std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
                auto pos = pick(rng);
                auto id = live[pos];
                live[pos] = live.back();
                live.pop_back();
                ops.push_back({Op::Cancel, id});
            }
            else
                ops.push_back({Op::Bbo});
        }
        return ops;
    }
    std::unique_ptr<OrderBook> make_book(char c)
    {
        if (c == 'A')
            return std::make_unique<TreeBook>();
        if (c == 'B')
            return std::make_unique<VectorBook>();
        if (c == 'C')
            return std::make_unique<ArrayBook>();
        throw std::invalid_argument("implementation must be A, B, or C");
    }
    bool equal_level(const Level &a, const Level &b) { return std::fabs(a.price - b.price) < 1e-9 && a.qty == b.qty; }
    bool equal_levels(const std::vector<Level> &a, const std::vector<Level> &b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (!equal_level(a[i], b[i]))
                return false;
        return true;
    }
    bool equal_bbo(const BBO &a, const BBO &b) { return a.has_bid == b.has_bid && a.has_ask == b.has_ask && (!a.has_bid || (a.bid_price == b.bid_price && a.bid_qty == b.bid_qty)) && (!a.has_ask || (a.ask_price == b.ask_price && a.ask_qty == b.ask_qty)); }
    bool equivalent(const OrderBook &a, const OrderBook &b) { return equal_bbo(a.get_bbo(), b.get_bbo()) && equal_levels(a.get_bids(10), b.get_bids(10)) && equal_levels(a.get_asks(10), b.get_asks(10)); }
    void verify(const std::vector<Op> &ops)
    {
        auto a = make_book('A'), b = make_book('B'), c = make_book('C');
        std::cout << "Running correctness test...\n";
        for (auto *book : {a.get(), b.get(), c.get()})
        {
            auto empty = book->get_bbo();
            if (empty.has_bid || empty.has_ask || book->cancel_order(UINT64_MAX))
                throw std::runtime_error("empty-book or unknown-cancel behavior failed");
        }
        for (std::size_t i = 0; i < ops.size(); ++i)
        {
            const auto &o = ops[i];
            if (o.kind == Op::Add)
            {
                a->add_order(o.id, o.side, o.price, o.qty);
                b->add_order(o.id, o.side, o.price, o.qty);
                c->add_order(o.id, o.side, o.price, o.qty);
            }
            else if (o.kind == Op::Cancel)
            {
                const bool x = a->cancel_order(o.id);
                const bool y = b->cancel_order(o.id);
                const bool z = c->cancel_order(o.id);
                if (!x || !y || !z || x != y || x != z)
                {
                    throw std::runtime_error(
                        "cancel result mismatch at op " + std::to_string(i));
                }
            }
            else
            {
                a->get_bbo();
                b->get_bbo();
                c->get_bbo();
            }
            if ((i + 1) % 100 == 0)
            {
                a->get_bids(10);
                a->get_asks(10);
                b->get_bids(10);
                b->get_asks(10);
                c->get_bids(10);
                c->get_asks(10);
            }
            if (!equivalent(*a, *b) || !equivalent(*a, *c))
                throw std::runtime_error("book mismatch at op " + std::to_string(i));
        }
        for (char x : {'A', 'B', 'C'})
            std::cout << "Implementation " << x << ": PASS\n";
        std::cout << "\nAll implementations produce identical results.\n";
    }
    struct Result
    {
        double total, add, cancel, bbo;
        std::uint64_t checksum;
    };
    Result run_once(char impl, const std::vector<Op> &ops)
    {
        auto book = make_book(impl);
        using Clock = std::chrono::steady_clock;
        std::uint64_t nadd = 0, ncancel = 0, nbbo = 0, checksum = 0;
        long long add_ns = 0, cancel_ns = 0, bbo_ns = 0;
        const auto begin = Clock::now();
        for (std::size_t i = 0; i < ops.size(); ++i)
        {
            const auto &o = ops[i];
            const auto t = Clock::now();
            if (o.kind == Op::Add)
            {
                book->add_order(o.id, o.side, o.price, o.qty);
                add_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t).count();
                ++nadd;
            }
            else if (o.kind == Op::Cancel)
            {
                if (!book->cancel_order(o.id))
                    throw std::runtime_error("generated cancel was not live");
                cancel_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t).count();
                ++ncancel;
            }
            else
            {
                auto x = book->get_bbo();
                checksum += x.has_bid ? static_cast<std::uint64_t>(x.bid_price * 100) + x.bid_qty : 0;
                checksum += x.has_ask ? static_cast<std::uint64_t>(x.ask_price * 100) + x.ask_qty : 0;
                bbo_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t).count();
                ++nbbo;
            }
            if ((i + 1) % 100 == 0)
            {
                auto bids = book->get_bids(10);
                auto asks = book->get_asks(10);
                for (auto l : bids)
                    checksum += static_cast<std::uint64_t>(l.price * 100) + l.qty;
                for (auto l : asks)
                    checksum += static_cast<std::uint64_t>(l.price * 100) + l.qty;
            }
        }
        const auto total = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        auto final = book->get_bbo();
        checksum += final.has_bid ? static_cast<std::uint64_t>(final.bid_price * 100) + final.bid_qty : 0;
        checksum += final.has_ask ? static_cast<std::uint64_t>(final.ask_price * 100) + final.ask_qty : 0;
        return {total, static_cast<double>(add_ns) / nadd, static_cast<double>(cancel_ns) / ncancel, static_cast<double>(bbo_ns) / nbbo, checksum};
    }
    double median(std::vector<double> v)
    {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    }
    void benchmark(char impl, const std::vector<Op> &ops)
    {
        std::vector<Result> r;
        for (int i = 0; i < 5; ++i)
            r.push_back(run_once(impl, ops));
        std::vector<double> t, a, c, b;
        for (auto x : r)
        {
            t.push_back(x.total);
            a.push_back(x.add);
            c.push_back(x.cancel);
            b.push_back(x.bbo);
        }
        auto book = make_book(impl);
        for (const auto &o : ops)
        {
            if (o.kind == Op::Add)
                book->add_order(o.id, o.side, o.price, o.qty);
            else if (o.kind == Op::Cancel)
                book->cancel_order(o.id);
        }
        auto final = book->get_bbo();
        std::cout << "Implementation " << impl << " (median of 5 runs, 1,000,000 operations each)\n"
                  << "Total time: " << median(t) << " ms\nns / add_order: " << median(a) << "\nns / cancel_order: " << median(c) << "\nns / get_bbo: " << median(b) << "\nFinal BBO:\n";
        if (final.has_bid)
            std::cout << "Bid: " << final.bid_price << " x " << final.bid_qty << "\n";
        else
            std::cout << "Bid: none\n";
        if (final.has_ask)
            std::cout << "Ask: " << final.ask_price << " x " << final.ask_qty << "\n";
        else
            std::cout << "Ask: none\n";
        std::cout << "Checksum: " << r.front().checksum << "\n";
    }
}
int main(int argc, char **argv)
{
    try
    {
        const auto ops = workload();
        if (argc == 2 && std::string(argv[1]) == "--verify")
        {
            verify(ops);
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--impl")
        {
            benchmark(argv[2][0], ops);
            return 0;
        }
        std::cerr << "Usage: ./bench --verify | ./bench --impl A|B|C\n";
        return 2;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
