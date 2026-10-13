#include "benchmark.hpp"

#include <cstdlib>
#include <iostream>

bool same(const BBO& a, const BBO& b) {
    return a.has_bid == b.has_bid && a.bid_price == b.bid_price && a.bid_quantity == b.bid_quantity &&
           a.has_ask == b.has_ask && a.ask_price == b.ask_price && a.ask_quantity == b.ask_quantity;
}

bool same(const std::vector<Level>& a, const std::vector<Level>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const Level& x, const Level& y) {
        return x.price == y.price && x.quantity == y.quantity;
    });
}

void verify() {
    auto a = make_book('A'); auto b = make_book('B'); auto c = make_book('C');
    const auto workload = make_workload(1'000'000);
    for (std::size_t i = 0; i < workload.size(); ++i) {
        apply(workload[i], *a); apply(workload[i], *b); apply(workload[i], *c);
        if (!same(a->get_bbo(), b->get_bbo()) || !same(a->get_bbo(), c->get_bbo()) ||
            !same(a->get_bids(), b->get_bids()) || !same(a->get_bids(), c->get_bids()) ||
            !same(a->get_asks(), b->get_asks()) || !same(a->get_asks(), c->get_asks()))
            throw std::runtime_error("implementations disagree at operation " + std::to_string(i));
    }
    std::cout << "Implementation A: PASS\nImplementation B: PASS\nImplementation C: PASS\n"
                 "All implementations produce identical results.\n";
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--verify") { verify(); return 0; }
        if (argc != 3 || std::string(argv[1]) != "--impl") {
            std::cerr << "usage: ./bench --verify | ./bench --impl A|B|C\n";
            return 2;
        }
        const char implementation = argv[2][0];
        const auto workload = make_workload(1'000'000);
        std::vector<BenchmarkResult> runs;
        for (int i = 0; i < 5; ++i) runs.push_back(benchmark_once(implementation, workload));
        std::vector<double> totals, adds, cancels, bbos;
        for (const auto& run : runs) { totals.push_back(run.total_ms); adds.push_back(run.ns_add); cancels.push_back(run.ns_cancel); bbos.push_back(run.ns_bbo); }
        const auto& final = runs.back();
        std::cout << "Implementation: " << implementation << "\n"
                  << "Total time (median ms): " << median(totals) << "\n"
                  << "ns / add (median): " << median(adds) << "\n"
                  << "ns / cancel (median): " << median(cancels) << "\n"
                  << "ns / BBO (median): " << median(bbos) << "\n"
                  << "Checksum: " << final.checksum << "\n"
                  << "Final BBO: bid=" << (final.final_bbo.has_bid ? std::to_string(final.final_bbo.bid_price) : "empty")
                  << " ask=" << (final.final_bbo.has_ask ? std::to_string(final.final_bbo.ask_price) : "empty") << "\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
