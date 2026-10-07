#include <cassert>

#include "spsc_queue.hpp"

struct Order {
    int id;
    int qty;
};

void test_single_capacity() {
    SPSCQueue<int, 1> q;
    int val = 0;

    assert(q.is_empty());
    assert(!q.is_full());
    assert(q.size() == 0);
    assert(q.capacity() == 1);
    assert(!q.pop(val));

    assert(q.push(42));
    assert(!q.is_empty());
    assert(q.is_full());
    assert(q.size() == 1);
    assert(!q.push(99));

    assert(q.pop(val));
    assert(val == 42);
    assert(q.is_empty());
    assert(!q.is_full());

    assert(q.push(84));
    assert(q.pop(val));
    assert(val == 84);
    assert(q.is_empty());
}

void test_wraparound_fifo() {
    SPSCQueue<int, 4> q;
    int val = 0;

    assert(q.push(1));
    assert(q.push(2));

    assert(q.pop(val));
    assert(val == 1);

    assert(q.push(3));
    assert(q.push(4));
    assert(q.push(5));

    assert(q.is_full());
    assert(q.size() == 4);
    assert(!q.push(6));

    assert(q.pop(val) && val == 2);
    assert(q.pop(val) && val == 3);
    assert(q.pop(val) && val == 4);
    assert(q.pop(val) && val == 5);

    assert(q.is_empty());
    assert(!q.pop(val));
}

void test_multi_cycle() {
    SPSCQueue<int, 8> q;
    int val = 0;

    for (int i = 0; i < 100; ++i) {
        assert(q.push(i));
        assert(q.pop(val));
        assert(val == i);
    }

    assert(q.is_empty());
    assert(q.size() == 0);
}

void test_struct_type() {
    SPSCQueue<Order, 3> q;
    Order out{};

    assert(q.push(Order{101, 500}));
    assert(q.push(Order{102, 1000}));

    assert(q.pop(out));
    assert(out.id == 101 && out.qty == 500);

    assert(q.push(Order{103, 1500}));
    assert(q.push(Order{104, 2000}));
    assert(q.is_full());

    assert(q.pop(out));
    assert(out.id == 102 && out.qty == 1000);

    assert(q.pop(out));
    assert(out.id == 103 && out.qty == 1500);

    assert(q.pop(out));
    assert(out.id == 104 && out.qty == 2000);

    assert(q.is_empty());
}

int main() {
    test_single_capacity();
    test_wraparound_fifo();
    test_multi_cycle();
    test_struct_type();

    return 0;
}
