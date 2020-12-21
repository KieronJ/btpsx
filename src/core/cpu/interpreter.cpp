#include <cstddef>

#include <common/bit.hpp>
#include <common/bitrange.hpp>
#include <common/types.hpp>

#include <spdlog/spdlog.h>

#include "../error.hpp"
#include "core.hpp"

namespace Cpu
{

void Core::OpSll(u32 i)
{
    WriteRegister(Rd(i), ReadRegister(Rt(i)) << Sa(i));
}

void Core::OpSrl(u32 i)
{
    WriteRegister(Rd(i), ReadRegister(Rt(i)) >> Sa(i));
}

void Core::OpSra(u32 i)
{
    const s32 value = ReadRegister(Rt(i));

    WriteRegister(Rd(i), value >> Sa(i));
}

void Core::OpSllv(u32 i)
{
    WriteRegister(Rd(i), ReadRegister(Rt(i)) << (ReadRegister(Rs(i)) & 0x1f));
}

void Core::OpSrlv(u32 i)
{
    WriteRegister(Rd(i), ReadRegister(Rt(i)) >> (ReadRegister(Rs(i)) & 0x1f));
}

void Core::OpSrav(u32 i)
{
    const s32 value = ReadRegister(Rt(i));

    WriteRegister(Rd(i), value >> (ReadRegister(Rs(i)) & 0x1f));
}

void Core::OpJr(u32 i)
{
    Branch(ReadRegister(Rs(i)));
}

void Core::OpJalr(u32 i)
{
    const u32 target = ReadRegister(Rs(i));

    WriteRegister(Rd(i), m_next_pc);
    Branch(target);
}

void Core::OpSyscall(u32 i)
{
    (void)i;

    EnterException(Exception::Syscall);
}

void Core::OpBreak(u32 i)
{
    (void)i;

    EnterException(Exception::Breakpoint);
}

void Core::OpMfhi(u32 i)
{
    WriteRegister(Rd(i), m_hi);
}

void Core::OpMthi(u32 i)
{
    m_hi = ReadRegister(Rs(i));
}

void Core::OpMflo(u32 i)
{
    WriteRegister(Rd(i), m_lo);
}

void Core::OpMtlo(u32 i)
{
    m_lo = ReadRegister(Rs(i));
}

void Core::OpMult(u32 i)
{
    const s32 a = ReadRegister(Rs(i));
    const s32 b = ReadRegister(Rt(i));

    const s64 result = static_cast<s64>(a) * static_cast<s64>(b);

    m_hi = result >> 32;
    m_lo = result;
}

void Core::OpMultu(u32 i)
{
    const u64 a = ReadRegister(Rs(i));
    const u64 b = ReadRegister(Rt(i));

    const u64 result = a * b;

    m_hi = result >> 32;
    m_lo = result;
}

void Core::OpDiv(u32 i)
{
    const s32 n = ReadRegister(Rs(i));
    const s32 d = ReadRegister(Rt(i));

    if (d == 0) {
        m_hi = n;
        m_lo = n >= 0 ? -1 : 1;
    } else if (n == INT32_MIN && d == -1) {
        m_hi = 0;
        m_lo = INT32_MIN;
    } else {
        m_hi = n % d;
        m_lo = n / d;
    }
}

void Core::OpDivu(u32 i)
{
    const u32 n = ReadRegister(Rs(i));
    const u32 d = ReadRegister(Rt(i));

    if (d == 0) {
        m_hi = n;
        m_lo = -1;
    } else {
        m_hi = n % d;
        m_lo = n / d;
    }
}

void Core::OpAdd(u32 i)
{
    const u32 rs = ReadRegister(Rs(i));
    const u32 rt = ReadRegister(Rt(i));
    const u32 result = rs + rt;

    if (Bit::Check<31>(~(rs ^ rt) & (rs ^ result))) {
        EnterException(Exception::Overflow);
        return;
    }

    WriteRegister(Rd(i), result);
}

void Core::OpAddu(u32 i)
{
    WriteRegister(Rd(i), ReadRegister(Rs(i)) + ReadRegister(Rt(i)));
}

void Core::OpSub(u32 i)
{
    const u32 rs = ReadRegister(Rs(i));
    const u32 rt = ReadRegister(Rt(i));
    const u32 result = rs - rt;

    if (Bit::Check<31>((rs ^ rt) & (rs ^ result))) {
        EnterException(Exception::Overflow);
        return;
    }

    WriteRegister(Rd(i), result);
}

void Core::OpSubu(u32 i)
{
    WriteRegister(Rd(i), ReadRegister(Rs(i)) - ReadRegister(Rt(i)));
}

void Core::OpAnd(u32 i)
{
    WriteRegister(Rd(i), ReadRegister(Rs(i)) & ReadRegister(Rt(i)));
}

void Core::OpOr(u32 i)
{
    WriteRegister(Rd(i), ReadRegister(Rs(i)) | ReadRegister(Rt(i)));
}

void Core::OpXor(u32 i)
{
    WriteRegister(Rd(i), ReadRegister(Rs(i)) ^ ReadRegister(Rt(i)));
}

void Core::OpNor(u32 i)
{
    WriteRegister(Rd(i), ~(ReadRegister(Rs(i)) | ReadRegister(Rt(i))));
}

void Core::OpSlt(u32 i)
{
    const s32 rs = ReadRegister(Rs(i));
    const s32 rt = ReadRegister(Rt(i));

    WriteRegister(Rd(i), rs < rt);
}

void Core::OpSltu(u32 i)
{
    WriteRegister(Rd(i), ReadRegister(Rs(i)) < ReadRegister(Rt(i)));
}

void Core::OpBcond(u32 i)
{
    const s32 rs = ReadRegister(Rs(i));
    const u32 offset = Immse(i);

    const bool branch = Bit::Check<16>(i) ^ (rs < 0);
    const bool link = BitRange<20, 17>(i) == 0x8;

    if (link) {
        WriteRegister(31, m_next_pc);
    }

    if (branch) {
        Branch(m_pc + (offset << 2));
    }
}

void Core::OpJ(u32 i)
{
    const u32 target = Target(i);

    Branch((m_pc & 0xf0000000) | (target << 2));
}

void Core::OpJal(u32 i)
{
    const u32 target = Target(i);

    WriteRegister(31, m_next_pc);
    Branch((m_pc & 0xf0000000) | (target << 2));
}

void Core::OpBeq(u32 i)
{
    const u32 offset = Immse(i);

    if (ReadRegister(Rs(i)) == ReadRegister(Rt(i))) {
        Branch(m_pc + (offset << 2));
    }
}

void Core::OpBne(u32 i)
{
    const u32 offset = Immse(i);

    if (ReadRegister(Rs(i)) != ReadRegister(Rt(i))) {
        Branch(m_pc + (offset << 2));
    }
}

void Core::OpBlez(u32 i)
{
    const s32 rs = ReadRegister(Rs(i));
    const u32 offset = Immse(i);

    if (rs <= 0) {
        Branch(m_pc + (offset << 2));
    }
}

void Core::OpBgtz(u32 i)
{
    const s32 rs = ReadRegister(Rs(i));
    const u32 offset = Immse(i);

    if (rs > 0) {
        Branch(m_pc + (offset << 2));
    }
}

void Core::OpAddi(u32 i)
{
    const u32 rs = ReadRegister(Rs(i));
    const u32 imm = Immse(i);
    const u32 result = rs + imm;

    if (Bit::Check<31>(~(rs ^ imm) & (rs ^ result))) {
        EnterException(Exception::Overflow);
        return;
    }

    WriteRegister(Rt(i), result);
}

void Core::OpAddiu(u32 i)
{
    WriteRegister(Rt(i), ReadRegister(Rs(i)) + Immse(i));
}

void Core::OpSlti(u32 i)
{
    const s32 rs = ReadRegister(Rs(i));
    const s32 imm = Immse(i);

    WriteRegister(Rt(i), rs < imm);
}

void Core::OpSltiu(u32 i)
{
    const u32 imm = Immse(i);

    WriteRegister(Rt(i), ReadRegister(Rs(i)) < imm);
}

void Core::OpAndi(u32 i)
{
    WriteRegister(Rt(i), ReadRegister(Rs(i)) & Imm(i));
}

void Core::OpOri(u32 i)
{
    WriteRegister(Rt(i), ReadRegister(Rs(i)) | Imm(i));
}

void Core::OpXori(u32 i)
{
    WriteRegister(Rt(i), ReadRegister(Rs(i)) ^ Imm(i));
}

void Core::OpLui(u32 i)
{
    WriteRegister(Rt(i), Imm(i) << 16);
}

void Core::OpLb(u32 i)
{
    const u32 addr = ReadRegister(Rs(i)) + Immse(i);
    const s8 data = ReadByte(addr);

    WriteRegister(Rt(i), data);
}

void Core::OpLh(u32 i)
{
    const u32 addr = ReadRegister(Rs(i)) + Immse(i);

    if ((addr & 0x1) != 0) {
        EnterException(Exception::AddressLoad);
        return;
    }

    const s16 data = ReadHalf(addr);

    WriteRegister(Rt(i), data);
}

/*void Core::OpLwl(u32 i)
{
    static const u32 mask[] = { 0xffffff, 0xffff, 0xff, 0 };
    static const std::size_t shift[] = { 24, 16, 8, 0 };

    const u32 rt = ReadRegister(Rt(i));

    const u32 addr = ReadRegister(Rs(i)) + Immse(i);
    const u32 data = ReadWord(addr & ~0x3);

    const u32 v = (rt & mask[addr & 0x3]) | (data << shift[addr & 0x3]);

    WriteRegister(Rt(i), v);
}*/

void Core::OpLwl(u32 i)
{
    static const u32 mask[] = { 0xffffff, 0xffff, 0xff, 0 };
    static const std::size_t shift[] = { 24, 16, 8, 0 };

    const u32 rt = ReadRegister(Rt(i));

    const u32 addr = ReadRegister(Rs(i)) + Immse(i);
    const u32 data = ReadWord(addr & ~0x3);

    const u32 v = (rt & (0xffffff >> (8 * (addr & 0x3)))) | (data << (8 * (3 - (addr & 0x3))));

    WriteRegister(Rt(i), v);
}

void Core::OpLw(u32 i)
{
    

    const u32 addr = ReadRegister(Rs(i)) + Immse(i);

    if ((addr & 0x3) != 0) {
        EnterException(Exception::AddressLoad);
        return;
    }

    WriteRegister(Rt(i), ReadWord(addr));
}

void Core::OpLbu(u32 i)
{
    const u32 addr = ReadRegister(Rs(i)) + Immse(i);

    WriteRegister(Rt(i), ReadByte(addr));
}

void Core::OpLhu(u32 i)
{
    const u32 addr = ReadRegister(Rs(i)) + Immse(i);

    if ((addr & 0x1) != 0) {
        EnterException(Exception::AddressLoad);
        return;
    }

    WriteRegister(Rt(i), ReadHalf(addr));
}

void Core::OpLwr(u32 i)
{
    static const u32 mask[] = { 0, 0xff000000, 0xffff0000, 0xffffff00 };
    static const std::size_t shift[] = { 0, 8, 16, 24 };

    const u32 rt = ReadRegister(Rt(i));

    const u32 addr = ReadRegister(Rs(i)) + Immse(i);
    const u32 data = ReadWord(addr & ~0x3);

    const u32 v = (rt & mask[addr & 0x3]) | (data >> shift[addr & 0x3]);

    WriteRegister(Rt(i), v);
}

void Core::OpSb(u32 i)
{
    const u32 addr = ReadRegister(Rs(i)) + Immse(i);

    WriteByte(addr, ReadRegister(Rt(i)));
}

void Core::OpSh(u32 i)
{
    const u32 addr = ReadRegister(Rs(i)) + Immse(i);

    if ((addr & 0x1) != 0) {
        EnterException(Exception::AddressStore);
        return;
    }

    WriteHalf(addr, ReadRegister(Rt(i)));
}

void Core::OpSwl(u32 i)
{
    static const u32 mask[] = { 0xffffff00, 0xffff0000, 0xff000000, 0 };
    static const std::size_t shift[] = { 24, 16, 8, 0 };

    const u32 rt = ReadRegister(Rt(i));

    const u32 addr = ReadRegister(Rs(i)) + Immse(i);
    const u32 data = ReadWord(addr & ~0x3);

    const u32 v = (data & mask[addr & 0x3]) | (rt >> shift[addr & 0x3]);

    WriteWord(addr & ~0x3, v);
}

void Core::OpSw(u32 i)
{
    const u32 addr = ReadRegister(Rs(i)) + Immse(i);

    if ((addr & 0x3) != 0) {
        EnterException(Exception::AddressStore);
        return;
    }

    WriteWord(addr, ReadRegister(Rt(i)));
}

void Core::OpSwr(u32 i)
{
    static const u32 mask[] = { 0, 0xff, 0xffff, 0xffffff };
    static const std::size_t shift[] = { 0, 8, 16, 24 };

    const u32 rt = ReadRegister(Rt(i));

    const u32 addr = ReadRegister(Rs(i)) + Immse(i);
    const u32 data = ReadWord(addr & ~0x3);

    const u32 v = (data & mask[addr & 0x3]) | (rt << shift[addr & 0x3]);

    WriteWord(addr & ~0x3, v);
}

void Core::OpMfc0(u32 i)
{
    u32 value = 0;

    switch (Rd(i)) {
    case 12:
        value = m_status.raw;
        break;
    case 13:
        value = m_cause.raw;
        break;
    case 14:
        value = m_epc;
        break;
    case 15:
        value = 2;
        break;
    default: spdlog::warn("mfc0 from unknown register cop0r{}", Rd(i));
    }

    WriteRegister(Rt(i), value);
}

void Core::OpMtc0(u32 i)
{
    const u32 value = ReadRegister(Rt(i));

    switch (Rd(i)) {
    case 3:
    case 5:
    case 6:
    case 7:
    case 9:
    case 11:
        break;
    case 12:
        m_status.raw = value & 0xf055ff3f;
        break;
    case 13:
        m_cause.raw &= 0xfffffcff;
        m_cause.raw |= value & 0x00000300;
        break;
    case 14:
        m_epc = value;
        break;
    default: Error("mtc0 to unknown register cop0r{}", Rd(i));
    }
}

void Core::OpRfe(u32 i)
{
    (void)i;

    m_status.iec = m_status.iep;
    m_status.iep = m_status.ieo;

    m_status.kuc = m_status.kup;
    m_status.kup = m_status.kuo;
}

void Core::OpMfc2(u32 i)
{
    WriteRegister(Rt(i), m_gte.ReadData(Rd(i)));
}

void Core::OpCfc2(u32 i)
{
    WriteRegister(Rt(i), m_gte.ReadControl(Rd(i)));
}

void Core::OpMtc2(u32 i)
{
    m_gte.WriteData(Rd(i), ReadRegister(Rt(i)));
}

void Core::OpCtc2(u32 i)
{
    m_gte.WriteControl(Rd(i), ReadRegister(Rt(i)));
}

void Core::OpLwc2(u32 i)
{
    const u32 addr = ReadRegister(Rs(i)) + Immse(i);

    if ((addr & 0x3) != 0) {
        EnterException(Exception::AddressLoad);
        return;
    }

    m_gte.WriteData(Rt(i), ReadWord(addr));
}

void Core::OpSwc2(u32 i)
{
    const u32 addr = ReadRegister(Rs(i)) + Immse(i);

    if ((addr & 0x3) != 0) {
        EnterException(Exception::AddressStore);
        return;
    }

    WriteWord(addr, m_gte.ReadData(Rt(i)));
}

void Core::OpCop2cmd(u32 i)
{
    m_gte.Execute(i);
}

void Core::OpNop(u32 i)
{
    (void)i;
}

void Core::OpUnknown(u32 i)
{
    Error("unknown opcode 0x{:08x} at 0x{:08x}", i, m_current_pc);
}

}
