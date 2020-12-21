#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>

template <typename T, std::size_t Capacity>
class Cbuf {
    static_assert(Capacity != 0, "Capacity cannot be zero");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    std::size_t Enqueue(const T *data, std::size_t count)
    {
        count = std::min(count, Availible());

        const std::size_t length1 = std::min(Capacity - m_write, count);
        std::memcpy(&m_buffer[m_write], data, length1);

        const std::size_t length2 = count - length1;
        std::memcpy(m_buffer.data(), &data[length1], length2);

        m_write = (m_write + count) % Capacity;
        return count;
    }

    std::size_t Dequeue(T *data, std::size_t count)
    {
        count = std::min(count, Size());

        const std::size_t length1 = std::min(Capacity - m_read, count);
        std::memcpy(data, &m_buffer[m_read], length1);

        const std::size_t length2 = count - length1;
        std::memcpy(&data[length1], m_buffer.data(), length2);

        m_read = (m_read + count) % Capacity;
        return count;
    }

    inline bool Empty() const { return m_read == m_write; }
    inline bool Full() const { return Size() == Capacity - 1; }

    inline std::size_t Size() const { return (m_write - m_read) % Capacity; }
    inline std::size_t Availible() const { return Capacity - Size() - 1; }

private:
    std::array<T, Capacity> m_buffer;
    std::atomic<std::size_t> m_read = 0;
    std::atomic<std::size_t> m_write = 0;
};
