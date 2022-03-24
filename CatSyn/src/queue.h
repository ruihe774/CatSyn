#pragma once

#include <atomic>
#include <exception>
#include <queue>

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
            _mm_pause();
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
                _mm_pause();
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
