#include <iostream>

volatile long counter = 0;

void sendOrder() {
    counter = counter + 1;
}

int main() {

    int num_of_orders = 1000'000'00;

    for (int i = 0; i < num_of_orders; i++) {
        sendOrder();
    }

    return 0;
}
