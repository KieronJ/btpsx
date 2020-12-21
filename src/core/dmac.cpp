#include <cstddef>
#include <cstdint>

#include <spdlog/spdlog.h>

#include "cdc.hpp"
#include "dmac.hpp"
#include "emulator.hpp"
#include "error.hpp"
#include "gpu.hpp"
#include "intc.hpp"
#include "spu.hpp"

namespace Core
{

Dmac::Dmac(Emulator *emulator) : m_emulator(emulator) {}

void Dmac::Reset()
{
    for (size_t i = 0; i < Channel::Count; ++i) {
        m_channels[i].chcr.raw = 0;
    }

    m_dpcr = 0x7654321;
    m_dicr.raw = 0;
}

uint32_t Dmac::Read(uint32_t addr)
{
    if (addr == 0x1f8010f0) {
        return m_dpcr;
    }

    if (addr == 0x1f8010f4) {
        return m_dicr.raw;
    }

    const size_t index = (addr >> 4) & 0x7;
    const DmaChannel *channel = &m_channels[index];

    switch (addr & 0xf) {
    case 0x0: return channel->madr.raw;
    case 0x4: return channel->bcr.raw;
    case 0x8: return channel->chcr.raw;
    default: Error("read from unknown dmac reg 0x{:08x}", addr);
    }
}

void Dmac::Write(uint32_t addr, uint32_t data)
{
    if (addr == 0x1f8010e8) {
        DmaChannel *channel = &m_channels[Channel::Otc];

        channel->chcr.raw = data & 0x51000000;
        channel->chcr.backward = true;

        if (channel->chcr.enable && channel->chcr.start) {
            StartTransferOtc();

            if ((m_dicr.enable & (1 << Channel::Otc)) != 0) {
                m_dicr.flag = m_dicr.flag | (1 << Channel::Otc);
                UpdateInterrupts();
            }

            //spdlog::info("otc complete");
            channel->chcr.raw &= ~0x11000000;
        }

        return;
    }

    if (addr == 0x1f8010f0) {
        m_dpcr = data;
        return;
    }

    if (addr == 0x1f8010f4) {
        m_dicr.raw &= 0xff000000;
        m_dicr.raw &= ~(data & 0x7f000000);
        m_dicr.raw |= data & 0xff803f;
        UpdateInterrupts();
        return;
    }

    const size_t index = (addr >> 4) & 0x7;
    DmaChannel *channel = &m_channels[index];

    switch (addr & 0xf) {
    case 0x0:
        channel->madr.address = data;
        break;
    case 0x4:
        channel->bcr.raw = data;
        break;
    case 0x8:
        channel->chcr.raw = data & 0x71770703;

        if (channel->chcr.enable && (channel->chcr.start
            || (channel->chcr.sync_mode != SyncMode::Manual))) {
            switch (index) {
            case Channel::MdecIn:
                StartTransferMdecIn();
                break;
            case Channel::Gpu:
                StartTransferGpu();
                break;
            case Channel::Cdrom:
                StartTransferCdrom();
                break;
            case Channel::Spu:
                StartTransferSpu();
                break;
            default: Error("unsupported dma{}", index);
            }

            if ((m_dicr.enable & (1 << index)) != 0) {
                m_dicr.flag = m_dicr.flag | (1 << index);
                UpdateInterrupts();
            }

            //spdlog::info("dma{} complete", index);
            channel->chcr.raw &= ~0x11000000;
        }

        break;
    default: Error("write to unknown dmac reg 0x{:08x}", addr);
    }
}

void Dmac::StartTransferMdecIn()
{
    spdlog::warn("unimplemented mdec in dma");
}

void Dmac::StartTransferGpu()
{
    const DmaChannel *channel = &m_channels[Channel::Gpu];

    uint32_t addr = channel->madr.address;
    size_t words = channel->bcr.size * channel->bcr.count;

    if (channel->chcr.sync_mode == SyncMode::Block) {
        do {
            if (channel->chcr.direction == Direction::FromRam) {
                const uint32_t data = m_emulator->ReadWord(addr & 0x1ffffc);
                m_emulator->m_gpu->Gp0(data);
            } else {
                const uint32_t data = m_emulator->m_gpu->GpuRead();
                m_emulator->WriteWord(addr & 0x1ffffc, data);
            }

            addr += channel->chcr.backward ? -4 : 4;
        } while (--words != 0);

        return;
    }

    if (channel->chcr.direction != Direction::FromRam) {
        Error("unimplemented gpu dma direction");
    }

    if (channel->chcr.sync_mode != SyncMode::LinkedList) {
        Error("unimplemented gpu dma sync mode");
    }

    for (;;) {
        const uint32_t entry = m_emulator->ReadWord(addr & 0x1ffffc);
        uint8_t count = entry >> 24;

        while (count-- != 0) {
            addr += 4;

            const uint32_t data = m_emulator->ReadWord(addr & 0x1ffffc);
            m_emulator->m_gpu->Gp0(data);
        }

        if ((entry & 0x800000) != 0) {
            break;
        }

        addr = entry & 0xffffff;
    }
}

void Dmac::StartTransferCdrom()
{
    const DmaChannel *channel = &m_channels[Channel::Cdrom];

    if (channel->chcr.direction != Direction::ToRam) {
        Error("unimplemented cdrom dma direction");
    }

    if (channel->chcr.sync_mode != SyncMode::Manual) {
        Error("unimplemented cdrom dma sync mode");
    }

    uint32_t addr = channel->madr.address;
    size_t words = channel->bcr.size;

    do {
        const uint32_t data = m_emulator->m_cdc->ReadDma();
        m_emulator->WriteWord(addr, data);
        addr = (addr + (channel->chcr.backward ? -4 : 4)) & 0xffffff;
    } while (--words != 0);
}

void Dmac::StartTransferSpu()
{
    const DmaChannel *channel = &m_channels[Channel::Spu];

    uint32_t addr = channel->madr.address;
    size_t words = channel->bcr.size * channel->bcr.count;

    if (channel->chcr.direction != Direction::FromRam) {
        Error("unimplemented spu dma direction");
    }

    if (channel->chcr.sync_mode != SyncMode::Block) {
        Error("unimplemented spu dma sync mode");
    }

    do {
        const uint32_t data = m_emulator->ReadWord(addr & 0x1ffffc);
        m_emulator->m_spu->WriteDma(data);

        addr += channel->chcr.backward ? -4 : 4;
    } while (--words != 0);
}

void Dmac::StartTransferOtc()
{
    const DmaChannel *channel = &m_channels[Channel::Otc];

    uint32_t addr = channel->madr.address;
    size_t words = channel->bcr.size;

    do {
        if (words == 1) {
            m_emulator->WriteWord(addr, 0xffffff);
            continue;
        }

        m_emulator->WriteWord(addr, addr - 4);
        addr = (addr - 4) & 0xffffff;
    } while (--words != 0);
} 

void Dmac::UpdateInterrupts()
{
    const bool old_irq = m_dicr.irq;

    const bool triggered = (m_dicr.enable & m_dicr.flag) != 0;
    m_dicr.irq = m_dicr.force || (m_dicr.master && triggered);

    if (!old_irq && m_dicr.irq) {
        m_emulator->m_intc->AssertInterrupt(Interrupt::Dma);
    }
}

}
