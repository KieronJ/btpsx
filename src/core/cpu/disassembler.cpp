#include <cstddef>
#include <string>

#include <fmt/format.h>

#include <common/bitrange.hpp>
#include <common/types.hpp>

#include "core.hpp"

namespace Cpu
{

static const char * Registers[] = {
    "$zero", "$at", "$v0", "$v1",
    "$a0",   "$a1", "$a2", "$a3",
    "$t0",   "$t1", "$t2", "$t3",
    "$t4",   "$t5", "$t6", "$t7",
    "$s0",   "$s1", "$s2", "$s3",
    "$s4",   "$s5", "$s6", "$s7",
    "$t8",   "$t9", "$k0", "$k1",
    "$gp",   "$sp", "$fp", "$ra",
};

static std::string Jump(const std::string& name, u32 i, u32 addr)
{
    const u32 target = 4 * BitRange<25, 0>(i);

    return fmt::format("{} 0x{:08x}", name, (addr & 0xf0000000) + target);
}

static std::string Branchn(const std::string& name, u32 i, u32 addr)
{
    const s16 offset = BitRange<15, 0>(i);
    const u32 target = addr + 4 * offset + 4;

    const char * const rs = Registers[BitRange<25, 21>(i)];
    const char * const rt = Registers[BitRange<20, 16>(i)];

    return fmt::format("{} {}, {}, 0x{:08x}", name, rs, rt, target);
}

static std::string Branchz(const std::string& name, u32 i, u32 addr)
{
    const s16 offset = BitRange<15, 0>(i);
    const u32 target = addr + 4 * offset + 4;

    const char * const rs = Registers[BitRange<25, 21>(i)];

    return fmt::format("{} {}, 0x{:08x}", name, rs, target);
}

static std::string Immediate(const std::string& name, u32 i)
{
    const s16 immediate = BitRange<15, 0>(i);

    const char * const rs = Registers[BitRange<25, 21>(i)];
    const char * const rt = Registers[BitRange<20, 16>(i)];

    return fmt::format("{} {}, {}, {:#x}", name, rt, rs, immediate);
}

static std::string ImmediateBitop(const std::string& name, u32 i)
{
    const u16 immediate = BitRange<15, 0>(i);

    const char * const rs = Registers[BitRange<25, 21>(i)];
    const char * const rt = Registers[BitRange<20, 16>(i)];

    return fmt::format("{} {}, {}, {:#x}", name, rt, rs, immediate);
}

static std::string Lui(u32 i)
{
    const u16 immediate = BitRange<15, 0>(i);

    const char * const rt = Registers[BitRange<20, 16>(i)];

    return fmt::format("lui {}, 0x{:04x}", rt, immediate);
}

static std::string LoadStore(const std::string& name, u32 i)
{
    const s16 offset = BitRange<15, 0>(i);

    const char * const rs = Registers[BitRange<25, 21>(i)];
    const char * const rt = Registers[BitRange<20, 16>(i)];

    return fmt::format("{} {}, {:#x}({})", name, rt, offset, rs);
}

static std::string Shift(const std::string& name, u32 i)
{
    const std::size_t sa = BitRange<10, 6>(i);

    const char * const rt = Registers[BitRange<20, 16>(i)];
    const char * const rd = Registers[BitRange<15, 11>(i)];

    return fmt::format("{} {}, {}, {}", name, rd, rt, sa);
}

static std::string Shiftv(const std::string& name, u32 i)
{
    const char * const rs = Registers[BitRange<25, 21>(i)];
    const char * const rt = Registers[BitRange<20, 16>(i)];
    const char * const rd = Registers[BitRange<15, 11>(i)];

    return fmt::format("{} {}, {}, {}", name, rd, rt, rs);
}

static std::string Jr(u32 i)
{
    const char * const rs = Registers[BitRange<25, 21>(i)];

    return fmt::format("jr {}", rs);
}

static std::string Jalr(u32 i)
{
    const char * const rs = Registers[BitRange<25, 21>(i)];
    const char * const rd = Registers[BitRange<15, 11>(i)];

    return fmt::format("jalr {}, {}", rs, rd);
}

static std::string Mf(const std::string& name, u32 i)
{
    const char * const rd = Registers[BitRange<15, 11>(i)];

    return fmt::format("{} {}", name, rd);
}

static std::string Mt(const std::string& name, u32 i)
{
    const char * const rs = Registers[BitRange<25, 21>(i)];

    return fmt::format("{} {}", name, rs);
}

static std::string MultDiv(const std::string& name, u32 i)
{
    const char * const rs = Registers[BitRange<25, 21>(i)];
    const char * const rt = Registers[BitRange<20, 16>(i)];

    return fmt::format("{} {}, {}", name, rs, rt);
}

static std::string Dst(const std::string& name, u32 i)
{
    const char * const rs = Registers[BitRange<25, 21>(i)];
    const char * const rt = Registers[BitRange<20, 16>(i)];
    const char * const rd = Registers[BitRange<15, 11>(i)];

    return fmt::format("{} {}, {}, {}", name, rd, rs, rt);
}

static std::string Special(u32 i)
{
    switch (BitRange<5, 0>(i)) {
    case 0x00: return Shift("sll", i);
    case 0x02: return Shift("srl", i);
    case 0x03: return Shift("sra", i);
    case 0x04: return Shiftv("sllv", i);
    case 0x06: return Shiftv("srlv", i);
    case 0x07: return Shiftv("srav", i);
    case 0x08: return Jr(i);
    case 0x09: return Jalr(i);
    case 0x0c: return "syscall";
    case 0x0d: return "break";
    case 0x10: return Mf("hi", i);
    case 0x11: return Mt("hi", i);
    case 0x12: return Mf("lo", i);
    case 0x13: return Mt("lo", i);
    case 0x18: return MultDiv("mult", i);
    case 0x19: return MultDiv("multu", i);
    case 0x1a: return MultDiv("div", i);
    case 0x1b: return MultDiv("divu", i);
    case 0x20: return Dst("add", i);
    case 0x21: return Dst("addu", i);
    case 0x22: return Dst("sub", i);
    case 0x23: return Dst("subu", i);
    case 0x24: return Dst("and", i);
    case 0x25: return Dst("or", i);
    case 0x26: return Dst("xor", i);
    case 0x27: return Dst("nor", i);
    case 0x2a: return Dst("slt", i);
    case 0x2b: return Dst("sltu", i);
    default: return fmt::format("unknown 0x{:08x}", i);
    }
}

static std::string Bcond(u32 i, u32 addr)
{
    switch(BitRange<20, 16>(i) & 0x11) {
    case 0x00: return Branchz("bltz", i, addr);
    case 0x01: return Branchz("bgez", i, addr);
    case 0x10: return Branchz("bltzal", i, addr);
    case 0x11: return Branchz("bgezal", i, addr);
    default: return fmt::format("unknown 0x{:08x}", i);
    }
}

static std::string Mxc0(const std::string& name, u32 i)
{
    const std::size_t rd = BitRange<15, 11>(i);

    const char * const rt = Registers[BitRange<20, 16>(i)];

    return fmt::format("{} {}, $cop0r{}", name, rt, rd);
}

static std::string Cop0(u32 i)
{
    switch(BitRange<25, 21>(i)) {
    case 0x00: return Mxc0("mfc0", i);
    case 0x04: return Mxc0("mtc0", i);
    case 0x10: return "rfe";
    default: return fmt::format("unknown 0x{:08x}", i);
    }
}

static std::string Mxc2(const std::string& name, u32 i)
{
    const std::size_t rd = BitRange<15, 11>(i);

    const char * const rt = Registers[BitRange<20, 16>(i)];

    return fmt::format("{} {}, $cop2r{}", name, rt, rd);
}

static std::string Cop2(u32 i)
{
    switch(BitRange<25, 21>(i)) {
    case 0x00: return Mxc2("mfc2", i);
    case 0x02: return Mxc2("cfc2", i);
    case 0x04: return Mxc2("mtc2", i);
    case 0x06: return Mxc2("ctc2", i);
    default: return fmt::format("unknown 0x{:08x}", i);
    }
}

std::string Core::Disassemble(u32 i, u32 addr) const
{
    if (i == 0) {
        return "nop";
    }

    switch (BitRange<31, 26>(i)) {
    case 0x00: return Special(i);
    case 0x01: return Bcond(i, addr);
    case 0x02: return Jump("j", i, addr);
    case 0x03: return Jump("jal", i, addr);
    case 0x04: return Branchn("beq", i, addr);
    case 0x05: return Branchn("bne", i, addr);
    case 0x06: return Branchz("blez", i, addr);
    case 0x07: return Branchz("bgtz", i, addr);
    case 0x08: return Immediate("addi", i);
    case 0x09: return ImmediateBitop("addiu", i);
    case 0x0a: return Immediate("slti", i);
    case 0x0b: return ImmediateBitop("sltiu", i);
    case 0x0c: return ImmediateBitop("andi", i);
    case 0x0d: return ImmediateBitop("ori", i);
    case 0x0e: return ImmediateBitop("xori", i);
    case 0x0f: return Lui(i);
    case 0x10: return Cop0(i);
    case 0x12: return Cop2(i);
    case 0x20: return LoadStore("lb", i);
    case 0x21: return LoadStore("lh", i);
    case 0x22: return LoadStore("lwl", i);
    case 0x23: return LoadStore("lw", i);
    case 0x24: return LoadStore("lbu", i);
    case 0x25: return LoadStore("lhu", i);
    case 0x26: return LoadStore("lwr", i);
    case 0x28: return LoadStore("sb", i);
    case 0x29: return LoadStore("sh", i);
    case 0x2a: return LoadStore("swl", i);
    case 0x2b: return LoadStore("sw", i);
    case 0x2e: return LoadStore("swr", i);
    default: return fmt::format("unknown 0x{:08x}", i);
    }
}

}
