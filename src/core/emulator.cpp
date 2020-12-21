#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <spdlog/spdlog.h>

#include "cpu/core.hpp"
#include "cpu/recompiler.hpp"

#include "joypad/digital.hpp"
#include "cdc.hpp"
#include "dmac.hpp"
#include "emulator.hpp"
#include "error.hpp"
#include "gpu.hpp"
#include "intc.hpp"
#include "io.hpp"
#include "scheduler.hpp"
#include "spu.hpp"
#include "timer.hpp"

namespace Core
{

Emulator::Emulator(const std::filesystem::path& bios,
                   const std::filesystem::path& disc,
                   bool enable_audio)
    : m_cpu(std::make_unique<Cpu::Core>(this)),
      m_cdc(std::make_unique<Cdc>(this, disc)),
      m_gpu(std::make_unique<Gpu>()),
      m_intc(std::make_unique<Intc>(this)),
      m_scheduler(std::make_unique<Scheduler>()),
      m_spu(std::make_unique<Spu>(enable_audio)),
      m_joypad(new Digital()),
      m_dmac(std::make_unique<Dmac>(this)),
      m_io(std::make_unique<Io>(this, m_joypad)),
      m_timer0(Timer<0>(this)),
      m_timer1(Timer<1>(this)),
      m_timer2(Timer<2>(this))
{
    std::ifstream f(bios, std::ios::binary);

    if (!f.is_open()) {
        Error("unable to open {}", bios.filename().string());
    }

    f.read(reinterpret_cast<char *>(m_bios.data()), BiosSize);
    f.close();

    m_bios[0x6f0c] = 0x01;
    m_bios[0x6f0d] = 0x00;
    m_bios[0x6f0e] = 0x01;
    m_bios[0x6f0f] = 0x24;

    m_bios[0x6f14] = 0xc0;
    m_bios[0x6f15] = 0xa9;
    m_bios[0x6f16] = 0x81;
    m_bios[0x6f17] = 0xaf;

    auto vblank_cb = [=]() {
        std::memcpy(m_swapchain.ProducerBuffer(), m_gpu->Framebuffer(), 2 * 1024 * 512);
        m_swapchain.Swap();

        m_intc->AssertInterrupt(Interrupt::Vblank);
        m_frame_finished = true;
    };

    auto spu_cb = [=]() { m_spu->Tick(); };

    m_scheduler->AddEvent(
        Scheduler::Event::Type::Vblank,
        Scheduler::Event::Mode::Periodic,
        CyclesPerFrame,
        vblank_cb
    );

    m_scheduler->AddEvent(
        Scheduler::Event::Type::Spu,
        Scheduler::Event::Mode::Periodic,
        CpuFrequency / 44100,
        spu_cb
    );

    Reset();
}

Emulator::~Emulator()
{
    delete m_joypad;
}

void Emulator::Reset()
{
    m_cpu->Reset();
    m_cdc->Reset();
    m_gpu->Reset();
    m_intc->Reset();
    m_spu->Reset();
    m_dmac->Reset();
    m_io->Reset();
    m_timer0.Reset();
    m_timer1.Reset();
    m_timer2.Reset();
}

void Emulator::Run()
{
    for (size_t i = 0; i < CyclesPerFrame / 2; ++i) {
        m_cpu->Run();

        m_cdc->Tick(); // DONE
        m_cdc->Tick();

        m_spu->Tick(); // DONE
        m_spu->Tick();

        m_timer0.Tick(); // TODO
        m_timer0.Tick();

        m_timer1.Tick(); // TODO
        m_timer1.Tick();

        m_timer2.Tick(); // TODO
        m_timer2.Tick();
    }

    m_intc->AssertInterrupt(Interrupt::Vblank); // DONE

    std::memcpy(m_swapchain.ProducerBuffer(), m_gpu->Framebuffer(), 2 * 1024 * 512);
    m_swapchain.Swap();
}

void Emulator::BenchFrame()
{
    int cycles = 0;
    auto last = std::chrono::high_resolution_clock::now();
    float delta;

    int cycles2;

    do {
        cycles2 = 0;

        while (cycles2 < 1000) cycles2 += m_cpu->Run();
        cycles += cycles2;

        auto current = std::chrono::high_resolution_clock::now();
        delta = std::chrono::duration_cast<std::chrono::microseconds>(current - last).count();
    } while (delta < 1000000.0f);

    spdlog::info("frame benchmark finished");
    spdlog::info("cpu speed = {:.3f} MHz", static_cast<float>(cycles) / 1000000.0f);
}

void Emulator::RunFrame()
{
    m_frame_finished = false;

    while (!m_frame_finished) {
        while (m_scheduler->NextEventTarget() > 0) {
            const size_t ticks = m_cpu->RunRecompiler();
            m_scheduler->Tick(ticks);
        }

        m_scheduler->UpdateEvents();
    }
}

void Emulator::LoadExe(const std::filesystem::path& filepath)
{
    struct PsxExeHeader {
        char magic[8];
        uint32_t unused[2];
        uint32_t pc;
        uint32_t gp;
        uint32_t text_addr;
        uint32_t text_size;
        uint32_t data_addr;
        uint32_t data_size;
        uint32_t bss_addr;
        uint32_t bss_size;
        uint32_t sp;
        uint32_t sp_size;
        uint32_t reserved[5];
        uint32_t padding[493];
    } header;

    std::ifstream exe(filepath, std::ios::binary);

    if (!exe.is_open()) {
        spdlog::warn("unable to open {}", filepath.filename().string());
        return;
    }

    exe.read(reinterpret_cast<char *>(&header), sizeof(header));

    const uint32_t text = header.text_addr & 0x1fffffff;
    const uint32_t data = header.data_addr & 0x1fffffff;
    const uint32_t bss = header.bss_addr & 0x1fffffff;

    if (text + header.text_size > RamSize) {
        spdlog::warn("text out of range addr=0x{:08x}, size={}", text, header.text_size);
        return;
    }

    if (data + header.data_size > RamSize) {
        spdlog::warn("data out of range addr=0x{:08x}, size={}", data, header.data_size);
        return;
    }

    if (bss + header.bss_size > RamSize) {
        spdlog::warn("bss out of range addr=0x{:08x}, size={}", bss, header.bss_size);
        return;
    }

    m_intc->WriteMask(0);

    m_cpu->WritePc(header.pc);
    m_cpu->WriteRegister(28, header.gp);
    m_cpu->WriteRegister(29, header.sp + header.sp_size);
    m_cpu->WriteRegister(30, header.sp + header.sp_size);

    exe.read(reinterpret_cast<char *>(&m_ram[text]), header.text_size);
    exe.read(reinterpret_cast<char *>(&m_ram[data]), header.data_size);
    std::memset(&m_ram[bss], 0, header.bss_size);
}

void Emulator::BurstFill(void *dst, u32 addr, std::size_t size)
{
    if (addr < RamEnd) {
        /* TODO: This depends on size */
        Tick(20);
        std::memcpy(dst, &m_ram.data()[addr], size);
        return;
    }

    if (addr >= BiosStart && addr < BiosEnd) {
        const std::size_t offset = addr - BiosStart;

        /* TODO: This depends on size */
        Tick(96);
        std::memcpy(dst, &m_bios.data()[offset], size);
        return;
    }

    Error("burst fill from unknown address 0x{:08x}", addr);
}

uint32_t Emulator::ReadCode(uint32_t addr)
{
    if (addr < RamEnd) {
        Tick(5);
        return reinterpret_cast<uint32_t *>(m_ram.data())[addr >> 2];
    }

    if (addr >= BiosStart && addr < BiosEnd) {
        const uint32_t offset = (addr - BiosStart) >> 2;

        Tick(24);
        return reinterpret_cast<uint32_t *>(m_bios.data())[offset];
    }

    Error("read (code) from unknown address 0x{:08x}", addr);
}

uint8_t Emulator::ReadByte(uint32_t addr)
{
    if (addr < RamEnd) {
        Tick(5);
        return m_ram[addr];
    }

    if (addr >= BiosStart && addr < BiosEnd) {
        const uint32_t offset = addr - BiosStart;

        Tick(6);
        return m_bios[offset];
    }

    if (addr >= ScratchpadStart && addr < ScratchpadEnd) {
        const uint32_t offset = addr - ScratchpadStart;
        return m_scratchpad[offset];
    }

    if (addr >= 0x1f000000 && addr < 0x1f800000) {
        Tick(6);
        return 0;
    }

    if (addr == 0x1f801040) {
        Tick(3);
        return m_io->Rx();
    }

    if (addr >= 0x1f801800 && addr < 0x1f801804) {
        Tick(6);
        return m_cdc->Read(addr);
    }

    if (addr == 0x1f802021) {
        Tick(12);
        return 0xc;
    }

    Error("read (byte) from unknown address 0x{:08x}", addr);
}

uint16_t Emulator::ReadHalf(uint32_t addr)
{
    if (addr < RamEnd) {
        Tick(5);
        return reinterpret_cast<uint16_t *>(m_ram.data())[addr >> 1];
    }

    if (addr >= BiosStart && addr < BiosEnd) {
        const uint32_t offset = (addr - BiosStart) >> 1;

        Tick(12);
        return reinterpret_cast<uint16_t *>(m_bios.data())[offset];
    }

    if (addr >= ScratchpadStart && addr < ScratchpadEnd) {
        const uint32_t offset = (addr - ScratchpadStart) >> 1;
        return reinterpret_cast<uint16_t *>(m_scratchpad.data())[offset];
    }

    if (addr == 0x1f801044) {
        Tick(3);
        return m_io->ReadStatus();
    }

    if (addr == 0x1f80104a) {
        Tick(3);
        return m_io->ReadControl();
    }

    if (addr == 0x1f801070) {
        Tick(3);
        return m_intc->ReadStatus();
    }

    if (addr == 0x1f801074) {
        Tick(3);
        return m_intc->ReadMask();
    }

    if (addr >= 0x1f801100 && addr < 0x1f801110) {
        Tick(3);
        return m_timer0.Read(addr);
    }

    if (addr >= 0x1f801110 && addr < 0x1f801120) {
        Tick(3);
        return m_timer1.Read(addr);
    }

    if (addr >= 0x1f801120 && addr < 0x1f801130) {
        Tick(3);
        return m_timer2.Read(addr);
    }

    if (addr >= 0x1f801c00 && addr < 0x1f802000) {
        Tick(18);
        return m_spu->Read(addr);
    }

    Error("read (half) from unknown address 0x{:08x}", addr);
}

uint32_t Emulator::ReadWord(uint32_t addr)
{
    if (addr < RamEnd) {
        Tick(5);
        return reinterpret_cast<uint32_t *>(m_ram.data())[addr >> 2];
    }

    if (addr >= BiosStart && addr < BiosEnd) {
        const uint32_t offset = (addr - BiosStart) >> 2;

        Tick(24);
        return reinterpret_cast<uint32_t *>(m_bios.data())[offset];
    }

    if (addr >= ScratchpadStart && addr < ScratchpadEnd) {
        const uint32_t offset = (addr - ScratchpadStart) >> 2;
        return reinterpret_cast<uint32_t *>(m_scratchpad.data())[offset];
    }

    if (addr >= 0x1f000000 && addr < 0x1f800000) {
        Tick(24);
        return 0;
    }

    if (addr == 0x1f801014) {
        return 0;
    }

    if (addr == 0x1f801060) {
        return 0;
    }

    if (addr == 0x1f801070) {
        Tick(3);
        return m_intc->ReadStatus();
    }

    if (addr == 0x1f801074) {
        Tick(3);
        return m_intc->ReadMask();
    }

    if (addr >= 0x1f801080 && addr < 0x1f801100) {
        Tick(3);
        return m_dmac->Read(addr);
    }

    if (addr >= 0x1f801100 && addr < 0x1f801110) {
        Tick(3);
        return m_timer0.Read(addr);
    }

    if (addr >= 0x1f801110 && addr < 0x1f801120) {
        Tick(3);
        return m_timer1.Read(addr);
    }

    if (addr >= 0x1f801120 && addr < 0x1f801130) {
        Tick(3);
        return m_timer2.Read(addr);
    }

    if (addr == 0x1f801810) {
        Tick(3);
        return m_gpu->GpuRead();
    }

    if (addr == 0x1f801814) {
        Tick(3);
        return m_gpu->GpuStat();
    }

    if (addr == 0x1f801824) {
        Tick(3);
        spdlog::warn("read from unimplemented mdec control reg");
        return 0;
    }

    Error("read (word) from unknown address 0x{:08x}", addr);
}

void Emulator::WriteByte(uint32_t addr, uint8_t data)
{
    if (addr < RamEnd) {
        m_ram[addr] = data;
        Cpu::Recompiler::InvalidateAddress(addr);
        return;
    }

    if (addr >= ScratchpadStart && addr < ScratchpadEnd) {
        const uint32_t offset = addr - ScratchpadStart;
        m_scratchpad[offset] = data;
        return;
    }

    if (addr == 0x1f801040) {
        m_io->Tx(data);
        return;
    }

    if (addr >= 0x1f801800 && addr < 0x1f801804) {
        m_cdc->Write(addr, data);
        return;
    }

    if (addr == 0x1f802023) {
        if (data == '\r') {
            return;
        }

        if (data == '\n' && m_tty.length() > 0) {
            spdlog::debug("tty: {}", m_tty);
            m_tty.clear();
            return;
        }

        m_tty.push_back(data);
        return;
    }

    if (addr >= 0x1f802000 && addr < 0x1f804000) {
        return;
    }

    Error("write (byte) to unknown address 0x{:08x}", addr);
}

void Emulator::WriteHalf(uint32_t addr, uint16_t data)
{
    if (addr < RamEnd) {
        reinterpret_cast<uint16_t *>(m_ram.data())[addr >> 1] = data;
        Cpu::Recompiler::InvalidateAddress(addr);
        return;
    }

    if (addr >= ScratchpadStart && addr < ScratchpadEnd) {
        const uint32_t offset = (addr - ScratchpadStart) >> 1;
        reinterpret_cast<uint16_t *>(m_scratchpad.data())[offset] = data;
        return;
    }

    if (addr == 0x1f801048) {
        m_io->WriteMode(data);
        return;
    }

    if (addr == 0x1f80104a) {
        m_io->WriteControl(data);
        return;
    }

    if (addr == 0x1f80104e) {
        m_io->m_baudrate = data;
        return;
    }

    if (addr == 0x1f801070) {
        m_intc->WriteStatus(data);
        return;
    }

    if (addr == 0x1f801074) {
        m_intc->WriteMask(data);
        return;
    }

    if (addr >= 0x1f801100 && addr < 0x1f801110) {
        m_timer0.Write(addr, data);
        return;
    }

    if (addr >= 0x1f801110 && addr < 0x1f801120) {
        m_timer1.Write(addr, data);
        return;
    }

    if (addr >= 0x1f801120 && addr < 0x1f801130) {
        m_timer2.Write(addr, data);
        return;
    }

    if (addr >= 0x1f801c00 && addr < 0x1f802000) {
        m_spu->Write(addr, data);
        return;
    }

    Error("write (half) to unknown address 0x{:08x}", addr);
}

void Emulator::WriteWord(uint32_t addr, uint32_t data)
{
    if (addr < RamEnd) {
        reinterpret_cast<uint32_t *>(m_ram.data())[addr >> 2] = data;
        Cpu::Recompiler::InvalidateAddress(addr);
        return;
    }

    if (addr >= ScratchpadStart && addr < ScratchpadEnd) {
        const uint32_t offset = (addr - ScratchpadStart) >> 2;
        reinterpret_cast<uint32_t *>(m_scratchpad.data())[offset] = data;
        return;
    }

    if (addr >= 0x1f801000 && addr < 0x1f801024) {
        return;
    }

    if (addr == 0x1f801060) {
        return;
    }

    if (addr == 0x1f801070) {
        m_intc->WriteStatus(data);
        return;
    }

    if (addr == 0x1f801074) {
        m_intc->WriteMask(data);
        return;
    }

    if (addr >= 0x1f801080 && addr < 0x1f801100) {
        m_dmac->Write(addr, data);
        return;
    }

    if (addr >= 0x1f801100 && addr < 0x1f801110) {
        m_timer0.Write(addr, data);
        return;
    }

    if (addr >= 0x1f801110 && addr < 0x1f801120) {
        m_timer1.Write(addr, data);
        return;
    }

    if (addr >= 0x1f801120 && addr < 0x1f801130) {
        m_timer2.Write(addr, data);
        return;
    }

    if (addr == 0x1f801810) {
        m_gpu->Gp0(data);
        return;
    }

    if (addr == 0x1f801814) {
        m_gpu->Gp1(data);
        return;
    }

    if (addr == 0x1f801820) {
        spdlog::warn("write to unimplemented mdec command reg");
        return;
    }

    if (addr == 0x1f801824) {
        spdlog::warn("write to unimplemented mdec control reg");
        return;
    }

    if (addr == 0xfffe0130) {
        return;
    }

    Error("write (word) to unknown address 0x{:08x}", addr);
}

}
