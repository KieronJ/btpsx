#include <algorithm>
#include <cstddef>

#include <common/bitrange.hpp>
#include <common/signextend.hpp>
#include <common/types.hpp>

#include <spdlog/spdlog.h>

#include "../error.hpp"
#include "gte.hpp"

namespace Cpu
{

void Gte::Execute(u32 i)
{
    Command command;
    command.raw = i;

    m_lm = command.lm ? 0 : -0x8000;
    m_tv = command.tv;
    m_mv = command.mv;
    m_mx = command.mx;
    m_sf = command.sf ? 12 : 0;

    m_flags.raw = 0;

    switch (command.op) {
    case 0x06:
        Nclip();
        break;
    case 0x10:
        Dpc<false>();
        break;
    case 0x12:
        Mvmva();
        break;
    case 0x13:
        Ncd<0>();
        break;
    case 0x2d:
        Avsz3();
        break;
    case 0x30:
        Rtp<0, false>();
        Rtp<1, false>();
        Rtp<2, true>();
        break;
    default: spdlog::warn("unknown gte command 0x{:x}", command.op);
    }

    m_flags.checksum = (m_flags.raw & 0x7f87e000) != 0;
}

u32 Gte::DecompressColour() const
{
    s32 irx = m_ir.x;
    s32 iry = m_ir.y;
    s32 irz = m_ir.z;

    irx = std::clamp(irx, 0, 0xf80) >> 7;
    iry = std::clamp(iry, 0, 0xf80) >> 2;
    irz = std::clamp(irz, 0, 0xf80) << 3;

    return irx | iry | irz;
}

u32 Gte::ReadData(std::size_t index) const
{
    switch (index) {
    case 0:  return static_cast<u16>(m_v[0].x) | m_v[0].y << 16;
    case 1:  return static_cast<u16>(m_v[0].z);
    case 2:  return static_cast<u16>(m_v[1].x) | m_v[1].y << 16;
    case 3:  return static_cast<u16>(m_v[1].z);
    case 4:  return static_cast<u16>(m_v[2].x) | m_v[2].y << 16;
    case 5:  return static_cast<u16>(m_v[2].z);
    case 6:  return m_colour.raw;
    case 7:  return m_otz;
    case 8:  return m_ir0;
    case 9:  return m_ir.x;
    case 10: return m_ir.y;
    case 11: return m_ir.z;
    case 12: return static_cast<u16>(m_sx[0]) | m_sy[0] << 16;
    case 13: return static_cast<u16>(m_sx[1]) | m_sy[1] << 16;
    case 14: case 15: return static_cast<u16>(m_sx[2]) | m_sy[2] << 16;
    case 16: return m_sz[0];
    case 17: return m_sz[1];
    case 18: return m_sz[2];
    case 19: return m_sz[3];
    case 20: return m_rgb[0].raw;
    case 21: return m_rgb[1].raw;
    case 22: return m_rgb[2].raw;
    case 23: return m_res;
    case 24: return m_mac0;
    case 25: return m_mac.x;
    case 26: return m_mac.y;
    case 27: return m_mac.z;
    case 28: case 29: return DecompressColour();
    case 30: return m_lzcs;
    case 31: return m_lzcr;
    default: Error("read from unknown gte reg {}", index);
    }
}

static inline std::size_t CountLeadingOnes(s32 value)
{
    std::size_t count = 0;

    while (value < 0) {
        ++count;
        value <<= 1;
    }

    return count;
}

void Gte::WriteData(std::size_t index, u32 value)
{
    switch (index) {
    case 0:  m_v[0].x = value; m_v[0].y = value >> 16; break;
    case 1:  m_v[0].z = value; break;
    case 2:  m_v[1].x = value; m_v[1].y = value >> 16; break;
    case 3:  m_v[1].z = value; break;
    case 4:  m_v[2].x = value; m_v[2].y = value >> 16; break;
    case 5:  m_v[2].z = value; break;
    case 6:  m_colour.raw = value; break;
    case 7:  m_otz = value; break;
    case 8:  m_ir0 = value; break;
    case 9:  m_ir.x = value; break;
    case 10: m_ir.y = value; break;
    case 11: m_ir.z = value; break;
    case 12: m_sx[0] = value; m_sy[0] = value >> 16; break;
    case 13: m_sx[1] = value; m_sy[1] = value >> 16; break;
    case 14: m_sx[2] = value; m_sy[2] = value >> 16; break;
    case 15:
        m_sx[0] = m_sx[1];
        m_sx[1] = m_sx[2];
        m_sx[2] = value;

        m_sy[0] = m_sy[1];
        m_sy[1] = m_sy[2];
        m_sy[2] = value >> 16;
        break;
    case 16: m_sz[0] = value; break;
    case 17: m_sz[1] = value; break;
    case 18: m_sz[2] = value; break;
    case 19: m_sz[3] = value; break;
    case 20: m_rgb[0].raw = value; break;
    case 21: m_rgb[1].raw = value; break;
    case 22: m_rgb[2].raw = value; break;
    case 23: m_res = value; break;
    case 24: m_mac0 = value; break;
    case 25: m_mac.x = value; break;
    case 26: m_mac.y = value; break;
    case 27: m_mac.z = value; break;
    case 28:
        m_ir.x = (value & 0x1f) << 7;
        m_ir.y = (value & 0x3e0) << 2;
        m_ir.z = (value & 0x7c00) >> 3;
        break;
    case 29: break;
    case 30:
        m_lzcs = value;

        if (value == 0) {
            m_lzcr = 32;
            break;
        }

        m_lzcr = (value > 0) ? __builtin_clz(value) : CountLeadingOnes(value);
        break;
    case 31: break;
    default: Error("write to unknown gte reg {}", index);
    }
}

u32 Gte::ReadControl(std::size_t index) const
{
    switch (index) {
    case 0:  return static_cast<u16>(m_rt.m[0][0]) | m_rt.m[0][1] << 16;
    case 1:  return static_cast<u16>(m_rt.m[0][2]) | m_rt.m[1][0] << 16;
    case 2:  return static_cast<u16>(m_rt.m[1][1]) | m_rt.m[1][2] << 16;
    case 3:  return static_cast<u16>(m_rt.m[2][0]) | m_rt.m[2][1] << 16;
    case 4:  return static_cast<u16>(m_rt.m[2][2]);
    case 5:  return m_tr.x;
    case 6:  return m_tr.y;
    case 7:  return m_tr.z;
    case 8:  return static_cast<u16>(m_llm.m[0][0]) | m_llm.m[0][1] << 16;
    case 9:  return static_cast<u16>(m_llm.m[0][2]) | m_llm.m[1][0] << 16;
    case 10: return static_cast<u16>(m_llm.m[1][1]) | m_llm.m[1][2] << 16;
    case 11: return static_cast<u16>(m_llm.m[2][0]) | m_llm.m[2][1] << 16;
    case 12: return static_cast<u16>(m_llm.m[2][2]);
    case 13: return m_bk.r;
    case 14: return m_bk.g;
    case 15: return m_bk.b;
    case 16: return static_cast<u16>(m_lcm.m[0][0]) | m_lcm.m[0][1] << 16;
    case 17: return static_cast<u16>(m_lcm.m[0][2]) | m_lcm.m[1][0] << 16;
    case 18: return static_cast<u16>(m_lcm.m[1][1]) | m_lcm.m[1][2] << 16;
    case 19: return static_cast<u16>(m_lcm.m[2][0]) | m_lcm.m[2][1] << 16;
    case 20: return static_cast<u16>(m_lcm.m[2][2]);
    case 21: return m_fc.r;
    case 22: return m_fc.g;
    case 23: return m_fc.b;
    case 24: return m_ofx;
    case 25: return m_ofy;
    case 26: return static_cast<s16>(m_h);
    case 27: return m_dqa;
    case 28: return m_dqb;
    case 29: return m_zsf3;
    case 30: return m_zsf4;
    case 31: return m_flags.raw;
    default: Error("read from unknown gte reg {}", 32 + index);
    }
}

void Gte::WriteControl(std::size_t index, u32 value)
{
    switch (index) {
    case 0:  m_rt.m[0][0] = value; m_rt.m[0][1] = value >> 16; break;
    case 1:  m_rt.m[0][2] = value; m_rt.m[1][0] = value >> 16; break;
    case 2:  m_rt.m[1][1] = value; m_rt.m[1][2] = value >> 16; break;
    case 3:  m_rt.m[2][0] = value; m_rt.m[2][1] = value >> 16; break;
    case 4:  m_rt.m[2][2] = value; break;
    case 5:  m_tr.x = value; break;
    case 6:  m_tr.y = value; break;
    case 7:  m_tr.z = value; break;
    case 8:  m_llm.m[0][0] = value; m_llm.m[0][1] = value >> 16; break;
    case 9:  m_llm.m[0][2] = value; m_llm.m[1][0] = value >> 16; break;
    case 10: m_llm.m[1][1] = value; m_llm.m[1][2] = value >> 16; break;
    case 11: m_llm.m[2][0] = value; m_llm.m[2][1] = value >> 16; break;
    case 12: m_llm.m[2][2] = value; break;
    case 13: m_bk.r = value; break;
    case 14: m_bk.g = value; break;
    case 15: m_bk.b = value; break;
    case 16: m_lcm.m[0][0] = value; m_lcm.m[0][1] = value >> 16; break;
    case 17: m_lcm.m[0][2] = value; m_lcm.m[1][0] = value >> 16; break;
    case 18: m_lcm.m[1][1] = value; m_lcm.m[1][2] = value >> 16; break;
    case 19: m_lcm.m[2][0] = value; m_lcm.m[2][1] = value >> 16; break;
    case 20: m_lcm.m[2][2] = value; break;
    case 21: m_fc.r = value; break;
    case 22: m_fc.g = value; break;
    case 23: m_fc.b = value; break;
    case 24: m_ofx = value; break;
    case 25: m_ofy = value; break;
    case 26: m_h = value; break;
    case 27: m_dqa = value; break;
    case 28: m_dqb = value; break;
    case 29: m_zsf3 = value; break;
    case 30: m_zsf4 = value; break;
    case 31:
        m_flags.raw = value & 0x7ffff000;

        if ((m_flags.raw & 0x7f87e000) != 0) {
            m_flags.checksum = true;
        }

        break;
    default: Error("write to unknown gte reg {}", 32 + index);
    }
}

void Gte::Nclip()
{
    const s64 x0 = m_sx[0];
    const s64 x1 = m_sx[1];
    const s64 x2 = m_sx[2];

    const s64 y0 = m_sy[0];
    const s64 y1 = m_sy[1];
    const s64 y2 = m_sy[2];

    SetMac<0>(x0 * y1 + x1 * y2 + x2 * y0 - x0 * y2 - x1 * y0 - x2 * y1);
}

template <bool T>
void Gte::Dpc()
{
    const s64 vx = m_v[0].x;
    const s64 vy = m_v[0].y;
    const s64 vz = m_v[0].z;

    const s64 ir1 = m_ir.x;
    const s64 ir2 = m_ir.y;
    const s64 ir3 = m_ir.z;

    const s64 llm11 = m_llm.m[0][0];
    const s64 llm12 = m_llm.m[0][1];
    const s64 llm13 = m_llm.m[0][2];

    const s64 llm21 = m_llm.m[1][0];
    const s64 llm22 = m_llm.m[1][1];
    const s64 llm23 = m_llm.m[1][2];

    const s64 llm31 = m_llm.m[2][0];
    const s64 llm32 = m_llm.m[2][1];
    const s64 llm33 = m_llm.m[2][2];

    const s64 lcm11 = m_lcm.m[0][0];
    const s64 lcm12 = m_lcm.m[0][1];
    const s64 lcm13 = m_lcm.m[0][2];

    const s64 lcm21 = m_lcm.m[1][0];
    const s64 lcm22 = m_lcm.m[1][1];
    const s64 lcm23 = m_lcm.m[1][2];

    const s64 lcm31 = m_lcm.m[2][0];
    const s64 lcm32 = m_lcm.m[2][1];
    const s64 lcm33 = m_lcm.m[2][2];

    SetIr<1>(SetMac<1>(vx * llm11 + vy * llm12 + vz * llm13));
    SetIr<2>(SetMac<2>(vx * llm21 + vy * llm22 + vz * llm23));
    SetIr<3>(SetMac<3>(vx * llm31 + vy * llm32 + vz * llm33));

    SetIr<1>(SetMac<1>(vx * lcm11 + vy * lcm12 + vz * lcm13));
    SetIr<2>(SetMac<2>(vx * lcm21 + vy * lcm22 + vz * lcm23));
    SetIr<3>(SetMac<3>(vx * lcm31 + vy * lcm32 + vz * lcm33));

    PushRgb(m_colour.r, m_colour.g, m_colour.b, m_colour.c);
}

void Gte::Mvmva()
{
    s64 tx; s64 ty; s64 tz;

    s64 vx; s64 vy; s64 vz;

    s64 m11; s64 m12; s64 m13;
    s64 m21; s64 m22; s64 m23;
    s64 m31; s64 m32; s64 m33;

    switch (m_tv) {
    case TranslationSel::TR:
        tx = m_tr.x; ty = m_tr.y; tz = m_tr.z;
        break;
    case TranslationSel::BK:
        tx = m_bk.r; ty = m_bk.g; tz = m_bk.b;
        break;
    case TranslationSel::None:
        tx = 0; ty = 0; tz = 0;
        break;
    default: Error("invalid translation vector specifier");
    }

    switch (m_mv) {
    case VectorSel::V0:
        vx = m_v[0].x; vy = m_v[0].y; vz = m_v[0].z;
        break;
    case VectorSel::V1:
        vx = m_v[1].x; vy = m_v[1].y; vz = m_v[1].z;
        break;
    case VectorSel::V2:
        vx = m_v[2].x; vy = m_v[2].y; vz = m_v[2].z;
        break;
    case VectorSel::IR:
        vx = m_ir.x; vy = m_ir.y; vz = m_ir.z;
        break;
    default: Error("invalid multiplication vector specifier");
    }

    switch (m_mx) {
    case MatrixSel::RT:
        m11 = m_rt.m[0][0]; m12 = m_rt.m[0][1]; m13 = m_rt.m[0][2];
        m21 = m_rt.m[1][0]; m22 = m_rt.m[1][1]; m23 = m_rt.m[1][2];
        m31 = m_rt.m[2][0]; m32 = m_rt.m[2][1]; m33 = m_rt.m[2][2];
        break; 
    case MatrixSel::LLM:
        m11 = m_llm.m[0][0]; m12 = m_llm.m[0][1]; m13 = m_llm.m[0][2];
        m21 = m_llm.m[1][0]; m22 = m_llm.m[1][1]; m23 = m_llm.m[1][2];
        m31 = m_llm.m[2][0]; m32 = m_llm.m[2][1]; m33 = m_llm.m[2][2];
        break;
    case MatrixSel::LCM:
        m11 = m_lcm.m[0][0]; m12 = m_lcm.m[0][1]; m13 = m_lcm.m[0][2];
        m21 = m_lcm.m[1][0]; m22 = m_lcm.m[1][1]; m23 = m_lcm.m[1][2];
        m31 = m_lcm.m[2][0]; m32 = m_lcm.m[2][1]; m33 = m_lcm.m[2][2];
        break;
    default: Error("invalid multiplication matrix specifier");
    }

    SetIr<1>(SetMac<1>((tx << 12) + vx * m11 + vy * m12 + vz * m13));
    SetIr<2>(SetMac<2>((ty << 12) + vx * m21 + vy * m22 + vz * m23));
    SetIr<3>(SetMac<3>((tz << 12) + vx * m31 + vy * m32 + vz * m33));
}

template <std::size_t V>
void Gte::Ncd()
{
    /* TODO */
    m_rgb[0].raw = m_rgb[1].raw;
    m_rgb[1].raw = m_rgb[2].raw;
    m_rgb[2].raw = m_colour.raw;
}

void Gte::Avsz3()
{
    const s64 zsf3 = m_zsf3;
    const s64 sz1 = m_sz[1];
    const s64 sz2 = m_sz[2];
    const s64 sz3 = m_sz[3];

    SetMac<0>((zsf3 * (sz1 + sz2 + sz3)) >> 12);

    m_flags.d = m_mac0 > 0xffff || m_mac0 < 0;
    m_otz = std::clamp(m_mac0, 0, 0xffff);
}

template <std::size_t V, bool Depth>
void Gte::Rtp()
{
    const s64 tx = m_tr.x;
    const s64 ty = m_tr.y;
    const s64 tz = m_tr.z;

    const s64 vx = m_v[V].x;
    const s64 vy = m_v[V].y;
    const s64 vz = m_v[V].z;

    const s64 rt11 = m_rt.m[0][0];
    const s64 rt12 = m_rt.m[0][1];
    const s64 rt13 = m_rt.m[0][2];

    const s64 rt21 = m_rt.m[1][0];
    const s64 rt22 = m_rt.m[1][1];
    const s64 rt23 = m_rt.m[1][2];

    const s64 rt31 = m_rt.m[2][0];
    const s64 rt32 = m_rt.m[2][1];
    const s64 rt33 = m_rt.m[2][2];

    SetIr<1>(SetMac<1>((tx << 12) + vx * rt11 + vy * rt12 + vz * rt13));
    SetIr<2>(SetMac<2>((ty << 12) + vx * rt21 + vy * rt22 + vz * rt23));
    SetIr<3>(SetMac<3>((tz << 12) + vx * rt31 + vy * rt32 + vz * rt33));

    PushSz(m_mac.z);

    const s64 division = RtpUnrDivide(m_h, m_sz[3]);

    PushSx(SetMac<0>(division * m_ir.x + m_ofx) >> 16);
    PushSy(SetMac<0>(division * m_ir.y + m_ofy) >> 16);

    if constexpr (Depth) {
        SetIr<0>(SetMac<0>(division * m_dqa + m_dqb) >> 12);
    }
}

u32 Gte::RtpUnrDivide(u32 h, u32 sz3)
{
    if (2 * sz3 <= h) {
        m_flags.e = true;
        return 0x1ffff;
    }

    const std::size_t z = __builtin_clz(sz3 & 0xffff) - 16;

    h <<= z;
    sz3 <<= z;

    const u16 u = RtpUnrTable[(sz3 - 0x7fc0) >> 7] + 0x101;
    
    sz3 = (0x2000080 - sz3 * u) >> 8;
    sz3 = (0x80 + sz3 * u) >> 8;

    return std::min(0x1ffffu, (0x8000 + h * sz3) >> 16);
}

template <std::size_t Mac>
s32 Gte::SetMac(s64 value)
{
    if constexpr (Mac == 0) {
        m_flags.fp = value > 0x7fffffff;
        m_flags.fn = value < -0x80000000;
        m_mac0 = value;
        return m_mac0;
    }

    if constexpr (Mac == 1) {
        m_flags.ap1 = value > 0x7ffffffffff;
        m_flags.an1 = value < -0x80000000000;
        m_mac.x = value >> m_sf;
        return m_mac.x;
    }

    if constexpr (Mac == 2) {
        m_flags.ap2 = value > 0x7ffffffffff;
        m_flags.an2 = value < -0x80000000000;
        m_mac.y = value >> m_sf;
        return m_mac.y;
    }

    if constexpr (Mac == 3) {
        m_flags.ap3 = value > 0x7ffffffffff;
        m_flags.an3 = value < -0x80000000000;
        m_mac.z = value >> m_sf;
        return m_mac.z;
    }

    Error("invalid mac specified");
}

template <std::size_t Ir>
void Gte::SetIr(s32 value)
{
    if constexpr (Ir == 0) {
        m_flags.h = value > 0xfff || value < 0;
        m_ir0 = std::clamp(value, 0, 0xfff);
        return;
    }

    if constexpr (Ir == 1) {
        m_flags.b1 = value > 0x7fff || value < m_lm;
        m_ir.x = std::clamp(value, m_lm, 0x7fff);
        return;
    }

    if constexpr (Ir == 2) {
        m_flags.b2 = value > 0x7fff || value < m_lm;
        m_ir.y = std::clamp(value, m_lm, 0x7fff);
        return;
    }

    if constexpr (Ir == 3) {
        m_flags.b3 = value > 0x7fff || value < m_lm;
        m_ir.z = std::clamp(value, m_lm, 0x7fff);
        return;
    }

    Error("invalid ir specified");
}

void Gte::PushSx(s32 value)
{
    m_flags.g1 = value > 0x3ff || value < -0x400;

    m_sx[0] = m_sx[1];
    m_sx[1] = m_sx[2];
    m_sx[2] = std::clamp(value, -0x400, 0x3ff);
}

void Gte::PushSy(s32 value)
{
    m_flags.g2 = value > 0x3ff || value < -0x400;

    m_sy[0] = m_sy[1];
    m_sy[1] = m_sy[2];
    m_sy[2] = std::clamp(value, -0x400, 0x3ff);
}

void Gte::PushSz(s32 value)
{
    m_flags.d = value > 0xffff || value < 0;

    m_sz[0] = m_sz[1];
    m_sz[1] = m_sz[2];
    m_sz[2] = m_sz[3];
    m_sz[3] = std::clamp(value, 0, 0xffff);
}

void Gte::PushRgb(u8 r, u8 g, u8 b, u8 c)
{
    m_rgb[0] = m_rgb[1];
    m_rgb[1] = m_rgb[2];

    m_rgb[2].r = r;
    m_rgb[2].g = g;
    m_rgb[2].b = b;
    m_rgb[2].c = c;
}

}
