#include <cstdint>

#include "../error.hpp"
#include "digital.hpp"
#include "joypad.hpp"

namespace Core
{

Digital::Digital() : m_state(State::Idle)
{
    m_keystate.raw = 0xffff;
}

void Digital::SetKeystate(const Key& key, bool state)
{
    state = !state;

    switch (key) {
    case Key::Select: m_keystate.select = state; break;
    case Key::Start: m_keystate.start = state; break;
    case Key::Up: m_keystate.up = state; break;
    case Key::Down: m_keystate.down = state; break;
    case Key::Left: m_keystate.left = state; break;
    case Key::Right: m_keystate.right = state; break;
    case Key::Cross: m_keystate.cross = state; break;
    case Key::Circle: m_keystate.circle = state; break;
    case Key::Triangle: m_keystate.triangle = state; break;
    case Key::Square: m_keystate.square = state; break;
    case Key::L1: m_keystate.l1 = state; break;
    case Key::R1: m_keystate.r1 = state; break;
    case Key::L2: m_keystate.l2 = state; break;
    case Key::R2: m_keystate.r2 = state; break;
    }
}

uint8_t Digital::Transmit(uint8_t value, bool& ack)
{
    switch (m_state) {
    case State::Idle:
        if (value == 0x1) {
            m_state = State::IdLow;
            ack = false;
        }

        return 0xff;
    case State::IdLow:
        m_command = value;
        m_state = State::IdHigh;

        ack = false;
        return ControllerId;
    case State::IdHigh:
        switch (m_command) {
        case 0x42:
            m_state = State::ReadLow;
            ack = false;
            break;
        case 0x43:
            m_state = State::Idle;
            break;
        default: Error("unknown digital pad command 0x{:02x}", m_command);
        }

        return 0x5a;
    case State::ReadLow:
        m_state = State::ReadHigh;

        ack = false;
        return m_keystate.low;
    case State::ReadHigh:
        m_state = State::Idle;

        return m_keystate.high;
    default: Error("unimplemented digital pad state");
    }
}

}
