#include <iostream>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdint>
#include <cassert>
#include <string>
#include <utility>

// ============================================================================
// INSTRUMENT DATA STRUCT (FOR VERIFICATION AND ZERO-LEAK AUDITING)
// ============================================================================
struct MarketDataRecord {
    inline static std::atomic<int64_t> live_instances{0};

    uint64_t order_id;
    std::string symbol;
    double price;
    uint32_t qty;

    MarketDataRecord(uint64_t id, std::string sym, double p, uint32_t q)
        : order_id(id), symbol(std::move(sym)), price(p), qty(q) {
        live_instances.fetch_add(1, std::memory_order_relaxed);
    }

    ~MarketDataRecord() {
        live_instances.fetch_sub(1, std::memory_order_relaxed);
    }
};

// ============================================================================
// 1. SCOPED MOVE-ONLY SMART POINTER (SmartPointer<T>)
// ============================================================================
// Ultra-low latency scoped pointer (similar to std::unique_ptr):
// - Strict single-ownership semantics.
// - Copy constructor and copy assignment are strictly deleted.
// - Supports efficient move semantics, operator*, and operator-> with zero overhead.
template <typename T>
class SmartPointer {
public:
    constexpr SmartPointer() noexcept : ptr_(nullptr) {}

    explicit SmartPointer(T* ptr) noexcept : ptr_(ptr) {}

    ~SmartPointer() {
        delete ptr_;
    }

    // Copy semantics explicitly disabled
    SmartPointer(const SmartPointer&) = delete;
    SmartPointer& operator=(const SmartPointer&) = delete;

    // Move constructor: transfers ownership and nullifies source pointer
    SmartPointer(SmartPointer&& other) noexcept : ptr_(other.release()) {}

    // Move assignment: transfers ownership, deallocates existing resource
    SmartPointer& operator=(SmartPointer&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    // Dereference operators
    T& operator*() const noexcept {
        return *ptr_;
    }

    T* operator->() const noexcept {
        return ptr_;
    }

    T* get() const noexcept {
        return ptr_;
    }

    explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }

    // Releases ownership without deallocating
    T* release() noexcept {
        T* old_ptr = ptr_;
        ptr_ = nullptr;
        return old_ptr;
    }

    // Deallocates current resource and manages new resource
    void reset(T* new_ptr = nullptr) noexcept {
        if (ptr_ != new_ptr) {
            delete ptr_;
            ptr_ = new_ptr;
        }
    }

    void swap(SmartPointer& other) noexcept {
        std::swap(ptr_, other.ptr_);
    }

private:
    T* ptr_{nullptr};
};

// ============================================================================
// 2. THREAD-SAFE REFERENCE-COUNTED POINTER (SharedPtr<T>)
// ============================================================================
// Thread-safe reference counting with std::atomic<uint32_t>:
// - Atomic increment on copy: relaxed memory order is safe since ownership
//   already guarantees lifetime of the object.
// - Atomic decrement on destruction: acquire-release semantics (fetch_sub with
//   acq_rel or release + acquire fence) ensures all modifications through any
//   SharedPtr instance happen-before the destructor of the managed object runs.
// - Automatically frees heap memory when reference counter reaches zero.
template <typename T>
class SharedPtr {
public:
    constexpr SharedPtr() noexcept : ptr_(nullptr), ref_count_(nullptr) {}

    explicit SharedPtr(T* ptr) {
        if (ptr) {
            ptr_ = ptr;
            ref_count_ = new std::atomic<uint32_t>(1);
        } else {
            ptr_ = nullptr;
            ref_count_ = nullptr;
        }
    }

    ~SharedPtr() {
        release_ref();
    }

