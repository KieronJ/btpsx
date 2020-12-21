#pragma once

#include <cstddef>

#include <common/types.hpp>

class CodeBuffer {
public:
    CodeBuffer(std::size_t size);
    ~CodeBuffer();

    u8 * Commit(std::size_t size);
    void Flush();

    inline u8 * Buffer() { return m_buffer; }
    inline u8 * Current() { return m_current; }

    inline std::size_t Remaining()
    {
        return m_size - (m_current - m_buffer);
    }

private:
    static u8 * Allocate(std::size_t size);
    static void Free(u8 *ptr, std::size_t size);

    u8 *m_buffer;
    std::size_t m_size;

    u8 *m_current;
};
