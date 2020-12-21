#pragma once

#include <stdexcept>
#include <string>

namespace Common::Error
{

[[noreturn]] inline void Fatal(const std::string& message)
{
    throw std::runtime_error(message);
}

}
