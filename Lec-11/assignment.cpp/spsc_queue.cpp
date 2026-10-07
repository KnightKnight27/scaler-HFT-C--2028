// WRITE AN SPSC QUEUE 
// SPINLOCK ( WHILE LOOP) OR STD::MUTEX 
// t1.join()  t2.join()
// producer consumer to push objects and pop objects 
//
// you need to figure out a way that with locks how many 
// 64 byte objects can u push and pop in 1 second
//  raise a git PR for the same 
//  add readme for ur per second specs 
//  feel free to add worst code qaulity :)
//
//
// ^^ MEMORY POOL ^^

#include <cstddef>
#include <iostream>
#include <mutex>
#include <new>

template <typename T>
class SPSC{
    public:
        // Constructor
        SPSC(std::size_t size) : mSize(size){
            // Allocating raw memory
            mData = static_cast<T*>(::operator new(sizeof(T) * size));
        }

        // Destructor
        ~SPSC(){
            ::operator delete(mData);
        }

        // No copying
        SPSC(const SPSC<T>&) = delete;
        SPSC(SPSC&&) = delete;

        // Push
        bool push(T val){
            std::lock_guard<std::mutex> lock(mMutex);

            if(size() == mSize){
                return false;
            }

            // Since we are using raw memory
            // We have to construct a T object with val at the specified memory address
            new (&mData[mPushIdx % mSize]) T(val);

            ++mPushIdx;
            return true
        }

    private:
        // Number of elements in the queue
        std::size_t size() const{
            return mPushIdx - mPopIdx;
        }

        T* mData{nullptr};  // Pointer to memory where queue objects will live 

        std::size_t mSize{0};         // Queue Capacity
        std::size_t mPushIdx{0};      // Where the next object should be pushed
        std::size_t mPopIdx{0};       // Where the next object should be popped

        std::mutex mMutex;
};