#pragma once

#include <cstddef>
#include <type_traits>

#include "bitsize.hpp"

namespace Bit
{

template <std::size_t Bit, typename T>
constexpr bool Check(const T& v) noexcept
{
    static_assert(std::is_integral_v<T>, "T must be integral");
    static_assert(BitSize<T>() > Bit, "Bit out of range");

    if constexpr (std::is_same_v<T, bool>) {
        return v;
    }

    return (v & (1 << Bit)) != 0;
}

template <std::size_t Bit, typename T>
inline void Set(T& v) noexcept
{
    static_assert(std::is_integral_v<T>, "T must be integral");
    static_assert(BitSize<T>() > Bit, "Bit out of range");

    if constexpr (std::is_same_v<T, bool>) {
        v = true;
        return;
    }

    v |= (1 << Bit);
}

template <std::size_t Bit, typename T>
inline void Clear(T& v) noexcept
{
    static_assert(std::is_integral_v<T>, "T must be integral");
    static_assert(BitSize<T>() > Bit, "Bit out of range");

    if constexpr (std::is_same_v<T, bool>) {
        v = false;
        return;
    }

    v &= ~(1 << Bit);
}

template <std::size_t Bit, typename T>
inline void Toggle(T& v) noexcept
{
    static_assert(std::is_integral_v<T>, "T must be integral");
    static_assert(BitSize<T>() > Bit, "Bit out of range");

    if constexpr (std::is_same_v<T, bool>) {
        v = !v;
        return;
    }

    v ^= (1 << Bit);
}

}
