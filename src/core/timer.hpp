#pragma once

#include <cstddef>

#include <common/bitfield.hpp>
#include <common/types.hpp>

namespace Core
{

class Emulator;

template <std::size_t Index>
class Timer {
public:
    Timer(Emulator *emulator) : m_emulator(emulator) { Reset(); }

    void Reset();

    void Tick();

    u16 Read(u32 addr);
    void Write(u32 addr, u16 data);

private:
    union {
        u16 raw;

        BitField<u16, bool, 0, 1> sync_enable; //*
        BitField<u16, u16, 1, 2> sync_mode; //*
        BitField<u16, bool, 3, 1> target_reset; //
        BitField<u16, bool, 4, 1> target_irq_enable; //
        BitField<u16, bool, 5, 1> overflow_irq_enable; //
        BitField<u16, bool, 6, 1> repeat; //*
        BitField<u16, bool, 7, 1> toggle; //
        BitField<u16, u16, 8, 2> source; //*
        BitField<u16, bool, 10, 1> nirq; //
        BitField<u16, bool, 11, 1> target; //
        BitField<u16, bool, 12, 1> overflow; //
    } m_mode;

    u16 m_counter, m_prescaler, m_target;

    Emulator *m_emulator;
};

}
