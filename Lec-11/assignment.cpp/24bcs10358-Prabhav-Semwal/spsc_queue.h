#pragma once

#include <cstdint>
#include <mutex>

struct Message {
    std::uint64_t data[8]{};
};

static_assert(sizeof(Message) == 64);

class SPSCQueue {
    Message buffer[1024];
    int head = 0, tail = 0, count = 0;
    std::mutex mtx;

public:
    bool push(const Message& message);
    bool pop(Message& message);
};
