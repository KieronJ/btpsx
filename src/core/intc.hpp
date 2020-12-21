#ifndef CORE_INTC_HPP
#define CORE_INTC_HPP

#include <cstdint>

namespace Core
{

class Emulator;

enum class Interrupt {
    Vblank,
    Gpu,
    Cdrom,
    Dma,
    Timer0,
    Timer1,
    Timer2,
    Controller,
    Sio,
    Spu,
    Pio
};

class Intc {
public:
    Intc(Emulator *emulator) : m_emulator(emulator) { Reset(); }

    inline void Reset()
    {
        m_status = m_mask = 0;
        Update();
    }

    inline void AssertInterrupt(Interrupt i)
    {
        m_status |= 1 << static_cast<uint32_t>(i);
        Update();
    }

    inline uint32_t ReadStatus() const
    {
        return m_status;
    }

    inline void WriteStatus(uint32_t data)
    {
        m_status &= data;
        Update();
    }

    uint32_t ReadMask() const
    {
        return m_mask;
    }

    inline void WriteMask(uint32_t data)
    {
        m_mask = data & 0x7ff;
        Update();
    }

private:
    void Update();
 
    uint32_t m_status, m_mask;

    Emulator *m_emulator;
};

}

#endif /* CORE_INTC_HPP */
