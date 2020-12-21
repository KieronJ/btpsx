#include <cassert>
#include <cstdint>

#include "emulator.hpp"
#include "intc.hpp"
#include "io.hpp"
#include "joypad/joypad.hpp"
#include "scheduler.hpp"

namespace Core
{

Io::Io(Emulator *emulator, Joypad *joypad)
    : m_joypad(joypad),
      m_emulator(emulator)
{
    Reset();
}

void Io::Reset()
{
    m_tx_busy = false;
    m_status.tx_ready1 = true;
    m_status.rx_has_data = false;
    m_status.tx_ready2 = true;
    m_status.nack = false;
    m_status.irq = false;
}

uint8_t Io::Rx()
{
    const uint8_t data = m_rx_data;

    m_status.rx_has_data = false;
    return data;
}

void Io::Tx(uint8_t value)
{
    assert(!m_tx_busy);

    m_tx_data = value;

    auto io_acknowledge_cb = [=]() { 
        bool nack = true;

        m_rx_data = m_joypad->Transmit(m_tx_data, nack);
        m_status.tx_ready1 = true;
        m_status.rx_has_data = true;
        m_status.tx_ready2 = true;
        m_status.nack = nack;

        if (!nack && !m_status.irq) {
            m_status.irq = true;
            m_emulator->m_intc->AssertInterrupt(Interrupt::Controller);
        }

        m_tx_busy = false;
    };

    m_emulator->m_scheduler->AddEvent(
        Scheduler::Event::Type::IoAcknowledge,
        Scheduler::Event::Mode::Once,
        /* TODO: ACK is pulled low some time after the transfer is complete */
        8 * (m_baudrate & ~1),
        io_acknowledge_cb
    );

    m_status.tx_ready1 = false;
    m_status.tx_ready2 = false;

    m_tx_busy = true;
}

uint16_t Io::ReadStatus()
{
    return m_status.raw;
}

void Io::WriteMode(uint16_t value)
{
    m_mode.raw = value & 0x13f;
}

uint16_t Io::ReadControl()
{
    return m_control.raw;
}

void Io::WriteControl(uint16_t value)
{
    m_control.raw = value & 0x3f7f;

    if (m_control.acknowledge) {
        m_status.irq = false;
    }
}

}
