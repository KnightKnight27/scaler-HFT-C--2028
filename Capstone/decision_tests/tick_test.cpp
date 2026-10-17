#include "types.hpp"
#include <cstdio>

int main() {
    double prices[] = {50.00, 50.29, 50.57, 50.83, 99.99, 100.01, 150.00};
    for (double p : prices) {
        int bad   = static_cast<int>(p * 100);
        int good  = ob::to_tick(p);
        std::printf("%.2f  trunc=%d  to_tick=%d  %s\n",
                    p, bad, good, bad == good ? "ok" : "<-- truncation bug");
    }
}