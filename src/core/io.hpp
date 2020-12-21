#ifndef CORE_IO_HPP
#define CORE_IO_HPP

#include <cstdint>

#include <common/bitfield.hpp>

#include "joypad/joypad.hpp"

namespace Core
{

class Emulator;

class Io {
public:
    Io(Emulator *emulator, Joypad *joypad);

    void Reset();

    uint8_t Rx();
    void Tx(uint8_t value);

    uint16_t ReadStatus();

    void WriteMode(uint16_t value);

    uint16_t ReadControl();
    void WriteControl(uint16_t value);

    uint16_t m_baudrate;

private:
    union {
        uint32_t raw;

        BitField<uint32_t, bool, 0, 1> tx_ready1;
        BitField<uint32_t, bool, 1, 1> rx_has_data;
        BitField<uint32_t, bool, 2, 1> tx_ready2;
        BitField<uint32_t, bool, 7, 1> nack;
        BitField<uint32_t, bool, 9, 1> irq;
    } m_status;

    union {
        uint16_t raw;

        BitField<uint16_t, uint8_t, 0, 2> baudrate_factor;
    } m_mode;

    union {
        uint16_t raw;

        BitField<uint16_t, bool, 0, 1> tx_enable;
        BitField<uint16_t, bool, 1, 1> njoy_output;
        BitField<uint16_t, bool, 2, 1> rx_enable;
        BitField<uint16_t, bool, 4, 1> acknowledge;
        BitField<uint16_t, bool, 6, 1> reset;
        BitField<uint16_t, bool, 13, 1> slot;
    } m_control;

    uint8_t m_rx_data;

    uint8_t m_tx_data;
    bool m_tx_busy = false;

    Joypad *m_joypad;
    Emulator *m_emulator;
};

}

#endif /* CORE_IO_HPP */
