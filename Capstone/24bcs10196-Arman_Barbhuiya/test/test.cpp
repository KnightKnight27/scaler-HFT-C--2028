
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
#include <algorithm>
#include <fstream>
#include <filesystem>

using u32 = std::uint32_t;
using u16 = std::uint16_t;
using u8 = std::uint8_t;

int main(){

    std::filesystem::create_directories("./output");

    for(char impl : {'A', 'B', 'C'}){

        std::unique_ptr<IOrderbook> book;
        if(impl=='A'){
            book = std::make_unique<OrderbookOne>();
        }else if(impl=='B'){
            book = std::make_unique<OrderbookTwo>();
        }else if(impl=='C'){
            book = std::make_unique<OrderbookThree>();
        }else{
            throw std::invalid_argument("Incorrect implementation");
        }

        std::ofstream out("./output/impl_" + std::string(1,impl) + ".txt");
        if(!out){
            throw std::runtime_error("Could not open output file");
        }

        std::mt19937 rng(42); // to genearte pseudo random price and quantity

        std::mt19937 side_rng(12345); // seperate rng used to generate pseudo random buy/sell
        std::bernoulli_distribution buy_dist(0.5);

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

        u32 add_order_count {};
        u32 get_bbo_count {};
        u32 cancel_order_count {};
        u32 get_bids_asks_count {};

        while(operations--){
            u32 step = 999999 - operations;

            if(operations%100==0){ // getting bids and ask every 100 operations
                get_bids_asks_count++;

                out<<"SNAPSHOT "<<step<<'\n';

                std::vector<Level> bids = book->get_bids();
                std::vector<Level> asks = book->get_asks();

                out<<"BIDS "<<bids.size();
                for(auto[p,q]:bids){
                    out<<' '<<p<<':'<<q;
                }
                out<<'\n';

                out<<"ASKS "<<asks.size();
                for(auto[p,q]:asks){
                    out<<' '<<p<<':'<<q;
                }
                out<<'\n';
            }

            u32 random_val = rng();

            if(operations%10==1){ // 10% get_bbo operations
                get_bbo_count++;

                BBO curr_bbo = book->get_bbo();

                out<<"BBO "<<step<<' '
                   <<curr_bbo.has_bid<<' '
                   <<curr_bbo.bid_price<<' '
                   <<curr_bbo.bid_qty<<' '
                   <<curr_bbo.has_ask<<' '
                   <<curr_bbo.ask_price<<' '
                   <<curr_bbo.ask_qty<<'\n';

            }else if(operations%10 == 2 || operations%10 == 3){ // 20% cancel operations
                if(order_ids.size()==0){
                    out<<"CANCEL_SKIPPED "<<step<<'\n';
                    continue; // no orders = no cancel
                }

                cancel_order_count++;

                u32 cancel_idx = random_val%order_ids.size();
                //purposely let cancel_idx be index to an already cancelled order, to check that case

                bool cancelled = book->cancel_order(order_ids[cancel_idx]);

                out<<"CANCEL "<<step<<' '
                   <<order_ids[cancel_idx]<<' '
                   <<cancelled<<'\n';

            }else{// 70% add operations

                add_order_count++;
                Side side = add_sides[side_idx++];

                u16 buy_value = ((random_val % 5000) + 5000); // range = [5000,9999]
                u16 sell_value = (random_val % 5000) + 10001; // range = [10001,15000]
                u8 quantity = ((random_val+operations) %100) +1; // range = [1,100]

                //made this section branchless, as it is hard to predict Side
                u16 arr[2] = {buy_value,sell_value};
                u16 value = arr[(side==Side::Sell)];

                book->add_order(current_id, side, value, quantity);

                out<<"ADD "<<step<<' '
                   <<current_id<<' '
                   <<(side==Side::Buy ? "BUY" : "SELL")<<' '
                   <<value<<' '
                   <<static_cast<unsigned>(quantity)<<'\n';

                order_ids.push_back(current_id++); // to store added order ids for cancelling em
            }
        }

        out<<"COUNTS\n";
        out<<"add_order_count "<<add_order_count<<'\n';
        out<<"cancel_order_count "<<cancel_order_count<<'\n';
        out<<"get_bbo_count "<<get_bbo_count<<'\n';
        out<<"get_bids_asks_count "<<get_bids_asks_count<<'\n';

        out.close();

        std::cout<<"Finished implementation "<<impl<<'\n';
    }

    auto files_equal = [](const std::string& a, const std::string& b){
        std::ifstream fa(a, std::ios::binary);
        std::ifstream fb(b, std::ios::binary);

        if(!fa || !fb){
            throw std::runtime_error("Could not open files for comparison");
        }

        return std::equal(
            std::istreambuf_iterator<char>(fa),
            std::istreambuf_iterator<char>(),
            std::istreambuf_iterator<char>(fb),
            std::istreambuf_iterator<char>()
        );
    };

    bool ab = files_equal("./output/impl_A.txt", "./output/impl_B.txt");
    bool ac = files_equal("./output/impl_A.txt", "./output/impl_C.txt");
    bool bc = files_equal("./output/impl_B.txt", "./output/impl_C.txt");

    std::cout<<"\n========== VERIFICATION ==========\n";
    std::cout<<"A == B: "<<(ab ? "PASS" : "FAIL")<<'\n';
    std::cout<<"A == C: "<<(ac ? "PASS" : "FAIL")<<'\n';
    std::cout<<"B == C: "<<(bc ? "PASS" : "FAIL")<<'\n';

    if(ab && ac && bc){
        std::cout<<"All implementations match!\n";
        return 0;
    }

    std::cout<<"Mismatch detected. Compare the output files.\n";
    return 1;
}