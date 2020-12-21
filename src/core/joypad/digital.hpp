#ifndef CORE_JOYPAD_DIGTAL_HPP
#define CORE_JOYPAD_DIGITAL_HPP

#include <cstddef>
#include <cstdint>

#include <common/bitfield.hpp>

#include "joypad.hpp"

namespace Core
{

class Digital : public Joypad {
public:
    Digital();

    void SetKeystate(const Key& key, bool state) override;
    uint8_t Transmit(uint8_t data, bool& ack) override;
private:
    static constexpr uint8_t ControllerId = 0x41;

    union {
        uint16_t raw;

        BitField<uint16_t, uint8_t, 0, 8> low;
        BitField<uint16_t, uint8_t, 8, 8> high;

        BitField<uint16_t, bool, 0, 1> select;
        BitField<uint16_t, bool, 3, 1> start;
        BitField<uint16_t, bool, 4, 1> up;
        BitField<uint16_t, bool, 5, 1> right;
        BitField<uint16_t, bool, 6, 1> down;
        BitField<uint16_t, bool, 7, 1> left;
        BitField<uint16_t, bool, 8, 1> l2;
        BitField<uint16_t, bool, 9, 1> r2;
        BitField<uint16_t, bool, 10, 1> l1;
        BitField<uint16_t, bool, 11, 1> r1;
        BitField<uint16_t, bool, 12, 1> triangle;
        BitField<uint16_t, bool, 13, 1> circle;
        BitField<uint16_t, bool, 14, 1> cross;
        BitField<uint16_t, bool, 15, 1> square;
    } m_keystate;

    enum class State { Idle, IdLow, IdHigh, ReadLow, ReadHigh };

    uint8_t m_command;
    State m_state;
};

}

#endif /* CORE_JOYPAD_DIGITAL_HPP */
