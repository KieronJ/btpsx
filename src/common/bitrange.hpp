#pragma once

#include <cstddef>
#include <type_traits>

#include "bitsize.hpp"

template <std::size_t To, std::size_t From, typename T>
constexpr T BitRange(const T& v) noexcept
{
    static_assert(std::is_integral_v<T>, "T must be integral");
    static_assert(!std::is_same_v<T, bool>, "T cannot be a boolean");
    static_assert(To > From, "invalid range");
    static_assert(To < BitSize<T>(), "out of range");

    enum {
        Range = To - From + 1,
        Mask = (1 << Range) - 1,
    };

    return (v >> From) & Mask;
}
