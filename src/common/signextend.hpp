#pragma once

#include <cstddef>
#include <type_traits>

#include "bitsize.hpp"

template <std::size_t Bits, typename T>
constexpr T SignExtend(const T& v) noexcept
{
    static_assert(std::is_integral_v<T>, "T must be integral");
    static_assert(!std::is_same_v<T, bool>, "T must not be a boolean");
    static_assert(Bits != 0, "Bits cannot be zero");
    static_assert(BitSize<T>() >= Bits, "T cannot be smaller than Bits");

    struct S { std::make_signed_t<T> v: Bits; };
    return reinterpret_cast<const S *>(&v)->v;
}
