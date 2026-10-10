
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <cstdint>

struct Item {
    std::uint64_t id;
    char data[56];
};

static_assert(sizeof(Item) == 64, "Item must be 64 bytes");

template <typename T>
class SPSC{
public:
    SPSC(std::size_t size)
        : mSize(size), mData(size){}

    bool push(const T& val){
        std::lock_guard<std::mutex> lock(mMutex);

        if(mCount == mSize)
            return false;

        mData[mPushIdx] = val;
        mPushIdx = (mPushIdx+1)%mSize;
        ++mCount;

        return true;
    }

    bool pop(T& val){
        std::lock_guard<std::mutex> lock(mMutex);

        if(mCount == 0)
            return false;

        val = mData[mPopIdx];
        mPopIdx = (mPopIdx+1)%mSize;
        --mCount;

        return true;
    }

private:
    std::size_t mSize;
    std::vector<T> mData;
    std::size_t mPushIdx = 0;
    std::size_t mPopIdx = 0;
    std::size_t mCount = 0;
    std::mutex mMutex;
};

int main(){
    const std::size_t totalItems = 5000000;
    SPSC<Item> q(1024);

    std::uint64_t checksum = 0;
    std::size_t consumed = 0;
    bool correct = true;

    auto start = std::chrono::steady_clock::now();

    std::thread producer([&](){
        for(std::size_t i=0; i<totalItems; i++){
            Item item{};
            item.id = i;

            while(!q.push(item)){
            }
        }
    });

    std::thread consumer([&](){
        Item item{};

        while(consumed < totalItems){
            if(q.pop(item)){
                if(item.id != consumed)
                    correct = false;

                checksum += item.id;
                consumed++;
            }
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();

    std::cout << "Object size: " << sizeof(Item) << " bytes\n";
    std::cout << "Items transferred: " << consumed << '\n';
    std::cout << "Time taken: " << seconds << " seconds\n";
    std::cout << "Items per second: " << consumed / seconds << '\n';
    std::cout << "Combined push/pop operations per second: " << (2.0 * consumed) / seconds << '\n';
    std::cout << "Checksum: " << checksum << '\n';

    if(!correct || consumed != totalItems){
        std::cerr << "Queue test failed\n";
        return 1;
    }

    return 0;
}