    // Copy constructor: increments atomic reference count
    SharedPtr(const SharedPtr& other) noexcept
        : ptr_(other.ptr_), ref_count_(other.ref_count_) {
        if (ref_count_) {
            ref_count_->fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Copy assignment: releases previous reference and retains new one
    SharedPtr& operator=(const SharedPtr& other) noexcept {
        if (this != &other) {
            release_ref();
            ptr_ = other.ptr_;
            ref_count_ = other.ref_count_;
            if (ref_count_) {
                ref_count_->fetch_add(1, std::memory_order_relaxed);
            }
        }
        return *this;
    }

    // Move constructor: zero-overhead transfer without touching atomic count
    SharedPtr(SharedPtr&& other) noexcept
        : ptr_(other.ptr_), ref_count_(other.ref_count_) {
        other.ptr_ = nullptr;
        other.ref_count_ = nullptr;
    }

    // Move assignment: releases existing reference and moves pointers
    SharedPtr& operator=(SharedPtr&& other) noexcept {
        if (this != &other) {
            release_ref();
            ptr_ = other.ptr_;
            ref_count_ = other.ref_count_;
            other.ptr_ = nullptr;
            other.ref_count_ = nullptr;
        }
        return *this;
    }

    T& operator*() const noexcept {
        return *ptr_;
    }

    T* operator->() const noexcept {
        return ptr_;
    }

    T* get() const noexcept {
        return ptr_;
    }

    explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }

    uint32_t use_count() const noexcept {
        return ref_count_ ? ref_count_->load(std::memory_order_acquire) : 0;
    }

    void reset(T* new_ptr = nullptr) {
        if (ptr_ != new_ptr) {
            release_ref();
            if (new_ptr) {
                ptr_ = new_ptr;
                ref_count_ = new std::atomic<uint32_t>(1);
            }
        }
    }

private:
    void release_ref() noexcept {
        if (ref_count_) {
            // Decrement with acq_rel semantics:
            // - release: guarantees all memory writes through this reference
            //   are published prior to decrementing.
            // - acquire: guarantees that the thread that drops refcount to 0
            //   observes all writes made by other threads before deleting.
            if (ref_count_->fetch_sub(1, std::memory_order_acq_rel) == 1) {
                delete ptr_;
                delete ref_count_;
            }
            ptr_ = nullptr;
            ref_count_ = nullptr;
        }
    }

    T* ptr_{nullptr};
    std::atomic<uint32_t>* ref_count_{nullptr};
};

// ============================================================================
// 3. DEMONSTRATION & VERIFICATION SUITE
// ============================================================================
void demonstrate_smart_pointer() {
    std::cout << "------------------------------------------------------------\n";
    std::cout << " [1] DEMONSTRATING SmartPointer<T> (Move-Only Scoped Pointer)\n";
    std::cout << "------------------------------------------------------------\n";

    assert(MarketDataRecord::live_instances.load() == 0);

    {
        // Allocation and dereferencing
        SmartPointer<MarketDataRecord> p1(new MarketDataRecord(101, "AAPL", 185.50, 500));
        std::cout << "[*] Created SmartPointer p1: " << p1->symbol
                  << " @ $" << p1->price << " (Qty: " << p1->qty << ")\n";
        assert(MarketDataRecord::live_instances.load() == 1);

        // Move construction
        SmartPointer<MarketDataRecord> p2(std::move(p1));
        std::cout << "[*] Moved p1 -> p2. p1 is null? " << (p1.get() == nullptr ? "true" : "false")
                  << ", p2 points to: " << p2->symbol << "\n";
        assert(!p1);
        assert(p2);
        assert(MarketDataRecord::live_instances.load() == 1);

        // Move assignment
        SmartPointer<MarketDataRecord> p3;
        p3 = std::move(p2);
        std::cout << "[*] Moved p2 -> p3. p2 is null? " << (p2.get() == nullptr ? "true" : "false")
                  << ", p3 points to: " << p3->symbol << "\n";
        assert(!p2);
        assert(p3);
        assert(MarketDataRecord::live_instances.load() == 1);

        // Mutating through operator->
        p3->price = 186.25;
        std::cout << "[*] Updated p3 price via operator->: $" << (*p3).price << "\n";
    } // p3 goes out of scope here -> auto deleted

    assert(MarketDataRecord::live_instances.load() == 0);
    std::cout << "[+] SmartPointer cleanup verified: Live instances = "
              << MarketDataRecord::live_instances.load() << "\n\n";
}

void demonstrate_shared_ptr() {
    std::cout << "------------------------------------------------------------\n";
    std::cout << " [2] DEMONSTRATING SharedPtr<T> (Atomic Reference Counting) \n";
    std::cout << "------------------------------------------------------------\n";

    assert(MarketDataRecord::live_instances.load() == 0);

    {
        SharedPtr<MarketDataRecord> root(new MarketDataRecord(202, "NVDA", 124.80, 1000));
        std::cout << "[*] Created SharedPtr root. Initial use_count: " << root.use_count() << "\n";
        assert(root.use_count() == 1);
        assert(MarketDataRecord::live_instances.load() == 1);

        {
            SharedPtr<MarketDataRecord> copy1 = root;
            std::cout << "[*] Copy1 created. use_count: " << root.use_count() << "\n";
            assert(root.use_count() == 2);

            {
                SharedPtr<MarketDataRecord> copy2 = copy1;
                std::cout << "[*] Copy2 created. use_count: " << root.use_count() << "\n";
                assert(root.use_count() == 3);
            }
            std::cout << "[*] Copy2 destroyed. use_count: " << root.use_count() << "\n";
            assert(root.use_count() == 2);
        }
        std::cout << "[*] Copy1 destroyed. use_count: " << root.use_count() << "\n";
        assert(root.use_count() == 1);

        // Multi-threaded stress test with concurrent copying and destructions
        constexpr size_t NUM_THREADS = 8;
        constexpr size_t ITERATIONS_PER_THREAD = 50000;
        std::cout << "[*] Spawning " << NUM_THREADS << " threads sharing root pointer ("
                  << ITERATIONS_PER_THREAD << " copies each)...\n";

        std::vector<std::thread> workers;
        workers.reserve(NUM_THREADS);

        for (size_t t = 0; t < NUM_THREADS; ++t) {
            workers.emplace_back([root, ITERATIONS_PER_THREAD]() {
                for (size_t i = 0; i < ITERATIONS_PER_THREAD; ++i) {
                    SharedPtr<MarketDataRecord> local_copy = root;
                    assert(local_copy->order_id == 202);
                }
            });
        }

        for (auto& w : workers) {
            w.join();
        }

        std::cout << "[*] All worker threads joined.\n";
        std::cout << "[*] Final root use_count: " << root.use_count() << "\n";
        assert(root.use_count() == 1);
        assert(MarketDataRecord::live_instances.load() == 1);
    } // root goes out of scope here -> refcount hits 0 -> deleted

    assert(MarketDataRecord::live_instances.load() == 0);
    std::cout << "[+] SharedPtr cleanup verified: Live instances = "
              << MarketDataRecord::live_instances.load() << "\n\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << " CUSTOM SMART POINTER IMPLEMENTATION & CONCURRENCY TEST     \n";
    std::cout << "============================================================\n\n";

    demonstrate_smart_pointer();
    demonstrate_shared_ptr();

    std::cout << "============================================================\n";
    std::cout << " ALL SMART POINTER TESTS PASSED SUCCESSFULLY! ZERO LEAKS.   \n";
    std::cout << "============================================================\n";
    return 0;
}
