#pragma once

#include <atomic>
#include <exception>
#include <queue>

inline void yield() noexcept {
#ifdef _WIN32
    _mm_pause();
#else
    asm volatile inline("pause");
#endif
}

struct StopRequested : std::exception {};

template<typename T> class SCQueue {
    struct Node {
        std::atomic<Node*> next;
        std::optional<T> value;

        explicit Node(std::optional<T> v) noexcept : value(std::move(v)) {}
    };

    std::atomic<Node*> head;
    std::atomic<Node*> tail;
    std::atomic_flag sem;

    template<bool notify = true> void push(Node* new_node) noexcept {
        auto prev = head.exchange(new_node, std::memory_order_acq_rel);
        prev->next.store(new_node, std::memory_order_release);
        if constexpr (notify) {
            sem.clear(std::memory_order_release);
            sem.notify_one();
        }
    }

  public:
    SCQueue() noexcept {
        auto stub = new Node{std::nullopt};
        head.store(stub, std::memory_order_relaxed);
        tail.store(stub, std::memory_order_relaxed);
    }

    template<bool notify = true> void push(T v) noexcept {
        push<notify>(new Node{std::move(v)});
    }

    template<bool wait = true> std::conditional_t<wait, T, std::optional<T>> pop() noexcept(!wait) {
        auto t = tail.load(std::memory_order_relaxed);
    reload:
        if (auto next = t->next.load(std::memory_order_acquire); next) {
            tail.store(next, std::memory_order_relaxed);
            delete t;
            std::optional<T> ret;
            next->value.swap(ret);
            if constexpr (wait) {
                if (!ret) [[unlikely]]
                    throw StopRequested();
                return std::move(ret.value());
            } else
                return ret;
        } else if (head.load(std::memory_order_acquire) == t) {
            if constexpr (wait)
                for (;;) {
                    if (!sem.test_and_set(std::memory_order_acquire))
                        goto reload;
                    sem.wait(true, std::memory_order_relaxed);
                }
            else
                return std::nullopt;
        } else [[unlikely]] {
            yield();
            goto reload;
        }
    }

    void request_stop() noexcept {
        push<true>(new Node{std::nullopt});
    }

    template<bool wait = true, typename F> void consume_one(F&& f) noexcept(!wait) {
        auto v = pop<wait>();
        if constexpr (wait)
            f(std::move(v));
        else {
            if (v)
                f(std::move(v.value()));
        }
    }

    template<bool wait = true, typename F> void consume_all(F&& f) noexcept(!wait) {
        auto fv = pop<wait>();
        if constexpr (wait) {
            f(std::move(fv));
        } else {
            if (fv)
                f(std::move(fv.value()));
            else
                return;
        }
        for (auto v = pop<false>(); v; v = pop<false>())
            f(std::move(v.value()));
    }

    template<typename F> void stream(F&& f) {
        while (true) {
            f(pop<true>());
        }
    }

    ~SCQueue() {
        for (auto cur = tail.load(std::memory_order_relaxed); cur;) {
            auto next = cur->next.load(std::memory_order_relaxed);
            delete cur;
            cur = next;
        }
    }
};

class SpinLock {
    std::atomic_flag lock;

  public:
    void acquire() noexcept {
        for (;;) {
            if (!lock.test_and_set(std::memory_order_acq_rel))
                break;
            while (lock.test(std::memory_order_relaxed))
                yield();
        }
    }
    void release() noexcept {
        lock.clear(std::memory_order_release);
    }
};

template<typename T, typename Compare = std::less<>> class PriorityQueue {
    std::priority_queue<T, std::vector<T>, Compare> pq;
    SpinLock lock;
    std::atomic_flag sem;
    std::atomic_flag stopped;

  public:
    template<bool notify = true> void push(T v) noexcept {
        lock.acquire();
        pq.push(std::move(v));
        lock.release();
        if constexpr (notify) {
            sem.clear(std::memory_order_release);
            sem.notify_one();
        }
    }

    template<bool wait = true> std::conditional_t<wait, T, std::optional<T>> pop() noexcept(!wait) {
    reload:
        lock.acquire();
        if (!pq.empty()) {
            auto v = std::move(pq.top());
            pq.pop();
            lock.release();
            return v;
        }
        lock.release();
        if constexpr (wait)
            for (;;) {
                if (stopped.test(std::memory_order_acquire)) [[unlikely]]
                    throw StopRequested();
                if (!sem.test_and_set(std::memory_order_acquire))
                    goto reload;
                sem.wait(true, std::memory_order_relaxed);
            }
        else
            return std::nullopt;
    }

    void request_stop() noexcept {
        stopped.test_and_set(std::memory_order_release);
        sem.clear(std::memory_order_release);
        sem.notify_all();
    }

    template<bool wait = true, typename F> void consume_one(F&& f) noexcept(!wait) {
        auto v = pop<wait>();
        if constexpr (wait)
            f(std::move(v));
        else {
            if (v)
                f(std::move(v.value()));
        }
    }

    template<bool wait = true, typename F> void consume_all(F&& f) noexcept(!wait) {
        auto fv = pop<wait>();
        if constexpr (wait) {
            f(std::move(fv));
        } else {
            if (fv)
                f(std::move(fv.value()));
            else
                return;
        }
        for (auto v = pop<false>(); v; v = pop<false>())
            f(std::move(v.value()));
    }

    template<typename F> void stream(F&& f) {
        while (true) {
            f(pop<true>());
        }
    }
};

class Wedge {
    static constexpr unsigned highest = 1 << (sizeof(unsigned) * 8 - 1);
    std::atomic_uint* atm;

  public:
    explicit Wedge(std::atomic_uint& atm) noexcept : atm(&atm) {}
    Wedge() noexcept : atm(nullptr) {}

    bool try_lock_shared() noexcept {
        if (atm->fetch_add(1, std::memory_order_acq_rel) & highest) {
            atm->fetch_sub(1, std::memory_order_relaxed);
            return false;
        } else
            return true;
    }

    bool try_lock_exclusive() noexcept {
        auto orig = atm->load(std::memory_order_relaxed);
        return orig == 0 && atm->compare_exchange_weak(orig, highest, std::memory_order_release);
    }

    void unlock_shared() noexcept {
        atm->fetch_sub(1, std::memory_order_relaxed);
    }

    void unlock_exclusive() noexcept {
        atm->fetch_and(~highest, std::memory_order_relaxed);
    }
};

class WedgeLock : public Wedge {
    unsigned state = 0;

  public:
    using Wedge::Wedge;

    bool try_lock_shared() noexcept {
        if (Wedge::try_lock_shared()) {
            state = 1;
            return true;
        } else
            return false;
    }

    bool try_lock_exclusive() noexcept {
        if (Wedge::try_lock_exclusive()) {
            state = 2;
            return true;
        } else
            return false;
    }

    void unlock_shared() noexcept {
        Wedge::unlock_shared();
        state = 0;
    }

    void unlock_exclusive() noexcept {
        Wedge::unlock_exclusive();
        state = 0;
    }

    ~WedgeLock() {
        if (state == 1)
            unlock_shared();
        else if (state == 2)
            unlock_exclusive();
    }
};
