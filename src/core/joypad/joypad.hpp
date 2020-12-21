#ifndef CORE_JOYPAD_HPP
#define CORE_JOYPAD_HPP

#include <cstdint>

namespace Core
{

enum class Key {
    Select,
    Start,
    Up,
    Down,
    Left,
    Right,
    Cross,
    Circle,
    Triangle,
    Square,
    L1,
    R1,
    L2,
    R2
};

class Joypad {
public:
    virtual ~Joypad() = 0;

    virtual void SetKeystate(const Key& key, bool state) = 0;
    virtual uint8_t Transmit(uint8_t value, bool& ack) = 0;
};

}

#endif /* CORE_JOYPAD_HPP */
