#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "Common.h"
#include "OrderBookA.h"
#include "OrderBookB.h"
#include "OrderBookC.h"

namespace
{
    constexpr size_t kTotalOps = 1'000'000;
    constexpr uint32_t kSeed = 42;
    constexpr int kDepthEvery = 100;
    constexpr int kDepth = 10;
    constexpr int kRuns = 5;

    enum class OpType
    {
        Add,
        Cancel,
        Bbo
    };

    struct WorkloadOp
    {
        OpType type;
        uint64_t id = 0;
        Side side = Side::Buy;
        double price = 0.0;
        long qty = 0;
        bool depthCheckpoint = false;
    };

    std::vector<WorkloadOp> generateWorkload(size_t totalOps, uint32_t seed)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> unitDist(0.0, 1.0);
        std::uniform_int_distribution<int> sideDist(0, 1);
        std::uniform_int_distribution<long> qtyDist(1, 100);
        std::uniform_int_distribution<int> bidTickDist(5000, 9999);
        std::uniform_int_distribution<int> askTickDist(10001, 15000);

        std::vector<WorkloadOp> ops;
        ops.reserve(totalOps);

        std::vector<uint64_t> liveIds;
        uint64_t nextId = 1;

        for (size_t i = 0; i < totalOps; ++i)
        {
            double r = unitDist(rng);
            WorkloadOp op;

            if (r < 0.70 || liveIds.empty())
            {
                Side side = (sideDist(rng) == 0) ? Side::Buy : Side::Sell;
                int ticks = (side == Side::Buy) ? bidTickDist(rng) : askTickDist(rng);

                op.type = OpType::Add;
                op.id = nextId++;
                op.side = side;
                op.price = ticksToPrice(ticks);
                op.qty = qtyDist(rng);

                liveIds.push_back(op.id);
            }
            else if (r < 0.90)
            {
                std::uniform_int_distribution<size_t> pick(0, liveIds.size() - 1);
                size_t idx = pick(rng);

                op.type = OpType::Cancel;
                op.id = liveIds[idx];

                liveIds[idx] = liveIds.back();
                liveIds.pop_back();
            }
            else
            {
                op.type = OpType::Bbo;
            }

            op.depthCheckpoint = ((i + 1) % kDepthEvery == 0);
            ops.push_back(op);
        }

