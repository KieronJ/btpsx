#include "emulator.hpp"
#include "intc.hpp"

//#include <spdlog/spdlog.h>

namespace Core
{

void Intc::Update()
{
    bool state = (m_status & m_mask & 0x7ff) != 0;
    m_emulator->m_cpu->AssertInterrupt(state);

    //if ((m_status & m_mask & 0x7fe) != 0) {
    //    spdlog::info("interrupt raised 0x{:04x}", m_status & m_mask & 0x7ff);
    //}
}

}
