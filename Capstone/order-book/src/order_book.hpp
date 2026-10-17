#pragma once

// All three books provide the same public methods.
#include "array_order_book.hpp"
#include "map_order_book.hpp"
#include "sorted_vector_order_book.hpp"

// C was fastest overall in the saved benchmarks. See REPORT.md for limitations.
using OrderBook = ArrayOrderBook;
