#include <common/types.hpp>

#include "decode.hpp"

namespace Cpu
{

OpClass Decode(u32 i)
{
    if (i == 0) return OpClass::Nop;

    switch (Op(i)) {
    case 0x00: switch(Fn(i)) {
        case 0x00: return OpClass::Sll;
        case 0x02: return OpClass::Srl;
        case 0x03: return OpClass::Sra;
        case 0x04: return OpClass::Sllv;
        case 0x06: return OpClass::Srlv;
        case 0x07: return OpClass::Srav;
        case 0x08: return OpClass::Jr;
        case 0x09: return OpClass::Jalr;
        case 0x0c: return OpClass::Syscall;
        case 0x0d: return OpClass::Break;
        case 0x10: return OpClass::Mfhi;
        case 0x11: return OpClass::Mthi;
        case 0x12: return OpClass::Mflo;
        case 0x13: return OpClass::Mtlo;
        case 0x18: return OpClass::Mult;
        case 0x19: return OpClass::Multu;
        case 0x1a: return OpClass::Div;
        case 0x1b: return OpClass::Divu;
        case 0x20: return OpClass::Add;
        case 0x21: return OpClass::Addu;
        case 0x22: return OpClass::Sub;
        case 0x23: return OpClass::Subu;
        case 0x24: return OpClass::And;
        case 0x25: return OpClass::Or;
        case 0x26: return OpClass::Xor;
        case 0x27: return OpClass::Nor;
        case 0x2a: return OpClass::Slt;
        case 0x2b: return OpClass::Sltu;
        default: return OpClass::Illegal; 
    }
    case 0x01: return OpClass::Bcond;
    case 0x02: return OpClass::J;
    case 0x03: return OpClass::Jal;
    case 0x04: return OpClass::Beq;
    case 0x05: return OpClass::Bne;
    case 0x06: return OpClass::Blez;
    case 0x07: return OpClass::Bgtz;
    case 0x08: return OpClass::Addi;
    case 0x09: return OpClass::Addiu;
    case 0x0a: return OpClass::Slti;
    case 0x0b: return OpClass::Sltiu;
    case 0x0c: return OpClass::Andi;
    case 0x0d: return OpClass::Ori;
    case 0x0e: return OpClass::Xori;
    case 0x0f: return OpClass::Lui;
    case 0x10: switch(Rs(i)) {
        case 0x00: return OpClass::Mfc0;
        case 0x04: return OpClass::Mtc0;
        case 0x10 ... 0x1f: switch(Fn(i)) {
            case 0x10: return OpClass::Rfe;
            default: return OpClass::Illegal;
        }
        default: return OpClass::Illegal;
    }
    case 0x12: switch(Rs(i)) {
        case 0x00: return OpClass::Mfc2;
        case 0x02: return OpClass::Cfc2;
        case 0x04: return OpClass::Mtc2;
        case 0x06: return OpClass::Ctc2;
        case 0x10 ... 0x1f: return OpClass::Cop2cmd;
        default: return OpClass::Illegal;
    }
    case 0x20: return OpClass::Lb;
    case 0x21: return OpClass::Lh;
    case 0x22: return OpClass::Lwl;
    case 0x23: return OpClass::Lw;
    case 0x24: return OpClass::Lbu;
    case 0x25: return OpClass::Lhu;
    case 0x26: return OpClass::Lwr;
    case 0x28: return OpClass::Sb;
    case 0x29: return OpClass::Sh;
    case 0x2a: return OpClass::Swl;
    case 0x2b: return OpClass::Sw;
    case 0x2e: return OpClass::Swr;
    case 0x32: return OpClass::Lwc2;
    case 0x3a: return OpClass::Swc2;
    default: return OpClass::Illegal;
    }
}

}
