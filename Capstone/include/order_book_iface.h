// order_book_iface.h — the logical interface every implementation must provide.
//
// There is deliberately no virtual base class. The benchmark and verifier are
// templates on the book type, so calls in the timed loop are direct and
// inlineable. A virtual call would add the same overhead to every implementation
// and blur the comparison.
//
// C++17 has no concepts, so conformance is checked at compile time instead: each
// book header ends with
//     static_assert(implements_order_book<BookX>(), "");
//
// Beyond the assignment API, every book also provides
//     bool check_invariants() const;
// It is a full O(N) consistency check of the book's internal structures, called only
// by the tests and the verifier, never in the timed path.
//
// Preconditions (asserted in debug builds, not checked in release / timed builds):
//   - add_order: id is new (never seen before), price lies in the side's band
//     (bids 50.00..99.99, asks 100.01..150.00), qty >= 1.
//   - get_bids / get_asks: depth >= 0.
#pragma once

#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include "types.h"

template <typename Book>
constexpr bool implements_order_book() {
    using std::declval;
    using std::is_same_v;

    static_assert(is_same_v<decltype(declval<Book&>().add_order(uint64_t{}, Side{}, double{}, long{})), void>,
                  "add_order(uint64_t, Side, double, long) -> void");
    static_assert(is_same_v<decltype(declval<Book&>().cancel_order(uint64_t{})), bool>,
                  "cancel_order(uint64_t) -> bool");

    // Queries must be callable on a const book.
    static_assert(is_same_v<decltype(declval<const Book&>().get_bbo()), BBO>,
                  "get_bbo() const -> BBO");
    static_assert(is_same_v<decltype(declval<const Book&>().get_bids(int{})), std::vector<Level>>,
                  "get_bids(int) const -> std::vector<Level>");
    static_assert(is_same_v<decltype(declval<const Book&>().get_asks(int{})), std::vector<Level>>,
                  "get_asks(int) const -> std::vector<Level>");
    static_assert(is_same_v<decltype(declval<const Book&>().check_invariants()), bool>,
                  "check_invariants() const -> bool");
    return true;
}
