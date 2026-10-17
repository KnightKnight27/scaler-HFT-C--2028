# First implementation

map<double, long, greater<double>> bids;
where key is the price and value is the total quantity

map<double, long, less<double>> asks;

unordered_map<uint64_t, Order> orders;

Order {
    Type type;
    double price;
    long qty;
}

For adding an order, insert it in orders hashmap and then find the order in bids/asks and update the qty
For cancelling an order, find it in orders hashmap, decrement from quantity in bids/asks and remove the entry from orders hashmap
Getting BBO and top bid/ask levels is trivial

# Second implementation

priority_queue<Order*> bids;
priority_queue<Order*> asks;

unordered_map<uint64_t, pair<Order*, long>> orders;
unordered_map<double, Order*> priceOrderMap;

In the orders map, the key is the id and value is a pair. The first value is the pointer to the Order and the second value is the original order quantity of the order.
priceOrderMap is a hashmap with id being the price and the value is the pointer to the Order.

For insertion, check if an entry for the same price already exists in the priceOrderMap, if yes then simply get the Order* from that map and update the quantity.
Otherwise, create a new heap allocated Order, add it in the orders hashmap, add it in priceOrderMap and push it into bids/asks priorityQueue

For cancellation, locate the order pointer and the original quantity by id from orders hashmap and simply decrement the quantity from the Order*
Remove the entry from orders.

When getting the top BBO or top K bid/ask levels, check if the quantity of the item that you fetch from the priority queue is 0, if it is 0, find the entry in priceOrderMap and delete it, free the Order* and pop & ignore the Order from the heap.

# Third implementation

std::unordered_map<uint64_t, Order*> orders;

std::list<Order*> bids;
std::list<Order*> asks;

For insertion of an order, insert it in the orders hashmap, find the correct sorted position in the bids/asks linkedlist and insert it there as well.
For cancellation, find the order from the orders hashmap, delete it from bids/asks linked lists.

top BB0, get K bid/ask are trivial