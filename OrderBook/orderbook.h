#ifndef ORDERBOOK_H
#define ORDERBOOK_H

#include "order.h"
#include "impl_a/orderbook_a.h"
#include "impl_b/orderbook_b.h"
#include "impl_c/orderbook_c.h"

// OrderBook defaults to OrderBookA
using OrderBook = OrderBookA;

#endif // ORDERBOOK_H
