#pragma once

#include "common/threads.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>

namespace Libs::LibKernel::WaitSupport {

// Thread-local hooks let a fixture advance time or inject a wake after a waiter
// is registered. Production always uses the monotonic clock and condition wait.
struct Hooks {
    std::function<uint64_t()> now_micros;
    std::function<void()> before_park;
};
inline thread_local Hooks* test_hooks = nullptr;

inline uint64_t NowMicros() {
    if (test_hooks != nullptr && test_hooks->now_micros) return test_hooks->now_micros();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

class Deadline {
public:
    explicit Deadline(const uint32_t* timeout): m_start(NowMicros()), m_finite(timeout != nullptr),
        m_budget(timeout == nullptr ? 0 : *timeout) {}
    uint32_t Remaining() const {
        const auto now = NowMicros();
        const auto elapsed = now >= m_start ? now - m_start : 0;
        return elapsed >= m_budget ? 0 : static_cast<uint32_t>(m_budget - elapsed);
    }
    bool Expired() const { return m_finite && Remaining() == 0; }
    uint32_t Slice() const { return m_finite ? std::min<uint32_t>(Remaining(), 10000) : 10000; }
    void Update(uint32_t* timeout) const { if (timeout != nullptr) *timeout = Remaining(); }
private:
    uint64_t m_start;
    bool m_finite;
    uint32_t m_budget;
};

inline void Park(Common::CondVar& condition, Common::Mutex& mutex, uint32_t micros) {
    if (test_hooks != nullptr && test_hooks->before_park) {
        mutex.Unlock();
        test_hooks->before_park();
        mutex.Lock();
    } else {
        condition.WaitFor(&mutex, micros);
    }
}
}
