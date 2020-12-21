#pragma once

#include <array>

#include <common/bitrange.hpp>
#include <common/types.hpp>

namespace Cpu
{

enum class OpClass : u8 {
    Nop,
    Sll,
    Srl,
    Sra,
    Sllv,
    Srlv,
    Srav,
    Jr,
    Jalr,
    Syscall,
    Break,
    Mfhi,
    Mthi,
    Mflo,
    Mtlo,
    Mult,
    Multu,
    Div,
    Divu,
    Add,
    Addu,
    Sub,
    Subu,
    And,
    Or,
    Xor,
    Nor,
    Slt,
    Sltu,
    Bcond,
    J,
    Jal,
    Beq,
    Bne,
    Blez,
    Bgtz,
    Addi,
    Addiu,
    Slti,
    Sltiu,
    Andi,
    Ori,
    Xori,
    Lui,
    Mfc0,
    Mtc0,
    Rfe,
    Mfc2,
    Cfc2,
    Mtc2,
    Ctc2,
    Cop2cmd,
    Lb,
    Lh,
    Lwl,
    Lw,
    Lbu,
    Lhu,
    Lwr,
    Sb,
    Sh,
    Swl,
    Sw,
    Swr,
    Lwc2,
    Swc2,
    Illegal,
    Count
};

enum class OpFlags : u8 { None, Branch, Delay };

struct Op {
    const char *name;
    OpFlags flags;
};

static const std::array<Op, static_cast<int>(OpClass::Count)> OpTable {{
    { "nop",     OpFlags::None },
    { "sll",     OpFlags::None },
    { "srl",     OpFlags::None },
    { "sra",     OpFlags::None },
    { "sllv",    OpFlags::None },
    { "srlv",    OpFlags::None },
    { "srav",    OpFlags::None },
    { "jr",      OpFlags::Delay },
    { "jalr",    OpFlags::Delay },
    { "syscall", OpFlags::Branch },
    { "break",   OpFlags::Branch },
    { "mfhi",    OpFlags::None },
    { "mthi",    OpFlags::None },
    { "mflo",    OpFlags::None },
    { "mtlo",    OpFlags::None },
    { "mult",    OpFlags::None },
    { "multu",   OpFlags::None },
    { "div",     OpFlags::None },
    { "divu",    OpFlags::None },
    { "add",     OpFlags::None },
    { "addu",    OpFlags::None },
    { "sub",     OpFlags::None },
    { "subu",    OpFlags::None },
    { "and",     OpFlags::None },
    { "or",      OpFlags::None },
    { "xor",     OpFlags::None },
    { "nor",     OpFlags::None },
    { "slt",     OpFlags::None },
    { "sltu",    OpFlags::None },
    { "bcond",   OpFlags::Delay },
    { "j",       OpFlags::Delay },
    { "jal",     OpFlags::Delay },
    { "beq",     OpFlags::Delay },
    { "bne",     OpFlags::Delay },
    { "blez",    OpFlags::Delay },
    { "bgtz",    OpFlags::Delay },
    { "addi",    OpFlags::None },
    { "addiu",   OpFlags::None },
    { "slti",    OpFlags::None },
    { "sltiu",   OpFlags::None },
    { "andi",    OpFlags::None },
    { "ori",     OpFlags::None },
    { "xori",    OpFlags::None },
    { "lui",     OpFlags::None },
    { "mfc0",    OpFlags::None },
    { "mtc0",    OpFlags::None },
    { "rfe",     OpFlags::Branch },
    { "mfc2",    OpFlags::None },
    { "cfc2",    OpFlags::None },
    { "mtc2",    OpFlags::None },
    { "ctc2",    OpFlags::None },
    { "cop2cmd", OpFlags::None },
    { "lb",      OpFlags::None },
    { "lh",      OpFlags::None },
    { "lwl",     OpFlags::None },
    { "lw",      OpFlags::None },
    { "lbu",     OpFlags::None },
    { "lhu",     OpFlags::None },
    { "lwr",     OpFlags::None },
    { "sb",      OpFlags::None },
    { "sh",      OpFlags::None },
    { "swl",     OpFlags::None },
    { "sw",      OpFlags::None },
    { "swr",     OpFlags::None },
    { "lwc2",    OpFlags::None },
    { "swc2",    OpFlags::None },
    { "illegal", OpFlags::Branch }
}};

OpClass Decode(u32 i);

inline size_t Op(u32 i) { return BitRange<31, 26>(i); }
inline size_t Rs(u32 i) { return BitRange<25, 21>(i); }
inline size_t Rt(u32 i) { return BitRange<20, 16>(i); }
inline size_t Rd(u32 i) { return BitRange<15, 11>(i); }
inline size_t Sa(u32 i) { return BitRange<10, 6>(i); }
inline size_t Fn(u32 i) { return BitRange<5, 0>(i); }
inline u16 Imm(u32 i) { return BitRange<15, 0>(i); }
inline s16 Immse(u32 i) { return BitRange<15, 0>(i); }
inline u32 Target(u32 i) { return BitRange<25, 0>(i); }

}
