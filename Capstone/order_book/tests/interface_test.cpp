#include "order_book.h"

int main() {
    BBO result;

    if (result.has_bid || result.has_ask) {
        return 1;
    }

    Level level{99.98, 10};

    if (level.qty != 10) {
        return 1;
    }

    return 0;
}
