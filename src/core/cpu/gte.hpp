#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

#include <common/bitfield.hpp>
#include <common/types.hpp>

namespace Cpu
{

class Gte {
public:
    void Execute(u32 i);

    u32 ReadData(std::size_t index) const;
    void WriteData(std::size_t index, u32 value);

    u32 ReadControl(std::size_t index) const;
    void WriteControl(std::size_t index, u32 value);

private:
    u32 DecompressColour() const;

    void Nclip();

    template <bool T>
    void Dpc();

    void Mvmva();

    template <std::size_t V>
    void Ncd();

    void Avsz3();

    template <std::size_t V, bool Depth>
    void Rtp();

    u32 RtpUnrDivide(u32 h, u32 sz3);

    template <std::size_t Mac>
    s32 SetMac(s64 value);

    template <std::size_t Ir>
    void SetIr(s32 value);

    void PushSx(s32 value);
    void PushSy(s32 value);
    void PushSz(s32 value);

    void PushRgb(u8 r, u8 g, u8 b, u8 c);

    static constexpr std::size_t UnrTableSize = 0x101;
    using UnrTable = std::array<u8, UnrTableSize>;

    constexpr int GetUnrTableIndex(int i)
    {
        /* TODO: shouldn't this be std::max()? */
        return std::max(0, (0x40000 / (i + 0x100) + 1) / 2 - 0x101);
    }

    constexpr UnrTable GenerateUnrTable()
    {
        UnrTable table = {};
    
        [&]<std::size_t...i>(std::index_sequence<i...>)
        {
            ((table[i] = GetUnrTableIndex(i)) , ...);
        }
        (std::make_index_sequence<UnrTableSize>{});
    
        return table;
    }

    UnrTable RtpUnrTable = GenerateUnrTable();

    template <typename T>
    struct Matrix {
        T m[3][3];
    };

    template <typename T>
    union Vector {
        struct { T x, y, z; };
        struct { T r, g, b; };
    };

    union Colour {
        u32 raw;

        BitField<u32, u8, 0, 8> r;
        BitField<u32, u8, 8, 8> g;
        BitField<u32, u8, 16, 8> b;
        BitField<u32, u8, 24, 8> c;
    };

    s32 m_lm;
    enum class TranslationSel { TR, BK, FC, None } m_tv;
    enum class VectorSel { V0, V1, V2, IR } m_mv;
    enum class MatrixSel { RT, LLM, LCM, Reserved } m_mx;
    std::size_t m_sf;

    union Command {
        u32 raw;

        BitField<u32, u8, 0, 6> op;
        BitField<u32, bool, 10, 1> lm;
        BitField<u32, TranslationSel, 13, 2> tv;
        BitField<u32, VectorSel, 15, 2> mv;
        BitField<u32, MatrixSel, 17, 2> mx;
        BitField<u32, bool, 19, 1> sf;
    };

    Vector<s16> m_v[3];

    Colour m_colour;

    u16 m_otz;

    s16 m_ir0;
    Vector<s16> m_ir;

    s16 m_sx[3], m_sy[3];
    u16 m_sz[4];

    Colour m_rgb[3];

    u32 m_res;
 
    s32 m_mac0;
    Vector<s32> m_mac;

    u32 m_lzcs, m_lzcr;

    Matrix<s16> m_rt;
    Matrix<s16> m_llm;
    Matrix<s16> m_lcm;

    Vector<s32> m_tr;
    Vector<s32> m_bk;
    Vector<s32> m_fc;

    s32 m_ofx, m_ofy;

    u16 m_h;

    s16 m_dqa;
    s32 m_dqb;

    s16 m_zsf3, m_zsf4;

    union {
        u32 raw;

        BitField<u32, bool, 12, 1> h;
        BitField<u32, bool, 13, 1> g2;
        BitField<u32, bool, 14, 1> g1;
        BitField<u32, bool, 15, 1> fn;
        BitField<u32, bool, 16, 1> fp;
        BitField<u32, bool, 17, 1> e;
        BitField<u32, bool, 18, 1> d;
        BitField<u32, bool, 19, 1> c3;
        BitField<u32, bool, 20, 1> c2;
        BitField<u32, bool, 21, 1> c1;
        BitField<u32, bool, 22, 1> b3;
        BitField<u32, bool, 23, 1> b2;
        BitField<u32, bool, 24, 1> b1;
        BitField<u32, bool, 25, 1> an3;
        BitField<u32, bool, 26, 1> an2;
        BitField<u32, bool, 27, 1> an1;
        BitField<u32, bool, 28, 1> ap3;
        BitField<u32, bool, 29, 1> ap2;
        BitField<u32, bool, 30, 1> ap1;
        BitField<u32, bool, 31, 1> checksum;
    } m_flags;
};

}
