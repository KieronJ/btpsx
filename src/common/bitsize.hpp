#pragma once

#include <climits>
#include <cstddef>
#include <type_traits>

template <typename T>
constexpr std::size_t BitSize() noexcept
{
    if constexpr (std::is_same_v<T, bool>) {
        return 1;
    }

    return CHAR_BIT * sizeof(T);
}
