#include <iostream>
#include "spsc_queue.hpp"

using namespace std;

int main() {
    cout << "Testing SPSC Queue with SpinLock..." << endl;
    test_spsc_queue<SpinLock>();

    cout << "Testing SPSC Queue with std::mutex..." << endl;
    test_spsc_queue<mutex>();

    return 0;
}
