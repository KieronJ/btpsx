#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include <common/swapchain.hpp>

#include "cpu/core.hpp"
#include "scheduler.hpp"
#include "timer.hpp"

namespace Core
{

class Cdc;
class Dmac;
class Gpu;
class Intc;
class Io;
class Joypad;
class Spu;

class Emulator : public Cpu::Bus {
public:
    Emulator(const std::filesystem::path& bios,
             const std::filesystem::path& disc,
             bool enable_audio);
    ~Emulator();

    void Reset();
    void Run();
    void RunFrame();
    void BenchFrame();

    void LoadExe(const std::filesystem::path& filepath);

    inline void Tick(int64_t ticks) override { m_scheduler->Tick(ticks); }

    void BurstFill(void *dst, u32 addr, std::size_t size);

    uint32_t ReadCode(uint32_t addr) override;

    uint8_t ReadByte(uint32_t addr) override;
    uint16_t ReadHalf(uint32_t addr) override;
    uint32_t ReadWord(uint32_t addr) override;

    void WriteByte(uint32_t addr, uint8_t data) override;
    void WriteHalf(uint32_t addr, uint16_t data) override;
    void WriteWord(uint32_t addr, uint32_t data) override;

    std::unique_ptr<Cpu::Core> m_cpu;
    std::unique_ptr<Cdc> m_cdc;
    std::unique_ptr<Gpu> m_gpu;
    std::unique_ptr<Intc> m_intc;
    std::unique_ptr<Scheduler> m_scheduler;
    std::unique_ptr<Spu> m_spu;

    Joypad *m_joypad;

    Swapchain<2, uint8_t[2 * 1024 * 512]> m_swapchain;

private:
    static constexpr uint32_t BiosStart = 0x1fc00000;
    static constexpr uint32_t BiosEnd = 0x1fc80000;
    static constexpr size_t BiosSize = 512 * 1024;

    static constexpr uint32_t RamEnd = 0x200000;
    static constexpr size_t RamSize = 2 * 1024 * 1024;

    static constexpr uint32_t ScratchpadStart = 0x1f800000;
    static constexpr uint32_t ScratchpadEnd = 0x1f800400;
    static constexpr size_t ScratchpadSize = 0x400;

    static constexpr std::size_t CpuFrequency = 44100 * 768;
    static constexpr std::size_t CyclesPerFrame = CpuFrequency / 60;

    std::array<uint8_t, BiosSize> m_bios;
    std::array<uint8_t, RamSize> m_ram;
    std::array<uint8_t, ScratchpadSize> m_scratchpad;

    std::unique_ptr<Dmac> m_dmac;
    std::unique_ptr<Io> m_io;

    Timer<0> m_timer0;
    Timer<1> m_timer1;
    Timer<2> m_timer2;

    bool m_frame_finished;

    std::string m_tty;
};

}
