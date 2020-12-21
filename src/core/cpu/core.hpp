#pragma once

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <common/bitfield.hpp>
#include <common/types.hpp>

#include "decode.hpp"
#include "gte.hpp"
#include "recompiler.hpp"

namespace Cpu
{

class Bus {
public:
    virtual void Tick(s64 ticks) = 0;
    virtual void BurstFill(void *dst, u32 addr, std::size_t size) = 0;

    virtual u32 ReadCode(u32 addr) = 0;

    virtual u8 ReadByte(u32 addr) = 0;
    virtual u16 ReadHalf(u32 addr) = 0;
    virtual u32 ReadWord(u32 addr) = 0;

    virtual void WriteByte(u32 addr, u8 data) = 0;
    virtual void WriteHalf(u32 addr, u16 data) = 0;
    virtual void WriteWord(u32 addr, u32 data) = 0;
};

class Core {
public:
    Core(Bus *bus);

    void Reset();

    int Run();
    int RunRecompiler();

    inline void AssertInterrupt(bool state)
    {
        m_cause.ip2 = state;
    }

    inline u32 ReadRegister(std::size_t index) const
    {
        return m_gpr[index];
    }

    inline void WriteRegister(std::size_t index, u32 value)
    {
        m_gpr[index] = value;
        m_gpr[0] = 0;
    }

    inline void WritePc(u32 value)
    {
        m_pc = value;
        m_next_pc = value + 4;
    }

    inline u32 * Gpr() { return m_gpr.data(); }

private:
    u32 Fetch();

    void OpSll(u32 i);
    void OpSrl(u32 i);
    void OpSra(u32 i);
    void OpSllv(u32 i);
    void OpSrlv(u32 i);
    void OpSrav(u32 i);
    void OpJr(u32 i);
    void OpJalr(u32 i);
    void OpSyscall(u32 i);
    void OpBreak(u32 i);
    void OpMfhi(u32 i);
    void OpMthi(u32 i);
    void OpMflo(u32 i);
    void OpMtlo(u32 i);
    void OpMult(u32 i);
    void OpMultu(u32 i);
    void OpDiv(u32 i);
    void OpDivu(u32 i);
    void OpAdd(u32 i);
    void OpAddu(u32 i);
    void OpSub(u32 i);
    void OpSubu(u32 i);
    void OpAnd(u32 i);
    void OpOr(u32 i);
    void OpXor(u32 i);
    void OpNor(u32 i);
    void OpSlt(u32 i);
    void OpSltu(u32 i);

    void OpBcond(u32 i);
    void OpJ(u32 i);
    void OpJal(u32 i);
    void OpBeq(u32 i);
    void OpBne(u32 i);
    void OpBlez(u32 i);
    void OpBgtz(u32 i);
    void OpAddi(u32 i);
    void OpAddiu(u32 i);
    void OpSlti(u32 i);
    void OpSltiu(u32 i);
    void OpAndi(u32 i);
    void OpOri(u32 i);
    void OpXori(u32 i);
    void OpLui(u32 i);
    void OpLb(u32 i);
    void OpLh(u32 i);
    void OpLwl(u32 i);
    void OpLw(u32 i);
    void OpLbu(u32 i);
    void OpLhu(u32 i);
    void OpLwr(u32 i);
    void OpSb(u32 i);
    void OpSh(u32 i);
    void OpSwl(u32 i);
    void OpSw(u32 i);
    void OpSwr(u32 i);

    void OpMfc0(u32 i);
    void OpMtc0(u32 i);
    void OpRfe(u32 i);

    void OpMfc2(u32 i);
    void OpCfc2(u32 i);
    void OpMtc2(u32 i);
    void OpCtc2(u32 i);
    void OpLwc2(u32 i);
    void OpSwc2(u32 i);
    void OpCop2cmd(u32 i);

    void OpNop(u32 i);
    void OpUnknown(u32 i);

    std::string Disassemble(u32 i, u32 addr) const;

public:
    static u32 TranslateAddress(u32 addr);

private:
    static constexpr std::size_t CacheEntries = 256;
    static constexpr std::size_t CacheLineSize = 16;

    struct CacheEntry {
        bool valid;
        u32 tag;
        u32 data[CacheLineSize / sizeof(u32)];
    };

    bool CacheHit(u32 addr) const;
    u32 CacheFetch(u32 addr) const;

    void CacheFill(u32 addr);
    void CacheInvalidate(u32 addr);

public:

    u8 ReadByte(u32 addr);
    u16 ReadHalf(u32 addr);
    u32 ReadWord(u32 addr);

    void WriteByte(u32 addr, u8 data);
    void WriteHalf(u32 addr, u16 data);
    void WriteWord(u32 addr, u32 data);

private:
    inline void Branch(u32 address)
    {
        m_next_pc = address;
        m_branch = true;
    }

    enum class Exception : u8 {
        Interrupt,
        AddressLoad = 4,
        AddressStore,
        Syscall = 8,
        Breakpoint,
        Overflow = 12
    };

    void EnterException(Exception e);

    enum Registers { Count = 32 };

    std::array<CacheEntry, CacheEntries> m_instruction_cache;

public:
    u32 m_pc, m_current_pc, m_next_pc;
    std::array<u32, Registers::Count> m_gpr;
    u32 m_hi, m_lo;
    bool m_branch, m_branch_delay;

    union {
        u32 raw;

        BitField<u32, bool, 0, 1> iec;
        BitField<u32, bool, 1, 1> kuc;
        BitField<u32, bool, 2, 1> iep;
        BitField<u32, bool, 3, 1> kup;
        BitField<u32, bool, 4, 1> ieo;
        BitField<u32, bool, 5, 1> kuo;
        BitField<u32, u8, 8, 8> im;
        BitField<u32, bool, 16, 1> isc;
        BitField<u32, bool, 18, 1> pz;
        BitField<u32, bool, 20, 1> pe;
        BitField<u32, bool, 22, 1> bev;
        BitField<u32, bool, 25, 1> re;
        BitField<u32, bool, 28, 1> cu0;
        BitField<u32, bool, 29, 1> cu1;
        BitField<u32, bool, 30, 1> cu2;
        BitField<u32, bool, 31, 1> cu3;
    } m_status;

    union {
        u32 raw;
    
        BitField<u32, Exception, 2, 5> exc_code;
        BitField<u32, u8, 8, 8> ip;
        BitField<u32, bool, 10, 1> ip2;
        BitField<u32, u8, 28, 2> ce;
        BitField<u32, bool, 30, 1> bt;
        BitField<u32, bool, 31, 1> bd;
    } m_cause;

    u32 m_epc;

    Gte m_gte;

    bool m_cache_enabled = false;

private:
    friend class Recompiler;

    std::unique_ptr<Recompiler> m_recompiler;
    Bus *m_bus;
};

}
