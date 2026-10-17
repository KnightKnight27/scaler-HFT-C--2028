#include "types.hpp"
#include "MapOrderbook.hpp"
#include "PQOrderbook.hpp"
#include "ArrayOrderbook.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <string>
#include <chrono>
#include <algorithm>

enum class Operation_type {Add, Cancel, BBO};

struct Operation {
    Operation_type type;
    uint64_t id;
    Side side;
    double price;
    long qty;
    uint64_t cancel_id;
};

std::vector<Operation> workload(long num_of_operations){
    std::vector<Operation> operations;
    operations.reserve(num_of_operations);
    std::mt19937 rng(42); //random number generator
    std::uniform_int_distribution<int> operation_dist(0, 9);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> qty_dist(1, 100);

    std::uniform_int_distribution<int> bid_price_dist(MIN_BID, MAX_BID);
    std::uniform_int_distribution<int> ask_price_dist(MIN_ASK, MAX_ASK);

    std::vector<uint64_t> active_orders;

    for(long i=0L;i<num_of_operations;++i){
        int operation_type = operation_dist(rng);
        if(operation_type<7 || active_orders.empty()){
            // add order
            Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
            double price = (side == Side::Buy) ? (bid_price_dist(rng)/100.0) : (ask_price_dist(rng)/100.0);
            long qty = qty_dist(rng);
            uint64_t id = i;
            operations.push_back({Operation_type::Add, id,side,price,qty,0});
            active_orders.push_back(id);
        } else if (operation_type < 9){
            // cancel order
            std::uniform_int_distribution<long> idx_dist(0, active_orders.size()-1);
            long idx = idx_dist(rng);
            operations.push_back({Operation_type::Cancel, 0,Side::Buy,0,0,active_orders[idx]});
            active_orders[idx] = active_orders.back();
            active_orders.pop_back();
        } else {
            // BBO query
            operations.push_back({Operation_type::BBO, 0,Side::Buy,0,0,0});
        }
    }
    return operations;
}

bool verify(){
    std::cout << "RUNNING CORRECTNESS CHECK" << std::endl;
    std::cout << std::endl;

    std::vector<Operation> ops = workload(10000);

    OrderBookMap maporderbook;
    OrderBookPQ pqorderbook;
    OrderBookArray arrayorderbook;

    for(long i=0;i<ops.size();i++){
        auto& op = ops[i];
        if(op.type == Operation_type::Add){
            maporderbook.add_order(op.id, op.side, op.price, op.qty);
            pqorderbook.add_order(op.id, op.side, op.price, op.qty);
            arrayorderbook.add_order(op.id, op.side, op.price, op.qty);
        } else if (op.type == Operation_type::Cancel){
            bool map_cancel = maporderbook.cancel_order(op.cancel_id);
            bool pq_cancel = pqorderbook.cancel_order(op.cancel_id);
            bool array_cancel = arrayorderbook.cancel_order(op.cancel_id);
        } else {
            BBO map_bbo = maporderbook.get_bbo();
            BBO pq_bbo = pqorderbook.get_bbo();
            BBO array_bbo = arrayorderbook.get_bbo();
            
            if(map_bbo.has_bid != pq_bbo.has_bid || map_bbo.has_ask != pq_bbo.has_ask || map_bbo.has_bid != array_bbo.has_bid || map_bbo.has_ask != array_bbo.has_ask){
                std::cout << "BBO MISMATCH!" << std::endl;
                return false;
            }
            if (map_bbo.has_bid){
                if(map_bbo.bid_price != pq_bbo.bid_price || map_bbo.bid_price != array_bbo.bid_price){ 
                    std::cout << "BBO BID PRICE MISMATCH!" << std::endl;
                    return false;
                }
                if (map_bbo.bid_qty != pq_bbo.bid_qty || map_bbo.bid_qty != array_bbo.bid_qty){
                    std::cout << "BBO BID QTY MISMATCH!" << std::endl;
                    return false;
                }
            }
            if (map_bbo.has_ask){
                if(map_bbo.ask_price != pq_bbo.ask_price || map_bbo.ask_price != array_bbo.ask_price){ 
                    std::cout << "BBO ASK PRICE MISMATCH!" << std::endl;
                    return false;
                }
                if (map_bbo.ask_qty != pq_bbo.ask_qty || map_bbo.ask_qty != array_bbo.ask_qty){
                    std::cout << "BBO ASK QTY MISMATCH!" << std::endl;
                    return false;
                }
            }
        }

        if(i%100 == 0){
            auto map_bids = maporderbook.get_bids(10);
            auto pq_bids = pqorderbook.get_bids(10);
            auto array_bids = arrayorderbook.get_bids(10);

            auto map_asks = maporderbook.get_asks(10);
            auto pq_asks = pqorderbook.get_asks(10);
            auto array_asks = arrayorderbook.get_asks(10);

            if (map_bids.size() != pq_bids.size() || map_bids.size() != array_bids.size() || map_asks.size() != pq_asks.size() || map_asks.size() != array_asks.size()){
                std::cout << "LEVELS MISMATCH!" << std::endl;
                return false;
            }

            for (long j=0; j<map_bids.size(); j++){
                if (map_bids[j].price != pq_bids[j].price || map_bids[j].price != array_bids[j].price){
                    std::cout << "BID LEVEL PRICES MISMATCH!" << std::endl;
                    return false;
                }
                if (map_bids[j].qty != pq_bids[j].qty || map_bids[j].qty != array_bids[j].qty){
                    std::cout << "BID LEVELS QTY MISMATCH!" << std::endl;
                    return false;
                }
            }
            for(long j=0; j<map_asks.size(); j++){
                if (map_asks[j].price != pq_asks[j].price || map_asks[j].price != array_asks[j].price){
                    std::cout << "ASK LEVEL PRICES MISMATCH!" << std::endl;
                    return false;
                }
                if (map_asks[j].qty != pq_asks[j].qty || map_asks[j].qty != array_asks[j].qty){
                    std::cout << "ASK LEVELS QTY MISMATCH!" << std::endl;
                    return false;
                }
            }
        }
    }
    std::cout << "Implementation A (Map):   PASS\n";
    std::cout << "Implementation B (PQ):    PASS\n";
    std::cout << "Implementation C (Array): PASS\n";
    std::cout << "All implementations produce identical results.\n";
    return true;
}

