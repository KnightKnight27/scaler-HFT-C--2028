#include <iostream>
#include <atomic>
#include <cstdint>

template <typename T>
class SmartPointer {
private:
    T* ptr;

public:
    explicit SmartPointer(T* p = nullptr) : ptr(p) {}

    // Disable copy semantics
    SmartPointer(const SmartPointer&) = delete;
    SmartPointer& operator=(const SmartPointer&) = delete;

    // Move semantics
    SmartPointer(SmartPointer&& other) noexcept : ptr(other.ptr) {
        other.ptr = nullptr;
    }

    SmartPointer& operator=(SmartPointer&& other) noexcept {
        if (this != &other) {
            delete ptr;
            ptr = other.ptr;
            other.ptr = nullptr;
        }
        return *this;
    }

    ~SmartPointer() {
        delete ptr;
    }

    T& operator*() const { return *ptr; }
    T* operator->() const { return ptr; }
};

template <typename T>
class SharedPtr {
private:
    T* ptr;
    std::atomic<uint32_t>* ref_count;

    void release_ref() {
        if (ref_count) {
            // Decrement with release semantics. If we hit 1 (meaning it will become 0),
            // we use acquire to ensure all prior writes are visible before deletion.
            if (ref_count->fetch_sub(1, std::memory_order_acq_rel) == 1) {
                delete ptr;
                delete ref_count;
            }
        }
    }

public:
    explicit SharedPtr(T* p = nullptr) {
        if (p) {
            ptr = p;
            ref_count = new std::atomic<uint32_t>(1);
        } else {
            ptr = nullptr;
            ref_count = nullptr;
        }
    }

    // Copy semantics
    SharedPtr(const SharedPtr& other) : ptr(other.ptr), ref_count(other.ref_count) {
        if (ref_count) {
            ref_count->fetch_add(1, std::memory_order_relaxed);
        }
    }

    SharedPtr& operator=(const SharedPtr& other) {
        if (this != &other) {
            release_ref();
            ptr = other.ptr;
            ref_count = other.ref_count;
            if (ref_count) {
                ref_count->fetch_add(1, std::memory_order_relaxed);
            }
        }
        return *this;
    }

    // Move semantics
    SharedPtr(SharedPtr&& other) noexcept : ptr(other.ptr), ref_count(other.ref_count) {
        other.ptr = nullptr;
        other.ref_count = nullptr;
    }

    SharedPtr& operator=(SharedPtr&& other) noexcept {
        if (this != &other) {
            release_ref();
            ptr = other.ptr;
            ref_count = other.ref_count;
            other.ptr = nullptr;
            other.ref_count = nullptr;
        }
        return *this;
    }

    ~SharedPtr() {
        release_ref();
    }

    T& operator*() const { return *ptr; }
    T* operator->() const { return ptr; }
    
    uint32_t use_count() const {
        return ref_count ? ref_count->load(std::memory_order_acquire) : 0;
    }
};

struct TestObj {
    int value;
    TestObj(int v) : value(v) {
        std::cout << "TestObj Created: " << value << "\n";
    }
    ~TestObj() {
        std::cout << "TestObj Destroyed: " << value << "\n";
    }
};

int main() {
    std::cout << "--- SmartPointer Test ---\n";
    SmartPointer<TestObj> sp1(new TestObj(10));
    std::cout << "sp1 value: " << sp1->value << "\n";
    SmartPointer<TestObj> sp2 = std::move(sp1);
    std::cout << "sp2 value: " << sp2->value << "\n";
    
    std::cout << "\n--- SharedPtr Test ---\n";
    {
        SharedPtr<TestObj> sh1(new TestObj(20));
        std::cout << "sh1 use count: " << sh1.use_count() << "\n";
        {
            SharedPtr<TestObj> sh2 = sh1;
            std::cout << "sh1 use count after copy: " << sh1.use_count() << "\n";
            std::cout << "sh2 value: " << sh2->value << "\n";
        }
        std::cout << "sh1 use count after sh2 goes out of scope: " << sh1.use_count() << "\n";
    }

    return 0;
}
