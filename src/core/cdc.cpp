#include <cstdint>
#include <cstring>
#include <filesystem>

#include <common/bit.hpp>

#include <spdlog/spdlog.h>

#include "cdc.hpp"
#include "emulator.hpp"
#include "error.hpp"
#include "intc.hpp"
#include "scheduler.hpp"

#include "disc/bin.hpp"

namespace Core
{

Cdc::Cdc(Emulator *emulator, const std::filesystem::path& disc)
    : m_emulator(emulator)
{
    Reset();

    if (disc.extension() == ".bin") {
        m_disc = std::make_unique<Bin>(disc);
    } else {
        Error("unsupported disc format {}", disc.extension().string());
    }
}

void Cdc::Reset()
{
    m_status.index = 0;
    m_status.adpbusy = false;
    m_status.prmempt = true;
    m_status.prmwrdy = true;
    m_status.rslrrdy = false;
    m_status.drqsts = false;
    m_status.busysts = false;

    m_setloc_unprocessed = true;

    m_stat.raw = m_mode.raw = 0;

    m_command2 = Command::Sync;

    m_parameter_fifo_size = m_response_fifo_size = m_data_fifo_size = 0;

    m_interrupt_enables = m_interrupt_flags = 0;
}

void Cdc::Tick()
{
    if (m_status.busysts && --m_command_counter == 0) {
        ExecuteCommand();
    }

    if (m_command2 != Command::Sync && --m_command2_counter == 0) {
        ExecuteCommandSecondResponse();
    }

    if (m_stat.drive_state == DriveState::Reading && --m_sector_counter == 0) {
        DeliverDataSector();
    }
}

uint8_t Cdc::Read(uint32_t addr)
{
    if ((addr & 0x3) == 0) {
        return m_status.raw;
    }

    const size_t index = 4 * ((addr - 1) & 0x3) + m_status.index;

    uint8_t data;

    switch (index) {
    case 1:
        if (m_response_fifo_size == 0) {
            Error("cdc response fifo underflow");
        }

        data = m_response_fifo[0];

        for (size_t i = 1; i < m_response_fifo_size; ++i) {
            m_response_fifo[i - 1] = m_response_fifo[i];
        }

        m_status.rslrrdy = --m_response_fifo_size > 0;
        return data;
    case 8: return 0xe0 | m_interrupt_enables;
    case 9: return 0xe0 | m_interrupt_flags;
    default: Error("read from unknown CDC reg 0x{:08x} index {}",
                                addr, m_status.index);
    }
}

void Cdc::Write(uint32_t addr, uint8_t data)
{
    if ((addr & 0x3) == 0) {
        m_status.index = data;
        return;
    }

    const size_t index = 4 * ((addr - 1) & 0x3) + m_status.index;
    size_t counter;

    switch (index) {
    case 0:
        if (m_status.busysts) {
            Error("sent cdc command whilst busy");
        }

        m_command = static_cast<Command>(data);
        counter = 25000;

        if (m_stat.motor_on) {
            counter *= 2;
        }

        if (m_command == Command::Init) {
            counter = 80000;
        }

        m_emulator->m_scheduler->AddEvent(
            Scheduler::Event::Type::CdCommand,
            Scheduler::Event::Mode::Once,
            counter,
            [=]() { ExecuteCommand(); }
        );

        m_status.busysts = true;
        break;
    case 3:
        spdlog::warn("unimplemented cdc volume");
        break;
    case 4:
        if (m_parameter_fifo_size >= ParameterFifoSize) {
            Error("cdc parameter fifo overflow");
        }

        m_parameter_fifo[m_parameter_fifo_size++] = data;
        m_status.prmempt = false;
        m_status.prmwrdy = m_parameter_fifo_size < ParameterFifoSize;
        break;
    case 5:
        m_interrupt_enables = data & 0x1f;
        break;
    case 6:
    case 7:
        spdlog::warn("unimplemented cdc volume");
        break;
    case 8:
        if (Bit::Check<5>(data)) {
            Error("unimplemented cdc smen");
        }

        if (Bit::Check<6>(data)) {
            Error("unimplemented cdc bfwr");
        }

        if (Bit::Check<7>(data)) {
            FillDataFifo();
        }

        break;
    case 9:
        m_interrupt_flags = ~(data & 0x1f);

        if (Bit::Check<6>(data)) {
            m_parameter_fifo_size = 0;
            m_status.prmempt = true;
            m_status.prmwrdy = true;
        }

        break;
    case 10:
        spdlog::warn("unimplemented cdc volume");
        break;
    case 11:
        spdlog::warn("unimplemented cdc apply volume");
        break;
    default: Error("write to unknown CDC reg 0x{:08x} index {}",
                                addr, m_status.index);
    }
}

uint32_t Cdc::ReadDma()
{
    if (m_data_fifo_size < 4) {
        Error("cdc data fifo underflow");
    }

    const uint32_t data = *reinterpret_cast<uint32_t *>(m_data_fifo.data());

    for (size_t i = 4; i < m_data_fifo_size; ++i) {
        m_data_fifo[i - 4] = m_data_fifo[i];
    }

    m_data_fifo_size -= 4;
    m_status.drqsts = m_data_fifo_size > 0;

    return data;
}

/* TODO: move this */
static inline uint8_t BcdToDecimal(uint8_t x)
{
    return x - 6 * (x >> 4);
}

void Cdc::ExecuteCommand()
{
    spdlog::debug("cdc command 0x{:02x}", m_command);

    size_t counter;

    switch(m_command) {
    case Command::GetStat:
        m_response_fifo[0] = m_stat.raw;

        m_response_fifo_size = 1;
        m_status.rslrrdy = true;
        break;
    case Command::SetLoc:
        m_setloc_timecode.minute = BcdToDecimal(m_parameter_fifo[0]);
        m_setloc_timecode.second = BcdToDecimal(m_parameter_fifo[1]);
        m_setloc_timecode.sector = BcdToDecimal(m_parameter_fifo[2]);

        m_setloc_unprocessed = true;

        m_response_fifo[0] = m_stat.raw;

        m_response_fifo_size = 1;
        m_status.rslrrdy = true;
        break;
    case Command::ReadN:
        m_stat.drive_state = DriveState::Reading;

        counter = 33868800 / 75;

        if (m_mode.drive_speed == DriveSpeed::Double) {
            counter /= 2;
        }

        m_emulator->m_scheduler->AddEvent(
            Scheduler::Event::Type::CdSector,
            Scheduler::Event::Mode::Manual,
            counter,
            [=]() {
                DeliverDataSector();

                std::size_t c = 33868800 / 75;

                if (m_mode.drive_speed == DriveSpeed::Double) {
                    c /= 2;
                }

                m_emulator->m_scheduler->RescheduleEvent(Scheduler::Event::Type::CdSector, c);

            }
        );

        if (m_setloc_unprocessed) {
            m_drive_timecode = m_setloc_timecode;
            m_setloc_unprocessed = false;
        }

        m_response_fifo[0] = m_stat.raw;

        m_response_fifo_size = 1;
        m_status.rslrrdy = true;
        break;
    case Command::Pause:
        m_response_fifo[0] = m_stat.raw;

        m_response_fifo_size = 1;
        m_status.rslrrdy = true;

        m_command2 = m_command;
        counter = 2160000;

        if (m_mode.drive_speed == DriveSpeed::Double) {
            counter /= 2;
        }

        if (m_stat.drive_state == DriveState::None) {
            counter = 7500;
            break;
        }

        m_emulator->m_scheduler->AddEvent(
            Scheduler::Event::Type::CdCommand2,
            Scheduler::Event::Mode::Once,
            counter,
            [=]() { ExecuteCommandSecondResponse(); }
        );

        if (m_stat.drive_state == DriveState::Reading) {
            m_emulator->m_scheduler->RemoveEvent(Scheduler::Event::Type::CdSector);
        }

        m_stat.drive_state = DriveState::None;
        break;
    case Command::Init: 
        m_response_fifo[0] = m_stat.raw;

        m_response_fifo_size = 1;
        m_status.rslrrdy = true;

        m_command2 = m_command;

        m_emulator->m_scheduler->AddEvent(
            Scheduler::Event::Type::CdCommand2,
            Scheduler::Event::Mode::Once,
            20000,
            [=]() { ExecuteCommandSecondResponse(); }
        );

        m_stat.motor_on = true;
        
        if (m_stat.drive_state == DriveState::Reading) {
            m_emulator->m_scheduler->RemoveEvent(Scheduler::Event::Type::CdSector);
        }

        m_stat.drive_state = DriveState::None;

        m_setloc_unprocessed = true;

        m_mode.raw = 0;
        break;
    case Command::Demute:
        m_response_fifo[0] = m_stat.raw;

        m_response_fifo_size = 1;
        m_status.rslrrdy = true;
        break;
    case Command::SetMode:
        m_mode.raw = m_parameter_fifo[0];

        m_response_fifo[0] = m_stat.raw;

        m_response_fifo_size = 1;
        m_status.rslrrdy = true;
        break;
    case Command::GetTn:
        m_response_fifo[0] = m_stat.raw;
        m_response_fifo[1] = 0x01;
        m_response_fifo[2] = 0x02;

        m_response_fifo_size = 3;
        m_status.rslrrdy = true;
        break;
    case Command::SeekL:
        m_stat.motor_on = true;
        
        if (m_stat.drive_state == DriveState::Reading) {
            m_emulator->m_scheduler->RemoveEvent(Scheduler::Event::Type::CdSector);
        }

        m_stat.drive_state = DriveState::Seeking;

        m_response_fifo[0] = m_stat.raw;

        m_response_fifo_size = 1;
        m_status.rslrrdy = true;

        m_command2 = m_command;

        m_emulator->m_scheduler->AddEvent(
            Scheduler::Event::Type::CdCommand2,
            Scheduler::Event::Mode::Once,
            20000,
            [=]() { ExecuteCommandSecondResponse(); }
        );

        break;
    case Command::Test:
        ExecuteTestCommand();
        break;
    case Command::GetId:
        m_response_fifo[0] = m_stat.raw;

        m_response_fifo_size = 1;
        m_status.rslrrdy = true;

        m_command2 = m_command;

        m_emulator->m_scheduler->AddEvent(
            Scheduler::Event::Type::CdCommand2,
            Scheduler::Event::Mode::Once,
            20000,
            [=]() { ExecuteCommandSecondResponse(); }
        );

        break;
    default: Error("unknown cdc command 0x{:02x}", m_command);
    }

    m_interrupt_flags = 0x3;

    /* TODO: edge-triggered interrupt */
    if ((m_interrupt_flags & m_interrupt_enables & 0x1f) != 0) {
        m_emulator->m_intc->AssertInterrupt(Interrupt::Cdrom);
        //spdlog::debug("cdc int={}", m_interrupt_flags);
    }

    m_parameter_fifo_size = 0;
    m_status.prmempt = true;
    m_status.prmwrdy = true;
    m_status.busysts = false;
}

void Cdc::ExecuteCommandSecondResponse()
{
    switch (m_command2) {
    case Command::Pause:
    case Command::Init:
        m_response_fifo[0] = m_stat.raw;

        m_response_fifo_size = 1;
        m_status.rslrrdy = true;

        m_interrupt_flags = 0x2;
        break;
    case Command::SeekL:
        m_drive_timecode = m_setloc_timecode;

        if (m_stat.drive_state == DriveState::Reading) {
            m_emulator->m_scheduler->RemoveEvent(Scheduler::Event::Type::CdSector);
        }

        m_stat.drive_state = DriveState::None;

        m_setloc_unprocessed = false;

        m_response_fifo[0] = m_stat.raw;

        m_response_fifo_size = 1;
        m_status.rslrrdy = true;

        m_interrupt_flags = 0x2;
        break;
    case Command::GetId:
        m_response_fifo[0] = 0x02;
        m_response_fifo[1] = 0x00;
        m_response_fifo[2] = 0x20;
        m_response_fifo[3] = 0x00;
        m_response_fifo[4] = 0x53;
        m_response_fifo[5] = 0x43;
        m_response_fifo[6] = 0x45;
        m_response_fifo[7] = 0x41;
        //m_response_fifo[0] = 0x08;
        //m_response_fifo[1] = 0x40;
        //m_response_fifo[2] = 0x00;
        //m_response_fifo[3] = 0x00;
        //m_response_fifo[4] = 0x00;
        //m_response_fifo[5] = 0x00;
        //m_response_fifo[6] = 0x00;
        //m_response_fifo[7] = 0x00;

        m_response_fifo_size = 8;
        m_status.rslrrdy = true;

        m_interrupt_flags = 0x2;
        //m_interrupt_flags = 0x5;
        break;
    default: Error("unknown cdc command 0x{:02x}", m_command2);
    }

    m_command2 = Command::Sync;

    /* TODO: edge-triggered interrupt */
    if ((m_interrupt_flags & m_interrupt_enables & 0x1f) != 0) {
        m_emulator->m_intc->AssertInterrupt(Interrupt::Cdrom);
        //spdlog::debug("cdc int={}", m_interrupt_flags);
    }
}

void Cdc::ExecuteTestCommand()
{
    if (m_parameter_fifo_size == 0) {
        Error("cdc parameter fifo size incorrect");
    }

    const uint8_t subcommand = m_parameter_fifo[0];

    switch (subcommand) {
    case 0x20:
        m_response_fifo[0] = 0x94;
        m_response_fifo[1] = 0x09;
        m_response_fifo[2] = 0x19;
        m_response_fifo[3] = 0xc0;

        m_response_fifo_size = 4;
        m_status.rslrrdy = true;
        break;
    default: Error("unknown cdc test sub-command 0x{:02x}", subcommand);
    }
}

void Cdc::DeliverDataSector()
{
    const uint8_t mm = m_drive_timecode.minute;
    const uint8_t ss = m_drive_timecode.second;
    const uint8_t ff = m_drive_timecode.sector;

    const size_t sector = 75 * (60 * mm + ss) + ff;

    if (sector >= 80 * 60 * 75) {
        Error("timecode past end of disk");
    }

    //spdlog::debug("delivering sector {:02}:{:02}:{:02}", mm, ss, ff);
    //spdlog::debug("lba = 0x{:x}", sector);

    m_disc->Read(m_sector_buffer.data(), sector);

    if (++m_drive_timecode.sector >= 75) {
        m_drive_timecode.sector = 0;

        if (++m_drive_timecode.second >= 60) {
            m_drive_timecode.second = 0;

            if (++m_drive_timecode.minute >= 80) {
                Error("moved past end of disk");
            }
        }
    }

    m_response_fifo[0] = m_stat.raw;

    m_response_fifo_size = 1;
    m_status.rslrrdy = true;

    /* TODO: pending interrupt flag */
    m_interrupt_flags = 0x1;

    /* TODO: edge-triggered interrupt */
    if ((m_interrupt_flags & m_interrupt_enables & 0x1f) != 0) {
        m_emulator->m_intc->AssertInterrupt(Interrupt::Cdrom);
        //spdlog::debug("cdc int={}", m_interrupt_flags);
    }   
}

void Cdc::FillDataFifo()
{
    const size_t start = (m_mode.sector_size == SectorSize::WholeSector) ? 12 : 24;
    const size_t length = (m_mode.sector_size == SectorSize::WholeSector) ? 2340 : 2048;

    std::memcpy(m_data_fifo.data(), &m_sector_buffer[start], length);

    m_data_fifo_size = length;
    m_status.drqsts = true;
}

}
