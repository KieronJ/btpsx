#include <cassert>
#include <cstdint>

#include <common/types.hpp>

#include "error.hpp"
#include "scheduler.hpp"

Scheduler::Scheduler()
{
    for (auto& event : m_events) {
        event.active = false;
    }

    auto idle_cb = []() { Error("idle event fired"); };
    AddEvent(Event::Type::Idle, Event::Mode::Once, INT64_MAX, idle_cb);
}

void Scheduler::UpdateEvents()
{
    while (NextEventTarget() <= 0) {
        const Event *event = m_event_list.front();
        event->callback();

        if (event->mode == Event::Mode::Once) {
            m_event_list.pop_front();
        } else if (event->mode == Event::Mode::Periodic) {
            RescheduleEvent(event->type, event->period);
        }
    }
}

void Scheduler::AddEvent(Event::Type type,
                         Event::Mode mode,
                         s64 ticks,
                         Event::Callback callback)
{
    assert(ticks >= 0);

    assert(type != Event::Type::Count);
    assert(!m_events[type].active);

    m_events[type].active = true;
    m_events[type].type = type;
    m_events[type].mode = mode;
    m_events[type].target = m_current_time + ticks;
    m_events[type].period = ticks;
    m_events[type].callback = callback;

    m_event_list.push_back(&m_events[type]);
        
    SortEvents();
    RecalcNextEventTarget();
}

void Scheduler::RemoveEvent(Event::Type type)
{
    assert(type != Event::Type::Count);
    assert(m_events[type].active);

    m_events[type].active = false;

    m_event_list.remove(&m_events[type]);
    RecalcNextEventTarget();
}

void Scheduler::RescheduleEvent(Event::Type type, std::size_t ticks)
{ 
    assert(ticks >= 0);

    assert(type != Event::Type::Count);
    assert(m_events[type].active);

    m_events[type].target += ticks;

    SortEvents();
    RecalcNextEventTarget();
}
