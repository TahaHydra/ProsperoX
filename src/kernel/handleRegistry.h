#pragma once

#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <cstdint>

namespace Libs::LibKernel {

// Guest pointer-shaped handles are opaque identities, never object addresses.
// IDs are not reused during a process lifetime. An operation pins its object;
// removal prevents new acquisitions without freeing an object beneath a waiter.
template <class Handle, class Object> class HandleRegistry {
public:
    Handle Insert(std::shared_ptr<Object> object) {
        std::lock_guard lock(m_mutex);
        if (m_next == std::numeric_limits<uintptr_t>::max()) return nullptr;
        const auto handle = reinterpret_cast<Handle>(m_next++);
        m_objects.emplace(handle, std::move(object));
        return handle;
    }
    std::shared_ptr<Object> Acquire(Handle handle) {
        std::lock_guard lock(m_mutex);
        const auto it = m_objects.find(handle);
        return it == m_objects.end() ? nullptr : it->second;
    }
    std::shared_ptr<Object> Remove(Handle handle) {
        std::lock_guard lock(m_mutex);
        const auto it = m_objects.find(handle);
        if (it == m_objects.end()) return nullptr;
        auto object = std::move(it->second);
        m_objects.erase(it);
        return object;
    }
    size_t Size() {
        std::lock_guard lock(m_mutex);
        return m_objects.size();
    }
private:
    std::mutex m_mutex;
    uintptr_t m_next = 1;
    std::unordered_map<Handle, std::shared_ptr<Object>> m_objects;
};
}