struct RunStats {
    double total_ms;
    double add_ns;
    double cancel_ns;
    double bbo_ns;
    BBO final_bbo;
};

template <typename T>
bool benchmark() {
    long num_of_operations = 1000000;
    std::vector<Operation> ops = workload(num_of_operations);
    std::vector<RunStats> runs;
    runs.reserve(5);

    for(int run=1; run<=5; run++){
        T orderbook;
        auto start_total = std::chrono::steady_clock::now();
        uint64_t add_time = 0, cancel_time = 0, bbo_time = 0;
        uint64_t add_count = 0, cancel_count = 0, bbo_count = 0;
        
        for(long i=0l; i<ops.size(); i++){
            auto& op = ops[i];
            if(op.type == Operation_type::Add){
                auto start_add = std::chrono::steady_clock::now();
                orderbook.add_order(op.id, op.side, op.price, op.qty);
                auto end_add = std::chrono::steady_clock::now();
                add_time += std::chrono::duration_cast<std::chrono::nanoseconds>(end_add - start_add).count();
                add_count++;
            } 
            else if (op.type == Operation_type::Cancel){
                auto start_cancel = std::chrono::steady_clock::now();
                orderbook.cancel_order(op.cancel_id);
                auto end_cancel = std::chrono::steady_clock::now();
                cancel_time += std::chrono::duration_cast<std::chrono::nanoseconds>(end_cancel - start_cancel).count();
                cancel_count++;
            } 
            else {
                auto start_bbo = std::chrono::steady_clock::now();
                BBO bbo = orderbook.get_bbo();
                auto end_bbo = std::chrono::steady_clock::now();
                bbo_time += std::chrono::duration_cast<std::chrono::nanoseconds>(end_bbo - start_bbo).count();
                bbo_count++;
            }   
            
            if(i%100 == 0){
                auto bids = orderbook.get_bids(10);
                auto asks = orderbook.get_asks(10);
            }
        }
        BBO final_bbo  = orderbook.get_bbo();
        
        auto end_total = std::chrono::steady_clock::now();
        auto total_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_total - start_total).count();
        runs.push_back({total_time/1e6, (double)add_time/add_count, (double)cancel_time/cancel_count, (double)bbo_time/bbo_count, final_bbo});
        
        std::cout << "RUN " << run << "\n";
        std::cout << "TOTAL TIME: " << total_time << " ns\n";
        std::cout << "ADD TIME: " << add_time/add_count << " ns\n";
        std::cout << "CANCEL TIME: " << cancel_time/cancel_count << " ns\n";
        std::cout << "BBO TIME: " << bbo_time/bbo_count << " ns\n";
        std::cout << "Final BBO: Bid: " << (final_bbo.has_bid ? std::to_string(final_bbo.bid_price/100.0) + " x " + std::to_string(final_bbo.bid_qty) : "None")
        << " | Ask: " << (final_bbo.has_ask ? std::to_string(final_bbo.ask_price/100.0) + " x " + std::to_string(final_bbo.ask_qty) : "None") << "\n";
    }
    std::sort(runs.begin(), runs.end(), [](const RunStats& a, const RunStats& b){return a.total_ms < b.total_ms;});
    std::cout << "\n";
    std::cout << "MEDIAN of 5 RUNS: \n";
    std::cout << "TOTAL TIME: " << runs[2].total_ms << " ms\n";
    std::cout << "ADD TIME: " << runs[2].add_ns << " ns\n";
    std::cout << "CANCEL TIME: " << runs[2].cancel_ns << " ns\n";
    std::cout << "BBO TIME: " << runs[2].bbo_ns << " ns\n";
    std::cout << "Final BBO: Bid: " << (runs[2].final_bbo.has_bid ? std::to_string(runs[2].final_bbo.bid_price/100.0) + " x " + std::to_string(runs[2].final_bbo.bid_qty) : "None")
    << " | Ask: " << (runs[2].final_bbo.has_ask ? std::to_string(runs[2].final_bbo.ask_price/100.0) + " x " + std::to_string(runs[2].final_bbo.ask_qty) : "None") << "\n";
    std::cout << "\n";
    return true;
}

int main (int argc, char* argv[]){
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " [--verify | --impl A | --impl B | --impl C]\n";
        return 1;
    }
    if(std::string(argv[1]) == "--verify"){
        return verify() ? 0:1;
    } else if (std::string(argv[1]) == "--impl"){
        if (std::string(argv[2]) == "A"){
            benchmark<OrderBookMap>();
        } else if (std::string(argv[2]) == "B"){
            benchmark<OrderBookPQ>();
        } else if (std::string(argv[2]) == "C"){
            benchmark<OrderBookArray>();
        } else {
            std::cout << "Invalid arguments!" << std::endl;
            return 1;
        }
        return 0;
    }else {
        std::cout << "Invalid arguments! Usage: " << argv[0] << " [--verify | --impl A | --impl B | --impl C]\n";
        return 1;
    }
}