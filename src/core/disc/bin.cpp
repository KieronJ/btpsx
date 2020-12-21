#include <cstddef>
#include <filesystem>
#include <fstream>

//#include <spdlog/spdlog.h>

#include "../error.hpp"
#include "bin.hpp"

namespace Core
{

void Bin::Open(const std::filesystem::path& filepath)
{
    m_disc.open(filepath, std::ios::binary);

    if (!m_disc.is_open()) {
        Error("unable to open {}", filepath.filename().string());
    }
}

void Bin::Close()
{
    m_disc.close();
}

void Bin::Read(void *buffer, size_t sector)
{
    if (sector < PreGapSectors) {
        Error("attempt to read pre-gap");
    }

    //spdlof::info("seek=0x{:x}", SectorSize * (sector - PreGapSectors));

    m_disc.seekg(SectorSize * (sector - PreGapSectors));
    m_disc.read(reinterpret_cast<char *>(buffer), SectorSize);
}

}
