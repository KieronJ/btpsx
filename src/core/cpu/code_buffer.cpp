#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>

#include <sys/mman.h>

#include <common/types.hpp>

#include "code_buffer.hpp"

CodeBuffer::CodeBuffer(std::size_t size)
    : m_buffer{ Allocate(size) },
      m_size{ size },
      m_current{ m_buffer } {}

CodeBuffer::~CodeBuffer() { Free(m_buffer, m_size); }

u8 * CodeBuffer::Commit(std::size_t size)
{
    if (size > Remaining()) {
        std::cout << "not enough memory for commit" << std::endl;
        return nullptr;
    }

    m_current += size;
    return m_current - size;
}

void CodeBuffer::Flush()
{
    m_current = m_buffer;
    std::memset(m_buffer, 0, m_size);
}

u8 * CodeBuffer::Allocate(std::size_t size)
{
    void *ret = mmap(
        nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_ANONYMOUS | MAP_PRIVATE, -1, 0
    );

    assert(ret != MAP_FAILED);
    return reinterpret_cast<u8 *>(ret);
}

void CodeBuffer::Free(u8 *ptr, std::size_t size)
{
    int ret = munmap(ptr, size);
    assert(ret != -1);
    (void)ret;
}
