#pragma once

#include <string>

#include <common/error.hpp>

#include <spdlog/spdlog.h>

template <typename ... Args>
[[noreturn]] inline void Error(const std::string& format, const Args& ... args)
{
    spdlog::error(format, args...);
    Common::Error::Fatal("internal error");
}
