#include <iostream>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdint>
#include <cassert>
#include <chrono>
#include <numeric>
#include <iomanip>

// ============================================================================
// GLOBAL ALLOCATION COUNTER (Zero-Leak Verification)
// ============================================================================
inline std::atomic<int64_t> g_active_node_allocations{0};

// Helper function for x86 CPU pause instruction
inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
    __asm__ __volatile__("yield" ::: "memory");
#endif
}

// ============================================================================
// LOCK-FREE TREIBER STACK WITH SAFE DEFERRED NODE RECLAMATION
// ============================================================================
template <typename T>
class LockFreeStack {
private:
    struct Node {
        T data;
        Node* next{nullptr};

        explicit Node(const T& val) : data(val), next(nullptr) {
            g_active_node_allocations.fetch_add(1, std::memory_order_relaxed);
        }

        ~Node() {
            g_active_node_allocations.fetch_sub(1, std::memory_order_relaxed);
        }
    };

    // Cache-line aligned top-of-stack atomic pointer
    alignas(64) std::atomic<Node*> head_{nullptr};

    // Safe hazard/deferred reclamation infrastructure (Anthony Williams pattern)
    // Prevents ABA use-after-free without heavy external garbage collection
    alignas(64) std::atomic<unsigned> threads_in_pop_{0};
    alignas(64) std::atomic<Node*> to_be_deleted_{nullptr};

    static void delete_nodes(Node* nodes) {
        while (nodes) {
            Node* next = nodes->next;
            delete nodes;
            nodes = next;
        }
    }

    void chain_pending_nodes(Node* first, Node* last) {
        last->next = to_be_deleted_.load(std::memory_order_relaxed);
        while (!to_be_deleted_.compare_exchange_weak(last->next, first,
                                                     std::memory_order_release,
                                                     std::memory_order_relaxed)) {
            cpu_relax();
        }
    }

    void chain_pending_nodes(Node* nodes) {
        if (!nodes) return;
        Node* last = nodes;
        while (last->next) {
            last = last->next;
        }
        chain_pending_nodes(nodes, last);
    }

    void chain_pending_node(Node* node) {
        node->next = nullptr;
        chain_pending_nodes(node, node);
    }

    void try_reclaim(Node* old_head) {
        // If this thread is the solitary thread active in pop(), safe to reclaim
        if (threads_in_pop_.load(std::memory_order_relaxed) == 1) {
            Node* claim_list = to_be_deleted_.exchange(nullptr, std::memory_order_acq_rel);
            if (--threads_in_pop_ == 0) {
                // Still only thread: safely delete all accumulated deferred nodes
                delete_nodes(claim_list);
            } else if (claim_list) {
                // Another thread concurrently entered pop(): re-chain deferred nodes
                chain_pending_nodes(claim_list);
            }
            delete old_head;
        } else {
            // Other threads are concurrently reading old_head->next: defer deletion
            chain_pending_node(old_head);
            if (--threads_in_pop_ == 0) {
                // If we dropped the count to 0, attempt to drain deferred list
                Node* claim_list = to_be_deleted_.exchange(nullptr, std::memory_order_acq_rel);
                if (claim_list) {
                    if (threads_in_pop_.load(std::memory_order_relaxed) == 0) {
                        delete_nodes(claim_list);
                    } else {
                        chain_pending_nodes(claim_list);
                    }
                }
            }
        }
    }

public:
    LockFreeStack() = default;

    ~LockFreeStack() {
        // Drain active nodes
        T val;
        while (pop(val)) {}

        // Drain deferred pending nodes
        delete_nodes(to_be_deleted_.exchange(nullptr, std::memory_order_acq_rel));
    }

    // Disable copy semantics
    LockFreeStack(const LockFreeStack&) = delete;
    LockFreeStack& operator=(const LockFreeStack&) = delete;

    // ------------------------------------------------------------------------
    // PUSH: CAS loop with compare_exchange_weak (release / relaxed)
    // ------------------------------------------------------------------------
    // - Success (release): Ensures data written into new_node is published
    //   and visible before any other thread observes the updated head_.
    // - Failure (relaxed): Reloads current head into new_node->next on CAS failure.
    void push(const T& val) {
        Node* new_node = new Node(val);
        new_node->next = head_.load(std::memory_order_relaxed);

        while (!head_.compare_exchange_weak(new_node->next, new_node,
                                            std::memory_order_release,
                                            std::memory_order_relaxed)) {
            cpu_relax();
        }
    }

    // ------------------------------------------------------------------------
    // POP: CAS loop with compare_exchange_weak (acquire / relaxed)
    // ------------------------------------------------------------------------
    // - Success (acquire): Synchronizes with push's release order, ensuring
    //   old_head->data is fully visible before reading.
    // - Failure (relaxed): Reloads current head into old_head on CAS failure.
    // - Safe node deallocation ensures zero leaks and prevents use-after-free.
    bool pop(T& val) {
        ++threads_in_pop_;
        Node* old_head = head_.load(std::memory_order_relaxed);

        while (old_head && !head_.compare_exchange_weak(old_head, old_head->next,
                                                        std::memory_order_acquire,
                                                        std::memory_order_relaxed)) {
            cpu_relax();
        }

        if (!old_head) {
            --threads_in_pop_;
            return false; // Stack empty
        }

        val = old_head->data;
        try_reclaim(old_head);
        return true;
    }

    bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == nullptr;
    }

    // Force reclaim of any remaining deferred nodes when threads are quiescent
    void reclaim_quiescent() {
        if (threads_in_pop_.load(std::memory_order_relaxed) == 0) {
            delete_nodes(to_be_deleted_.exchange(nullptr, std::memory_order_acq_rel));
        }
    }
};

// ============================================================================
// MULTI-THREADED TEST HARNESS
// ============================================================================
void run_multithreaded_test() {
    std::cout << "============================================================\n";
    std::cout << " LOCK-FREE TREIBER STACK MULTI-THREADED VERIFICATION       \n";
    std::cout << "============================================================\n";

    constexpr size_t NUM_PRODUCERS = 4;
    constexpr size_t NUM_CONSUMERS = 4;
    constexpr size_t OPS_PER_PRODUCER = 100000;
    constexpr size_t TOTAL_OPS = NUM_PRODUCERS * OPS_PER_PRODUCER;

    std::cout << " Producers          : " << NUM_PRODUCERS << " threads\n";
    std::cout << " Consumers          : " << NUM_CONSUMERS << " threads\n";
    std::cout << " Items Per Producer : " << OPS_PER_PRODUCER << "\n";
    std::cout << " Total Items Pushed : " << TOTAL_OPS << "\n\n";

    double duration_sec = 0.0;
    uint64_t total_popped = 0;

    {
        LockFreeStack<uint64_t> stack;
        std::atomic<bool> start_flag{false};
        std::atomic<bool> producers_done{false};
        std::atomic<size_t> active_producers{NUM_PRODUCERS};

        std::vector<std::thread> producers;
        std::vector<std::thread> consumers;

        std::vector<uint64_t> consumer_counts(NUM_CONSUMERS, 0);
        std::vector<uint64_t> consumer_sums(NUM_CONSUMERS, 0);

        const auto start_time = std::chrono::high_resolution_clock::now();

        // Spawn Producers
        for (size_t p = 0; p < NUM_PRODUCERS; ++p) {
            producers.emplace_back([&, p]() {
                while (!start_flag.load(std::memory_order_acquire)) {
                    cpu_relax();
                }

                const uint64_t base_val = (p + 1) * 10000000ULL;
                for (size_t i = 1; i <= OPS_PER_PRODUCER; ++i) {
                    stack.push(base_val + i);
                }

                if (--active_producers == 0) {
                    producers_done.store(true, std::memory_order_release);
                }
            });
        }

        // Spawn Consumers
        for (size_t c = 0; c < NUM_CONSUMERS; ++c) {
            consumers.emplace_back([&, c]() {
                while (!start_flag.load(std::memory_order_acquire)) {
                    cpu_relax();
                }

                uint64_t val = 0;
                while (!producers_done.load(std::memory_order_acquire) || !stack.empty()) {
                    if (stack.pop(val)) {
                        ++consumer_counts[c];
                        consumer_sums[c] += val;
                    } else {
                        cpu_relax();
                    }
                }
                // Drain any lingering items
                while (stack.pop(val)) {
                    ++consumer_counts[c];
                    consumer_sums[c] += val;
                }
            });
        }

        // Trigger start
        start_flag.store(true, std::memory_order_release);

        for (auto& t : producers) {
            t.join();
        }
        for (auto& t : consumers) {
            t.join();
        }

        const auto end_time = std::chrono::high_resolution_clock::now();
        const std::chrono::duration<double> duration = end_time - start_time;
        duration_sec = duration.count();

        for (size_t c = 0; c < NUM_CONSUMERS; ++c) {
            total_popped += consumer_counts[c];
        }

        std::cout << "[*] Multi-threaded execution finished in " << std::fixed
                  << std::setprecision(4) << duration_sec << " seconds.\n";
        std::cout << "[*] Total Items Popped across consumers: " << total_popped << "\n";
        assert(total_popped == TOTAL_OPS);
        std::cout << "[+] ASSERTION PASSED: All " << TOTAL_OPS << " items successfully pushed & popped!\n";

        // Reclaim deferred items now that threads have joined
        stack.reclaim_quiescent();
        std::cout << "[*] Active allocations before stack destruction: "
                  << g_active_node_allocations.load() << "\n";
    } // stack is destroyed here, cleaning up all internal state

    // Verify exactly zero leaks after stack destruction
    std::cout << "[*] Active node allocations after stack destruction: "
              << g_active_node_allocations.load() << "\n";
    assert(g_active_node_allocations.load() == 0);
    std::cout << "[+] ZERO LEAKS VERIFIED: g_active_node_allocations == 0\n";

    double ops_per_sec = static_cast<double>(TOTAL_OPS * 2) / duration_sec;
    std::cout << "[*] Multi-Threaded Treiber Stack Throughput: " << std::fixed
              << std::setprecision(2) << (ops_per_sec / 1e6) << " Million CAS ops/sec\n";
    std::cout << "============================================================\n\n";
}

int main() {
    run_multithreaded_test();
    return 0;
}
