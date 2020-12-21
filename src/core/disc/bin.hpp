#ifndef CORE_DISC_BIN_HPP
#define CORE_DISC_BIN_HPP

#include <cstddef>
#include <filesystem>
#include <fstream>

#include "disc.hpp"

namespace Core
{

class Bin : public Disc {
public:
    Bin(const std::filesystem::path& filepath) { Open(filepath); }
    ~Bin() override { Close(); }

    void Open(const std::filesystem::path& filepath) override;
    void Close() override;

    void Read(void *buffer, size_t sector) override;

private:
    static constexpr size_t PreGapSectors = 150;
    static constexpr size_t SectorSize = 2352;

    std::ifstream m_disc;
};

}

#endif /* CORE_DISC_BIN_HPP */
