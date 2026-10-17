#include <iostream>
#include <vector>


enum class Side {Buy,Sell};

struct Order{
  unsigned long long id;
  Side side; //buy or sell
  long quantity; //will try to keep quantity without decimal points since it takes extra cpu cycles to have floating points, as well as its not very precise;
  int price; //same as above
  long timestampNs;
};

int main(){
  Order first{};
  first.id=1;
  first.side=Side::Buy;
  first.quantity=37;
  first.price=9950;

  Order second{};
  second.id = 2;
  second.side=Side::Sell;
  second.quantity=10;
  second.price=10200;


  std::vector<Order> orders;
  orders.push_back(first);
  orders.push_back(second);

  std::cout << "Orders stored: " << orders.size() << "\n";
  int bestBid = 0;
  int bestAsk = 15001;
  for (const Order& current : orders) {
    std::cout << current.id << "\n";
    if (current.side == Side::Buy && current.price>bestBid) {
      bestBid = current.price;
    }
    if (current.side == Side::Sell && current.price<bestAsk) {
      bestAsk = current.price;
    }
  }

  long bidQty = 0;
  long askQty = 0;
  std::cout << "Best Bid is: "<< bestBid << " & Best Ask is: " << bestAsk <<"\n";
  //why we cant write for(int i = 0, i<orders.size();i++)?




}