#include <cstddef>

#include <common/types.hpp>

#include <spdlog/spdlog.h>

#include "emulator.hpp"
#include "error.hpp"
#include "intc.hpp"
#include "timer.hpp"

namespace Core
{

template <std::size_t Index>
void Timer<Index>::Reset()
{
    m_counter = m_prescaler = m_mode.raw = m_target = 0;
    m_mode.nirq = true;
}

template <>
void Timer<1>::Tick()
{
    if (m_mode.source == 1 || m_mode.source == 3) {
        if ((++m_prescaler % 2100) != 0) {
            return;
        }
    }

    m_counter += 1;

    if (m_mode.target_irq_enable && m_counter == m_target) {
        m_mode.target = true;

        if (m_mode.toggle) {
            m_mode.nirq = !m_mode.nirq;

            if (!m_mode.nirq) {
                m_emulator->m_intc->AssertInterrupt(Interrupt::Timer1);
            }
        } else {
            m_mode.nirq = false;
            m_emulator->m_intc->AssertInterrupt(Interrupt::Timer1);
        }

        if (m_mode.target_reset) {
            m_counter = 0;
        }
    } else if (m_mode.overflow_irq_enable && m_counter == 0) {
        m_mode.overflow = true;

        if (m_mode.toggle) {
            m_mode.nirq = !m_mode.nirq;

            if (!m_mode.nirq) {
                m_mode.nirq = false;
                m_emulator->m_intc->AssertInterrupt(Interrupt::Timer1);
            }
        } else {
            m_emulator->m_intc->AssertInterrupt(Interrupt::Timer1);
        }
    }
}

template <std::size_t Index>
void Timer<Index>::Tick()
{
    Interrupt i;

    switch (Index) {
    case 0:
        i = Interrupt::Timer0;
        break;
    case 1:
        i = Interrupt::Timer1;
        break;
    case 2:
        i = Interrupt::Timer2;
        break;
    default: Error("invalid timer({})", Index);
    }

    if ((++m_prescaler % 8) != 0) {
        return;
    }

    /* TODO: source */
    m_counter += 1;

    if (m_mode.target_irq_enable && m_counter == m_target) {
        m_mode.target = true;

        if (m_mode.toggle) {
            m_mode.nirq = !m_mode.nirq;

            if (!m_mode.nirq) {
                m_emulator->m_intc->AssertInterrupt(i);
            }
        } else {
            m_mode.nirq = false;
            m_emulator->m_intc->AssertInterrupt(i);
        }

        if (m_mode.target_reset) {
            m_counter = 0;
        }
    } else if (m_mode.overflow_irq_enable && m_counter == 0) {
        m_mode.overflow = true;

        if (m_mode.toggle) {
            m_mode.nirq = !m_mode.nirq;

            if (!m_mode.nirq) {
                m_mode.nirq = false;
                m_emulator->m_intc->AssertInterrupt(i);
            }
        } else {
            m_emulator->m_intc->AssertInterrupt(i);
        }
    }
}

template <std::size_t Index>
u16 Timer<Index>::Read(u32 addr)
{
    switch (addr & 0xf) {
    case 0x0: return m_counter;
    case 0x4: return m_mode.raw;
    case 0x8: return m_target;
    default: Error("read from unknown timer reg 0x{:08x}", addr);
    }
}

template <std::size_t Index>
void Timer<Index>::Write(u32 addr, u16 data)
{
    switch (addr & 0xf) {
    case 0x0:
        m_counter = data;
        break;
    case 0x4:
        m_mode.raw = (m_mode.raw & 0x1800) | (data & 0x3ff);
        m_mode.nirq = true;

        if (m_mode.sync_enable) {
            spdlog::warn("unimplemented timer({}) sync enabled", Index);
        }

        if (m_mode.toggle) {
            spdlog::warn("unimplemented timer({}) toggle enabled", Index);
        }

        m_counter = 0;
        m_prescaler = 0;
        break;
    case 0x8:
        m_target = data;
        break;
    default: Error("write to unknown timer reg 0x{:08x}", addr);
    }
}

template class Timer<0>;
template class Timer<1>;
template class Timer<2>;

}
