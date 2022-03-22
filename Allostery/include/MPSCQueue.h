#include <atomic>
#include <optional>

#include <emmintrin.h>

template<typename T> class MPSCQueue {
    struct Node {
        std::atomic<Node*> next;
        std::optional<T> value;

        explicit Node(std::optional<T> v) noexcept : value(std::move(v)) {}
    };

    std::atomic<Node*> head;
    std::atomic<Node*> tail;
    std::atomic_flag sem;

  public:
    MPSCQueue() noexcept {
        auto stub = new Node{std::nullopt};
        head.store(stub, std::memory_order_relaxed);
        tail.store(stub, std::memory_order_relaxed);
    }

    template<bool notify = true> void push(T v) noexcept {
        auto new_node = new Node{std::move(v)};
        auto prev = head.exchange(new_node, std::memory_order_acq_rel);
        prev->next.store(new_node, std::memory_order_release);
        if constexpr (notify) {
            sem.clear(std::memory_order_release);
            sem.notify_one();
        }
    }

    template<bool wait = true> std::conditional_t<wait, T, std::optional<T>> pop() noexcept {
        auto t = tail.load(std::memory_order_relaxed);
        auto sure = false;
    reload:
        if (auto next = t->next.load(sure ? std::memory_order_relaxed : std::memory_order_acquire); next) {
            tail.store(next, std::memory_order_relaxed);
            delete t;
            std::optional<T> ret;
            next->value.swap(ret);
            if constexpr (wait)
                return std::move(ret.value());
            else
                return ret;
        } else if (head.load(std::memory_order_acquire) == t) {
            if constexpr (wait) {
                for (;;) {
                    if (!sem.test_and_set(std::memory_order_acquire))
                        goto reload;
                    sem.wait(true, std::memory_order_relaxed);
                    sure = true;
                }
            } else
                return std::nullopt;
        } else [[unlikely]] {
            _mm_pause();
            goto reload;
        }
    }
};