        return ops;
    }

    struct BenchResult
    {
        double totalMs = 0.0;
        double nsPerAdd = 0.0;
        double nsPerCancel = 0.0;
        double nsPerBbo = 0.0;
        BBO finalBbo;
        long long checksum = 0;
    };

    template <typename Book>
    BenchResult runOnce(const std::vector<WorkloadOp> &ops)
    {
        using clock = std::chrono::steady_clock;
        Book book;

        long long checksum = 0;
        uint64_t addCount = 0, cancelCount = 0, bboCount = 0;
        int64_t addNs = 0, cancelNs = 0, bboNs = 0;

        auto totalStart = clock::now();

        for (const auto &op : ops)
        {
            switch (op.type)
            {
            case OpType::Add:
            {
                auto s = clock::now();
                book.add_order(op.id, op.side, op.price, op.qty);
                auto e = clock::now();
                addNs += std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
                ++addCount;
                break;
            }
            case OpType::Cancel:
            {
                auto s = clock::now();
                bool ok = book.cancel_order(op.id);
                auto e = clock::now();
                cancelNs += std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
                ++cancelCount;
                checksum += ok ? 1 : 0;
                break;
            }
            case OpType::Bbo:
            {
                auto s = clock::now();
                BBO bbo = book.get_bbo();
                auto e = clock::now();
                bboNs += std::chrono::duration_cast<std::chrono::nanoseconds>(e - s).count();
                ++bboCount;
                checksum += bbo.bid_qty + bbo.ask_qty;
                break;
            }
            }

            if (op.depthCheckpoint)
            {
                for (const auto &l : book.get_bids(kDepth))
                {
                    checksum += l.qty;
                }
                for (const auto &l : book.get_asks(kDepth))
                {
                    checksum += l.qty;
                }
            }
        }

        auto totalEnd = clock::now();

        BenchResult result;
        result.totalMs = std::chrono::duration<double, std::milli>(totalEnd - totalStart).count();
        result.nsPerAdd = addCount ? static_cast<double>(addNs) / static_cast<double>(addCount) : 0.0;
        result.nsPerCancel = cancelCount ? static_cast<double>(cancelNs) / static_cast<double>(cancelCount) : 0.0;
        result.nsPerBbo = bboCount ? static_cast<double>(bboNs) / static_cast<double>(bboCount) : 0.0;
        result.finalBbo = book.get_bbo();
        result.checksum = checksum;
        return result;
    }

    double medianOf(std::vector<double> values)
    {
        std::sort(values.begin(), values.end());
        size_t n = values.size();
        return (n % 2 == 1) ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0;
    }

    void printFinalBbo(const BBO &bbo)
    {
        std::cout << "Final BBO:\n";
        if (bbo.has_bid)
        {
            std::cout << "  Bid: " << bbo.bid_price << " x " << bbo.bid_qty << "\n";
        }
        else
        {
            std::cout << "  Bid: none\n";
        }
        if (bbo.has_ask)
        {
            std::cout << "  Ask: " << bbo.ask_price << " x " << bbo.ask_qty << "\n";
        }
        else
        {
            std::cout << "  Ask: none\n";
        }
    }

    template <typename Book>
    void runBenchmark(const std::string &label)
    {
        auto ops = generateWorkload(kTotalOps, kSeed);

        std::vector<BenchResult> results;
        results.reserve(kRuns);
        for (int run = 0; run < kRuns; ++run)
        {
            results.push_back(runOnce<Book>(ops));
        }

        std::vector<double> totalMs, nsAdd, nsCancel, nsBbo;
        for (const auto &r : results)
        {
            totalMs.push_back(r.totalMs);
            nsAdd.push_back(r.nsPerAdd);
            nsCancel.push_back(r.nsPerCancel);
            nsBbo.push_back(r.nsPerBbo);
        }

        std::cout << "Implementation " << label << " -- median of " << kRuns << " runs (" << kTotalOps << " ops each)\n";
        std::cout << "  Total time : " << medianOf(totalMs) << " ms\n";
        std::cout << "  ns / add   : " << medianOf(nsAdd) << "\n";
        std::cout << "  ns / cancel: " << medianOf(nsCancel) << "\n";
        std::cout << "  ns / BBO   : " << medianOf(nsBbo) << "\n\n";

        printFinalBbo(results[0].finalBbo);
        std::cout << "Checksum: " << results[0].checksum << "\n";
    }

    bool bboEqual(const BBO &x, const BBO &y)
    {
        if (x.has_bid != y.has_bid || x.has_ask != y.has_ask)
        {
            return false;
        }
        if (x.has_bid && (x.bid_price != y.bid_price || x.bid_qty != y.bid_qty))
        {
            return false;
        }
        if (x.has_ask && (x.ask_price != y.ask_price || x.ask_qty != y.ask_qty))
        {
            return false;
        }
        return true;
    }

    bool levelsEqual(const std::vector<Level> &x, const std::vector<Level> &y)
    {
        if (x.size() != y.size())
        {
            return false;
        }
        for (size_t i = 0; i < x.size(); ++i)
        {
            if (x[i].price != y[i].price || x[i].qty != y[i].qty)
            {
                return false;
            }
        }
        return true;
    }

    // Implementation A is treated as the reference: B and C are each diffed
    // against it after every single operation (BBO) and at the same 100-op
    // depth checkpoints the workload already defines (bids/asks). Diffing full
    // depth after every one of the 1,000,000 operations would be excessive and
    // adds nothing the periodic checks don't already cover.
    void runVerify()
    {
        auto ops = generateWorkload(kTotalOps, kSeed);

        OrderBookA a;
        OrderBookB b;
        OrderBookC c;

        bool bOk = true, cOk = true;
        size_t bFailOp = 0, cFailOp = 0;
        std::string bDetail, cDetail;

        for (size_t i = 0; i < ops.size() && !(bFailOp && cFailOp); ++i)
        {
            const auto &op = ops[i];

            if (op.type == OpType::Add)
            {
                a.add_order(op.id, op.side, op.price, op.qty);
                b.add_order(op.id, op.side, op.price, op.qty);
                c.add_order(op.id, op.side, op.price, op.qty);
            }
            else if (op.type == OpType::Cancel)
            {
                bool aOk = a.cancel_order(op.id);
                bool bRes = b.cancel_order(op.id);
                bool cRes = c.cancel_order(op.id);

                if (bOk && bRes != aOk)
                {
                    bOk = false;
                    bFailOp = i + 1;
                    bDetail = "cancel_order return value mismatch";
                }
                if (cOk && cRes != aOk)
                {
                    cOk = false;
                    cFailOp = i + 1;
                    cDetail = "cancel_order return value mismatch";
                }
            }

            BBO ba = a.get_bbo();
            if (bOk && !bboEqual(ba, b.get_bbo()))
            {
                bOk = false;
                bFailOp = i + 1;
                bDetail = "get_bbo() mismatch";
            }
            if (cOk && !bboEqual(ba, c.get_bbo()))
            {
                cOk = false;
                cFailOp = i + 1;
                cDetail = "get_bbo() mismatch";
            }

            if (op.depthCheckpoint)
            {
                auto aBids = a.get_bids(kDepth);
                auto aAsks = a.get_asks(kDepth);

                if (bOk && (!levelsEqual(aBids, b.get_bids(kDepth)) || !levelsEqual(aAsks, b.get_asks(kDepth))))
                {
                    bOk = false;
                    bFailOp = i + 1;
                    bDetail = "get_bids/get_asks mismatch";
                }
                if (cOk && (!levelsEqual(aBids, c.get_bids(kDepth)) || !levelsEqual(aAsks, c.get_asks(kDepth))))
                {
                    cOk = false;
                    cFailOp = i + 1;
                    cDetail = "get_bids/get_asks mismatch";
                }
            }
        }

        std::cout << "Running correctness test...\n\n";
        std::cout << "Implementation A: PASS (reference)\n";
        std::cout << "Implementation B: " << (bOk ? "PASS" : "FAIL") << "\n";
        if (!bOk)
        {
            std::cout << "  diverged at operation " << bFailOp << ": " << bDetail << "\n";
        }
        std::cout << "Implementation C: " << (cOk ? "PASS" : "FAIL") << "\n";
        if (!cOk)
        {
            std::cout << "  diverged at operation " << cFailOp << ": " << cDetail << "\n";
        }
        std::cout << "\n";

        if (bOk && cOk)
        {
            std::cout << "All implementations produce identical results.\n";
        }
        else
        {
            std::cout << "Mismatch detected -- see above for the first diverging operation.\n";
        }
    }

    void printUsage(const char *prog)
    {
        std::cerr << "Usage:\n"
                  << "  " << prog << " --verify\n"
                  << "  " << prog << " --impl A|B|C\n";
    }

}

int main(int argc, char **argv)
{
    std::cout << std::fixed << std::setprecision(2);

    if (argc < 2)
    {
        printUsage(argv[0]);
        return 1;
    }

    std::string arg1 = argv[1];

    if (arg1 == "--verify")
    {
        runVerify();
        return 0;
    }

    if (arg1 == "--impl" && argc >= 3)
    {
        std::string impl = argv[2];
        if (impl == "A")
        {
            runBenchmark<OrderBookA>("A");
        }
        else if (impl == "B")
        {
            runBenchmark<OrderBookB>("B");
        }
        else if (impl == "C")
        {
            runBenchmark<OrderBookC>("C");
        }
        else
        {
            std::cerr << "Unknown implementation: " << impl << "\n";
            printUsage(argv[0]);
            return 1;
        }
        return 0;
    }

    printUsage(argv[0]);
    return 1;
}
