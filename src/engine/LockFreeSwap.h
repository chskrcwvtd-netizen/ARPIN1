#pragma once

#include <atomic>
#include <memory>
#include <utility>
#include <vector>

namespace phrasearp {

// Lock-free publish of an immutable snapshot.
// The audio thread copies at a step boundary and never allocates or takes a mutex.
// Retired boxes are deleted on the message thread.
template <typename T>
class LockFreeSwap {
public:
    struct Box {
        std::atomic<Box*> retireNext { nullptr };
        T value;
        explicit Box(T v) : value(std::move(v)) {}
    };

    LockFreeSwap() = default;
    LockFreeSwap(const LockFreeSwap&) = delete;
    LockFreeSwap& operator=(const LockFreeSwap&) = delete;

    ~LockFreeSwap() {
        if (Box* pending = pending_.exchange(nullptr, std::memory_order_acq_rel))
            delete pending;
        drain();
    }

    void publish(T value) {
        auto* fresh = new Box(std::move(value));
        Box* old = pending_.exchange(fresh, std::memory_order_acq_rel);
        if (old != nullptr)
            pushRetire(old);
    }

    bool hasPending() const { return pending_.load(std::memory_order_acquire) != nullptr; }

    // Audio thread. Copies into dest, then retires the box. No heap allocation.
    bool takeInto(T& dest) {
        Box* box = pending_.exchange(nullptr, std::memory_order_acq_rel);
        if (box == nullptr)
            return false;
        dest = box->value;
        pushRetire(box);
        return true;
    }

    void drain() {
        Box* node = retire_.exchange(nullptr, std::memory_order_acq_rel);
        while (node != nullptr) {
            Box* next = node->retireNext.load(std::memory_order_relaxed);
            delete node;
            node = next;
        }
    }

private:
    void pushRetire(Box* box) {
        Box* expected = retire_.load(std::memory_order_relaxed);
        do {
            box->retireNext.store(expected, std::memory_order_relaxed);
        } while (!retire_.compare_exchange_weak(expected, box, std::memory_order_release, std::memory_order_relaxed));
    }

    std::atomic<Box*> pending_ { nullptr };
    std::atomic<Box*> retire_ { nullptr };
};

} // namespace phrasearp
