#pragma once

#include <cstddef>
#include <type_traits>

#include "bitsize.hpp"
#include "signextend.hpp"

template <typename B, typename T, std::size_t Index, std::size_t Bits>
struct BitField {
    static_assert(std::is_integral_v<B>, "B must be integral");
    static_assert(Bits != 0, "Bits cannot be zero");
    static_assert(BitSize<B>() >= Index + Bits, "B cannot be smaller than field");
    static_assert(BitSize<T>() >= Bits, "T cannot be smaller than field");

    inline operator T() const noexcept
    {
        const B v = GetValue();

        if constexpr (std::is_same_v<T, bool>) {
            return !!v;
        }

        if constexpr (std::is_signed_v<T>) {
            return static_cast<T>(SignExtend<Bits>(v));
        }

        return static_cast<T>(v);
    }

    inline BitField& operator=(const T& v) noexcept
    {
        SetValue(static_cast<B>(v));
        return *this;
    }

    inline BitField& operator++()
    {
        SetValue(GetValue() + 1);
        return *this;
    }

    inline BitField& operator--()
    {
        SetValue(GetValue() - 1);
        return *this;
    }

    inline B GetValue() const noexcept
    {
        return (value >> Index) & Mask;
    }

    inline void SetValue(const B& v) noexcept
    {
        value &= ~(Mask << Index);
        value |= (v & Mask) << Index;
    }

    B value;
    enum : B { Mask = (B(1) << Bits) - 1 };
};
