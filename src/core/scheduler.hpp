#pragma once

#include <array>
#include <cassert>
#include <functional>
#include <list>

#include <common/types.hpp>

class Scheduler {
public:
    /* TODO: move out of this file */
    struct Event {
        enum Type : u8 {
            Idle,
            Vblank,
            Spu,
            CdCommand,
            CdCommand2,
            CdSector,
            IoAcknowledge,
            Count
        };

        enum class Mode : u8 {
            Once,
            Periodic,
            Manual
        };

        bool active;
        Type type;
        Mode mode;
        s64 target, period;
        std::function<void()> callback;

        using Callback = std::function<void()>;
    };

    Scheduler();

    void UpdateEvents();

    void AddEvent(Event::Type type, Event::Mode mode, s64 ticks, Event::Callback callback);
    void RemoveEvent(Event::Type type);
    void RescheduleEvent(Event::Type type, std::size_t ticks);

    inline void Tick(s64 ticks)
    {
        assert(ticks >= 0);

        m_current_time += ticks;
        m_next_event_target -= ticks;
    }

    inline s64 CurrentTime() const { return m_current_time; }
    inline s64 NextEventTarget() const { return m_next_event_target; }

private:
    inline void SortEvents()
    {
        m_event_list.sort([](const Event *a, const Event *b) {
            return a->target < b->target;
        });
    }

    inline void RecalcNextEventTarget()
    {
        m_next_event_target = m_event_list.front()->target - m_current_time;
        //m_next_event_target = std::min(512l, m_next_event_target);
    }

    s64 m_current_time = 0;
    s64 m_next_event_target;

    std::array<Event, Event::Type::Count> m_events;
    std::list<Event *> m_event_list;
};
