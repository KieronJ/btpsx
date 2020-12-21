#ifndef CORE_DISC_HPP
#define CORE_DISC_HPP

#include <cstddef>
#include <filesystem>

namespace Core
{

class Disc {
public:
    virtual ~Disc() = 0;

    virtual void Open(const std::filesystem::path& filepath) = 0;
    virtual void Close() = 0;

    virtual void Read(void *buffer, size_t sector) = 0;
};

}

#endif /* CORE_DISC_HPP */
