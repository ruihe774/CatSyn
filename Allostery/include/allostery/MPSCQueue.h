#pragma once

#include <atomic>
#include <optional>

#include <emmintrin.h>

#include <allostery/Stoper.h>

namespace allostery {

template<typename T> class MPSCQueue {
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
    MPSCQueue() noexcept {
        auto stub = new Node{std::nullopt};
        head.store(stub, std::memory_order_relaxed);
        tail.store(stub, std::memory_order_relaxed);
    }

    template<bool notify = true> void push(T v) noexcept {
        push<notify>(new Node{std::move(v)});
    }

    template<bool wait = true> std::conditional_t<wait, T, std::optional<T>> pop() {
        auto t = tail.load(std::memory_order_relaxed);
        auto sure = false;
    reload:
        if (auto next = t->next.load(sure ? std::memory_order_relaxed : std::memory_order_acquire); next) {
            tail.store(next, std::memory_order_relaxed);
            delete t;
            std::optional<T> ret;
            next->value.swap(ret);
            if (!ret) [[unlikely]]
                throw StopRequested();
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

    void request_stop() noexcept {
        push<true>(new Node{std::nullopt});
    }

    template<bool wait = true, typename F> void consume_one(F&& f) {
        auto v = pop<wait>();
        if constexpr (wait)
            f(std::move(v));
        else {
            if (v)
                f(std::move(v.value()));
        }
    }

    template<bool wait = true, typename F> void consume_all(F&& f) {
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

    ~MPSCQueue() {
        for (auto cur = tail.load(std::memory_order_relaxed); cur;) {
            auto next = cur->next.load(std::memory_order_relaxed);
            delete cur;
            cur = next;
        }
    }
};

} // namespace allostery
