#include "../common/OrderbookTypes.h"
#include "../include/Orderbook.h"
#include "../include/OrderbookOne.h"
#include "../include/OrderbookTwo.h"
#include "../include/OrderbookThree.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <random>
#include <vector>
#include <iostream>
#include <chrono>
#include <algorithm>

using u32 = std::uint32_t;
using u16 = std::uint16_t;
using u8 = std::uint8_t;
using Clock = std::chrono::steady_clock;


double to_ns(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<double, std::nano>(duration).count();
}

int main(int argc, char* argv[]){

    if (argc != 3) {
        throw std::invalid_argument("Incorrect number of arguments");
    }else if(std::string(argv[1])!="--impl"){
        throw std::invalid_argument("Incorrect arguments- Usage Example: ./bench --impl A");

    }


    

    std::mt19937 rng(42); // to genearte pseudo random price and quantity

    std::mt19937 side_rng(12345); // seperate rng used to generate pseudo random buy/sell
    std::bernoulli_distribution buy_dist(0.5);

    std::vector<double>add_order_times,cancel_order_times,get_bbo_times,get_bids_asks_times,total_durations;
    int checksum {}; // to ensure compiler doesnt optimize away stuff

    for(int times = 0;times<5;times++){

        std::unique_ptr<IOrderbook> book;
        if(std::string(argv[2])=="A"){
            book = std::make_unique<OrderbookOne>();
        }else if(std::string(argv[2])=="B"){
            book = std::make_unique<OrderbookTwo>();
        }else if(std::string(argv[2])=="C"){
            book = std::make_unique<OrderbookThree>();
        }else{
            throw std::invalid_argument("Incorrect arguments- Usage Example: ./bench --impl A");
        }

        u32 operations = 1e6;
    
        //making and pre populating buy/sell vector
        std::vector<Side> add_sides;
        add_sides.reserve(operations);
    
        for (u32 i = 0; i < operations; ++i) {
            add_sides.push_back(buy_dist(side_rng) ? Side::Buy : Side::Sell);
        }
    
        size_t side_idx = 0;
    
        std::vector<u32>order_ids; // to store added order ids for cancelling em
        order_ids.reserve(operations);
    
        u32 current_id {}; // not using mt19937 for order ids as it can have collisons and we need unique order ids
    
        Clock::duration add_order_time = Clock::duration::zero();
        Clock::duration get_bbo_time = Clock::duration::zero();
        Clock::duration cancel_order_time = Clock::duration::zero();
        Clock::duration get_bids_asks_time = Clock::duration::zero();
        u32 add_order_count {};
        u32 get_bbo_count {};
        u32 cancel_order_count {};
        u32 get_bids_asks_count {};
    
    
        auto total_start = Clock::now();
    
        while(operations--){
            if(operations%100==0){ // getting bids and ask every 100 operations
                get_bids_asks_count++;
    
    
                auto start = Clock::now();
    
                std::vector<Level> bids = book->get_bids();
                std::vector<Level> asks = book->get_asks();
    
                auto end = Clock::now();
                get_bids_asks_time+= end-start;
    
                checksum+=(bids.size()%2); // to prevent compiler from optimizing away this operation
                checksum+=(asks.size()%2);
            }
    
            u32 random_val = rng();
    
            if(operations%10==1){ // 10% get_bbo operations
                get_bbo_count++;
    
                auto start = Clock::now();
    
                BBO curr_bbo = book->get_bbo();
    
                auto end = Clock::now();
                get_bbo_time+=end-start;
    
                checksum+=curr_bbo.has_bid; // to prevent compiler from optimizing away this operation
    
            }else if(operations%10 == 2 || operations%10 == 3){ // 20% cancel operations
                if(order_ids.size()==0) continue; // no orders = no cancel
                cancel_order_count++;
    
                u32 cancel_idx = random_val%order_ids.size();
                //purposely let cancel_idx be index to an already cancelled order, to check that case
    
                auto start = Clock::now();
    
                bool cancelled = book->cancel_order(order_ids[cancel_idx]);
                
                auto end = Clock::now();
                cancel_order_time+=end-start;
    
            }else{// 70% add operations

                add_order_count++;
                Side side = add_sides[side_idx++];

                u16 buy_value = ((random_val % 5000) + 5000); // range = [5000,9999]
                u16 sell_value = (random_val % 5000) + 10001; // range = [10001,15000]
                u8 quantity = ((random_val+operations) %100) +1; // range = [1,100]
                
                //made this section branchless, as it is hard to predict Side
                u16 arr[2] = {buy_value,sell_value};
                u16 value = arr[(side==Side::Sell)];
    
                auto start = Clock::now();
                
                book->add_order(current_id, side, value, quantity);
                
                auto end = Clock::now();

                add_order_time+=end-start;
                
                order_ids.push_back(current_id++); // add order id in vector to support cancellation
    
            }
    
    
        }
    
        auto total_end = Clock::now();
    
        total_durations.push_back(to_ns(total_end-total_start));
        add_order_times.push_back(to_ns(add_order_time)/add_order_count); 
        cancel_order_times.push_back(to_ns(cancel_order_time)/cancel_order_count); 
        get_bbo_times.push_back(to_ns(get_bbo_time)/get_bbo_count); 
        get_bids_asks_times.push_back(to_ns(get_bids_asks_time)/get_bids_asks_count); 

    }

    sort(total_durations.begin(),total_durations.end());
    sort(add_order_times.begin(),add_order_times.end());
    sort(cancel_order_times.begin(),cancel_order_times.end());
    sort(get_bbo_times.begin(),get_bbo_times.end());
    sort(get_bids_asks_times.begin(),get_bids_asks_times.end());




    std::cout<<"Median Total execution time = "<<total_durations[2]<<"ns"<<std::endl<<std::endl;
    std::cout<<"Median Average time per add order = "<<add_order_times[2]<<"ns"<<std::endl;
    std::cout<<"Median Average time per cancel order = "<<cancel_order_times[2]<<"ns"<<std::endl;
    std::cout<<"Median Average time per get BBO = "<<get_bbo_times[2]<<"ns"<<std::endl;
    std::cout<<"Median Average time per get bids and asks = "<<get_bids_asks_times[2]<<"ns"<<std::endl;



    std::cout<<"checksum = "<<checksum<<std::endl;

    

}