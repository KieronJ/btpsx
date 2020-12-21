#pragma once

#include <cstddef>
#include <functional>
#include <mutex>

#include "types.hpp"

template <std::size_t Size, typename T>
class Swapchain {
    static_assert(Size == 2, "swapchain currently only supports two buffers");

public:
    inline void WithConsumer(std::function<void(T&)> f)
    {
        std::lock_guard guard(m_mutex);
        f(m_buffers[!m_index]);
    }

    inline T& ProducerBuffer() { return m_buffers[m_index]; }

    inline void Swap()
    {
        std::lock_guard guard(m_mutex);
        m_index = !m_index;
    }

private:
    bool m_index = 0;

    std::mutex m_mutex;
    std::array<T, 2> m_buffers;
};
