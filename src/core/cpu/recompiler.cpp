#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include <common/bit.hpp>
#include <common/types.hpp>

#include <xbyak/xbyak.h>

#include "code_buffer.hpp"
#include "core.hpp"
#include "decode.hpp"
#include "recompiler.hpp"

namespace Cpu
{

using BlockEntryFn = int (*)(Core *);

struct Block {
    BlockEntryFn entry;
    int bytes;
    u32 guest_address;
    int guest_instructions;
    bool valid;
};

static constexpr size_t kRamSize   = 2 * 1024 * 1024;
static constexpr size_t kBiosSize  = 512 * 1024;

static constexpr size_t kPageShift = 12;
static constexpr size_t kPageMask  = (1 << kPageShift) - 1;

static Block gRecompilerBlocks[(kRamSize + kBiosSize) >> 2];
static std::vector<Block *> gRecompilerPages[kRamSize >> kPageShift];

using namespace Xbyak::util;

Recompiler::Recompiler(Bus *bus, Core *cpu, size_t cache_size)
    : m_bus{ bus }, m_cpu{ cpu }, m_cache { CodeBuffer(cache_size) } {}

int Recompiler::Run(u32 address)
{
    size_t block_index = Core::TranslateAddress(address);

    if (block_index > kRamSize) {
        assert((address >= 0x1fc00000) && (address < (0x1fc00000 + kBiosSize)));
        block_index -= 0x1fc00000 - kRamSize;
    }

    Block& block = gRecompilerBlocks[block_index >> 2];

    if (!block.valid) {
        printf("recompiling block at 0x%08x\n", address);
        CompileBlock(block, address);
    }

    const int error = block.entry(m_cpu);

    if (error) {
        std::cerr << "block exited with error code: " <<  error << std::endl;
        std::abort();
    }

    return block.guest_instructions;
}

void Recompiler::ClearCache()
{
    m_cache.Flush();
    std::memset(gRecompilerBlocks, 0, sizeof(gRecompilerBlocks));
    for (size_t i = 0; i < (kRamSize >> kPageShift); ++i) gRecompilerPages[i].clear();
}

void Recompiler::InvalidateAddress(u32 address)
{
    if (address >= kRamSize) {
        printf("invalidating invalid block at 0x%08x\n", address);
        std::abort();
    }

    const size_t page = address >> kPageShift;

    //printf("invalidating block at 0x%08x : page %lu\n", address, page);
    for (Block *block : gRecompilerPages[page]) InvalidateBlock(*block);
    gRecompilerPages[page].clear();
}

void Recompiler::AddBlockRange(Block& block, u32 address, int size)
{
    if (address >= kRamSize) {
        printf("adding block range at invalid address 0x%08x\n", address);
        std::abort();
    }

    const u32 start = address >> kPageShift;
    const u32 end = start + (((size + kPageMask) & ~kPageMask) >> kPageShift);

    for (u32 i = start; i < end; ++i) gRecompilerPages[i].push_back(&block);
}

void Recompiler::InvalidateBlock(Block& block)
{
    block.valid = false;

    const u32 start = block.guest_address >> kPageShift;
    const u32 end = start + (((block.guest_instructions + kPageMask) & ~kPageMask) >> kPageShift);

    for (u32 i = start; i < end; ++i) {
        auto& vec = gRecompilerPages[i];
        auto found = std::find(vec.begin(), vec.end(), &block);
        if (found != vec.end()) vec.erase(found);
    }
}

void Recompiler::CompileBlock(Block& block, u32 address)
{
    Emitter e(m_cache.Remaining(), m_cache.Current());

    CompilePrologue(e);
 
    int instructions = 0;

    u32 i;
    OpClass op;
    OpFlags flags;

    block.guest_address = address;

    do {
        i = m_bus->ReadCode(Core::TranslateAddress(address)); 
        op = Decode(i);
        flags = OpTable[static_cast<int>(op)].flags;

        CompileInstruction(e, op, address, i);
        address += 4;
        instructions++;

        if (flags == OpFlags::Delay) {
            i = m_bus->ReadCode(Core::TranslateAddress(address)); 
            op = Decode(i);
            assert(OpTable[static_cast<int>(op)].flags == OpFlags::NONE);

            CompileInstruction(e, op, address, i);
            instructions++;
        }
    } while (flags == OpFlags::None);

    CompileEpilogue(e);

    m_cache.Commit(e.getSize());

    block.entry = e.getCode<BlockEntryFn>();
    block.bytes = e.getSize();
    block.guest_instructions = instructions;
    block.valid = true;

    const u32 phys = Core::TranslateAddress(block.guest_address);
    if (phys < kRamSize) AddBlockRange(block, phys, instructions * 4);

   // for (int i = 0; i < block.bytes; ++i) {
   //     const u8 data = reinterpret_cast<u8 *>(block.entry)[i];
   //     printf("\\x%02x", data);
   // }

   // printf("\n");
}

void Recompiler::CompilePrologue(Emitter& e)
{
    e.push(rbp);
    e.mov(rbp, rsp);

    e.push(rbx);
    e.push(r12);
    e.push(r13);
    e.push(r14);
    e.push(r15);
    e.sub(rsp, 8);

    /* mov rbx, &cpu */
    e.mov(rbx, rdi);
}

void Recompiler::CompileEpilogue(Emitter& e)
{
    e.xor(eax, eax);

    e.add(rsp, 8);
    e.pop(r15);
    e.pop(r14);
    e.pop(r13);
    e.pop(r12);
    e.pop(rbx);

    e.pop(rbp);
    e.ret();
}

void Recompiler::CompileInstruction(Emitter& e, OpClass op, u32 address, u32 i)
{
    if (op == OpClass::Nop) return;

    switch (op) {
    case OpClass::Sll:
        CompileSll(e, i);
        break;
    case OpClass::Srl:
        CompileSrl(e, i);
        break;
    case OpClass::Sra:
        CompileSra(e, i);
        break;
    case OpClass::Sllv:
        CompileSllv(e, i);
        break;
    case OpClass::Srlv:
        CompileSrlv(e, i);
        break;
    case OpClass::Srav:
        CompileSrav(e, i);
        break;
    case OpClass::Jr:
        CompileJr(e, i);
        break;
    case OpClass::Jalr:
        CompileJalr(e, address, i);
        break;
    case OpClass::Syscall:
        CompileSyscall(e, address, i);
        break;
    case OpClass::Mfhi:
        CompileMfhi(e, i);
        break;
    case OpClass::Mthi:
        CompileMthi(e, i);
        break;
    case OpClass::Mflo:
        CompileMflo(e, i);
        break;
    case OpClass::Mtlo:
        CompileMtlo(e, i);
        break;
    case OpClass::Mult:
        CompileMult(e, i);
        break;
    case OpClass::Multu:
        CompileMultu(e, i);
        break;
    case OpClass::Div:
        CompileDiv(e, i);
        break;
    case OpClass::Divu:
        CompileDivu(e, i);
        break;
    case OpClass::Add:
    case OpClass::Addu:
        CompileAddu(e, i);
        break;
    case OpClass::Subu:
        CompileSubu(e, i);
        break;
    case OpClass::And:
        CompileAnd(e, i);
        break;
    case OpClass::Or:
        CompileOr(e, i);
        break;
    case OpClass::Xor:
        CompileXor(e, i);
        break;
    case OpClass::Nor:
        CompileNor(e, i);
        break;
    case OpClass::Slt:
        CompileSlt(e, i);
        break;
    case OpClass::Sltu:
        CompileSltu(e, i);
        break;
    case OpClass::Bcond:
        CompileBcond(e, address, i);
        break;
    case OpClass::J:
        CompileJ(e, address, i);
        break;
    case OpClass::Jal:
        CompileJal(e, address, i);
        break;
    case OpClass::Beq:
        CompileBeq(e, address, i);
        break;
    case OpClass::Bne:
        CompileBne(e, address, i);
        break;
    case OpClass::Blez:
        CompileBlez(e, address, i);
        break;
    case OpClass::Bgtz:
        CompileBgtz(e, address, i);
        break;
    case OpClass::Addi:
    case OpClass::Addiu:
        CompileAddiu(e, i);
        break;
    case OpClass::Slti:
        CompileSlti(e, i);
        break;
    case OpClass::Sltiu:
        CompileSltiu(e, i);
        break;
    case OpClass::Andi:
        CompileAndi(e, i);
        break;
    case OpClass::Ori:
        CompileOri(e, i);
        break;
    case OpClass::Xori:
        CompileXori(e, i);
        break;
    case OpClass::Lui:
        CompileLui(e, i);
        break;
    case OpClass::Mfc0:
        CompileMfc0(e, i);
        break;
    case OpClass::Mtc0:
        CompileMtc0(e, i);
        break;
    case OpClass::Rfe:
        CompileRfe(e, i);
        break;
    case OpClass::Lb:
        CompileLb(e, i);
        break;
    case OpClass::Lh:
        CompileLh(e, i);
        break;
    case OpClass::Lw:
        CompileLw(e, i);
        break;
    case OpClass::Lbu:
        CompileLbu(e, i);
        break;
    case OpClass::Lhu:
        CompileLhu(e, i);
        break;
    case OpClass::Lwl:
        CompileLwl(e, i);
        break;
    case OpClass::Lwr:
        CompileLwr(e, i);
        break;
    case OpClass::Sb:
        CompileSb(e, i);
        break;
    case OpClass::Sh:
        CompileSh(e, i);
        break;
    case OpClass::Sw:
        CompileSw(e, i);
        break;
    case OpClass::Swl:
        CompileSwl(e, i);
        break;
    case OpClass::Swr:
        CompileSwr(e, i);
        break;
    case OpClass::Mfc2:
    case OpClass::Cfc2:
    case OpClass::Mtc2:
    case OpClass::Ctc2:
    case OpClass::Lwc2:
    case OpClass::Swc2:
    case OpClass::Cop2cmd:
        CompileGte(e, op, i);
        break;
    default:
        CompileIllegal(e, op, i);
    }

    //printf("emitted instruction: %s (%#08x)\n", OpTable[static_cast<int>(op)].name, i);
}

void Recompiler::CompileSll(Emitter& e, u32 i)
{
    if (Rd(i) == 0 || (Rd(i) == Rt(i) && Sa(i) == 0)) return;

    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rd = offsetof(Core, m_gpr) + Rd(i) * sizeof(u32);

    if (Rt(i) == 0) {
        e.xor(eax, eax);
        e.mov(dword [rbx + rd], eax);
    } else if (Sa(i) == 0) {
        e.mov(eax, dword [rbx + rt]);
        e.mov(dword [rbx + rd], eax);
    } else if (Rd(i) == Rt(i)) {
        e.shl(dword [rbx + rd], Sa(i));
    } else {
        e.mov(eax, dword [rbx + rt]);
        e.shl(eax, Sa(i));
        e.mov(dword [rbx + rd], eax);
    }
}

void Recompiler::CompileSrl(Emitter& e, u32 i)
{
    if (Rd(i) == 0 || (Rd(i) == Rt(i) && Sa(i) == 0)) return;

    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rd = offsetof(Core, m_gpr) + Rd(i) * sizeof(u32);

    if (Rt(i) == 0) {
        e.xor(eax, eax);
        e.mov(dword [rbx + rd], eax);
    } else if (Sa(i) == 0) {
        e.mov(eax, dword [rbx + rt]);
        e.mov(dword [rbx + rd], eax);
    } else if (Rd(i) == Rt(i)) {
        e.shr(dword [rbx + rd], Sa(i));
    } else {
        e.mov(eax, dword [rbx + rt]);
        e.shr(eax, Sa(i));
        e.mov(dword [rbx + rd], eax);
    }
}

void Recompiler::CompileSra(Emitter& e, u32 i)
{
    if (Rd(i) == 0 || (Rd(i) == Rt(i) && Sa(i) == 0)) return;

    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rd = offsetof(Core, m_gpr) + Rd(i) * sizeof(u32);

    if (Rt(i) == 0) {
        e.xor(eax, eax);
        e.mov(dword [rbx + rd], eax);
    } else if (Sa(i) == 0) {
        e.mov(eax, dword [rbx + rt]);
        e.mov(dword [rbx + rd], eax);
    } else if (Rd(i) == Rt(i)) {
        e.sar(dword [rbx + rd], Sa(i));
    } else {
        e.mov(eax, dword [rbx + rt]);
        e.sar(eax, Sa(i));
        e.mov(dword [rbx + rd], eax);
    }
}

void Recompiler::CompileSllv(Emitter& e, u32 i)
{
    if (Rd(i) == 0) return;

    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rd = offsetof(Core, m_gpr) + Rd(i) * sizeof(u32);

    if (Rt(i) == 0) {
        e.xor(eax, eax);
        e.mov(dword [rbx + rd], eax);
        return;
    }

    e.mov(ecx, dword [rbx + rs]);
    e.and(ecx, 0x1f);

    if (Rd(i) == Rt(i)) {
        e.shl(dword [rbx + rd], cl);
    } else {
        e.mov(eax, dword [rbx + rt]);
        e.shl(eax, cl);
        e.mov(dword [rbx + rd], eax);
    }
}

void Recompiler::CompileSrlv(Emitter& e, u32 i)
{
    if (Rd(i) == 0) return;

    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rd = offsetof(Core, m_gpr) + Rd(i) * sizeof(u32);

    if (Rt(i) == 0) {
        e.xor(eax, eax);
        e.mov(dword [rbx + rd], eax);
        return;
    }

    e.mov(ecx, dword [rbx + rs]);
    e.and(ecx, 0x1f);

    if (Rd(i) == Rt(i)) {
        e.shr(dword [rbx + rd], cl);
    } else {
        e.mov(eax, dword [rbx + rt]);
        e.shr(eax, cl);
        e.mov(dword [rbx + rd], eax);
    }
}

void Recompiler::CompileSrav(Emitter& e, u32 i)
{
    if (Rd(i) == 0) return;

    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rd = offsetof(Core, m_gpr) + Rd(i) * sizeof(u32);

    if (Rt(i) == 0) {
        e.xor(eax, eax);
        e.mov(dword [rbx + rd], eax);
        return;
    }

    e.mov(ecx, dword [rbx + rs]);
    e.and(ecx, 0x1f);

    if (Rd(i) == Rt(i)) {
        e.sar(dword [rbx + rd], cl);
    } else {
        e.mov(eax, dword [rbx + rt]);
        e.sar(eax, cl);
        e.mov(dword [rbx + rd], eax);
    }
}

void Recompiler::CompileJr(Emitter& e, u32 i)
{
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);

    const size_t pc = offsetof(Core, m_pc);
    const size_t next_pc = offsetof(Core, m_next_pc);

    (Rs(i) == 0) ? e.xor(eax, eax) : e.mov(eax, dword [rbx + rs]);

    e.mov(dword [rbx + pc], eax);
    e.add(eax, 4);
    e.mov(dword [rbx + next_pc], eax);
}

void Recompiler::CompileJalr(Emitter& e, u32 address, u32 i)
{
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rd = offsetof(Core, m_gpr) + Rd(i) * sizeof(u32);

    const size_t pc = offsetof(Core, m_pc);
    const size_t next_pc = offsetof(Core, m_next_pc);

    (Rs(i) == 0) ? e.xor(eax, eax) : e.mov(eax, dword [rbx + rs]);

    if (Rd(i) != 0) e.mov(dword [rbx + rd], address + 8);

    e.mov(dword [rbx + pc], eax);
    e.add(eax, 4);
    e.mov(dword [rbx + next_pc], eax);
}

void Recompiler::CompileSyscall(Emitter& e, u32 address, u32 i)
{
    const size_t pc = offsetof(Core, m_pc);
    const size_t next_pc = offsetof(Core, m_next_pc);

    const size_t status = offsetof(Core, m_status.raw);
    const size_t cause = offsetof(Core, m_cause.raw);
    const size_t epc = offsetof(Core, m_epc);

    e.mov(dword [rbx + epc], address);

    e.mov(eax, dword [rbx + status]);
    e.mov(ecx, eax);
    e.shl(ecx, 2);
    e.and(ecx, 0x3f);
    e.and(eax, ~0x3f);
    e.or(eax, ecx);
    e.mov(dword [rbx + status], eax);

    e.mov(eax, dword [rbx + cause]);
    e.and(eax, 0x3fffff73);
    e.or(eax, 0x8 << 2);
    e.mov(dword [rbx + cause], eax);

    e.mov(ecx, 0x80000080);
    e.mov(edx, 0xbfc00180);

    e.bt(eax, 22);
    e.cmovc(ecx, edx);

    e.mov(dword [rbx + pc], ecx);
    e.add(ecx, 4);
    e.mov(dword [rbx + next_pc], ecx);
}

void Recompiler::CompileMfhi(Emitter& e, u32 i)
{
    if (Rd(i) == 0) return;

    const size_t rd = offsetof(Core, m_gpr) + Rd(i) * sizeof(u32);
    const size_t hi = offsetof(Core, m_hi);

    e.mov(eax, dword [rbx + hi]);
    e.mov(dword [rbx + rd], eax);
}

void Recompiler::CompileMthi(Emitter& e, u32 i)
{
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t hi = offsetof(Core, m_hi);

    (Rs(i) == 0) ? e.xor(eax, eax) : e.mov(eax, dword [rbx + rs]);
    e.mov(dword [rbx + hi], eax);
}

void Recompiler::CompileMflo(Emitter& e, u32 i)
{
    if (Rd(i) == 0) return;

    const size_t rd = offsetof(Core, m_gpr) + Rd(i) * sizeof(u32);
    const size_t lo = offsetof(Core, m_lo);

    e.mov(eax, dword [rbx + lo]);
    e.mov(dword [rbx + rd], eax);
}

void Recompiler::CompileMtlo(Emitter& e, u32 i)
{
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t lo = offsetof(Core, m_lo);

    (Rs(i) == 0) ? e.xor(eax, eax) : e.mov(eax, dword [rbx + rs]);
    e.mov(dword [rbx + lo], eax);
}

void Recompiler::CompileMult(Emitter& e, u32 i)
{
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);

    const size_t hi = offsetof(Core, m_hi);
    const size_t lo = offsetof(Core, m_lo);

    e.mov(eax, dword [rbx + rs]);
    e.mov(ecx, dword [rbx + rt]);

    e.imul(ecx);

    e.mov(dword [rbx + lo], eax);
    e.mov(dword [rbx + hi], edx);
}

void Recompiler::CompileMultu(Emitter& e, u32 i)
{
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);

    const size_t hi = offsetof(Core, m_hi);
    const size_t lo = offsetof(Core, m_lo);

    e.mov(eax, dword [rbx + rs]);
    e.mov(ecx, dword [rbx + rt]);

    e.mul(ecx);

    e.mov(dword [rbx + lo], eax);
    e.mov(dword [rbx + hi], edx);
}

void Recompiler::CompileDiv(Emitter& e, u32 i)
{
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);

    const size_t hi = offsetof(Core, m_hi);
    const size_t lo = offsetof(Core, m_lo);

    e.mov(eax, dword [rbx + rs]);
    e.mov(ecx, dword [rbx + rt]);

    e.cdq();
    e.idiv(ecx);

    e.mov(dword [rbx + lo], eax);
    e.mov(dword [rbx + hi], edx);
}

void Recompiler::CompileDivu(Emitter& e, u32 i)
{
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);

    const size_t hi = offsetof(Core, m_hi);
    const size_t lo = offsetof(Core, m_lo);

    e.mov(eax, dword [rbx + rs]);
    e.mov(ecx, dword [rbx + rt]);

    e.xor(edx, edx);
    e.div(ecx);

    e.mov(dword [rbx + lo], eax);
    e.mov(dword [rbx + hi], edx);
}

void Recompiler::CompileAddu(Emitter& e, u32 i)
{
    if (Rd(i) == 0) return;

    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rd = offsetof(Core, m_gpr) + Rd(i) * sizeof(u32);

    if (Rs(i) == 0 && Rt(i) == 0) {
        e.xor(eax, eax);
        e.mov(dword [rbx + rd], eax);
    } else if (Rs(i) == 0) {
        e.mov(eax, dword [rbx + rt]);
        e.mov(dword [rbx + rd], eax);
    } else if (Rt(i) == 0) {
        e.mov(eax, dword [rbx + rs]);
        e.mov(dword [rbx + rd], eax); 
    } else if (Rd(i) == Rs(i)) {
        e.mov(eax, dword [rbx + rt]);
        e.add(dword [rbx + rd], eax);
    } else if (Rd(i) == Rt(i)) {
        e.mov(eax, dword [rbx + rs]);
        e.add(dword [rbx + rd], eax);
    } else if (Rs(i) == Rt(i)) {
        e.mov(eax, dword [rbx + rs]);
        e.add(eax, eax);
        e.mov(dword [rbx + rd], eax);
    } else { 
        e.mov(eax, dword [rbx + rs]);
        e.mov(ecx, dword [rbx + rt]);
        e.add(eax, ecx);
        e.mov(dword [rbx + rd], eax);
    }
}

void Recompiler::CompileSubu(Emitter& e, u32 i)
{
    if (Rd(i) == 0) return;

    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rd = offsetof(Core, m_gpr) + Rd(i) * sizeof(u32);

    /*if (Rs(i) == 0 && Rt(i) == 0) {
        e.xor(eax, eax);
        e.mov(dword [rbx + rd], eax);
    } else if (Rs(i) == 0) {
        e.mov(eax, dword [rbx + rt]);
        e.neg(eax);
        e.mov(dword [rbx + rd], eax);
    } else if (Rt(i) == 0) {
        e.mov(eax, dword [rbx + rs]);
        e.mov(dword [rbx + rd], eax); 
    } else if (Rd(i) == Rs(i)) {
        e.mov(eax, dword [rbx + rt]);
        e.sub(dword [rbx + rd], eax);
    } else if (Rd(i) == Rt(i)) {
        e.mov(eax, dword [rbx + rs]);
        e.neg(dword [rbx + rd]);
        e.add(dword [rbx + rd], eax);
    } else if (Rs(i) == Rt(i)) {
        e.xor(eax, eax);
        e.mov(dword [rbx + rd], eax);
    } else {*/
        e.mov(eax, dword [rbx + rs]);
        e.mov(ecx, dword [rbx + rt]);
        e.sub(eax, ecx);
        e.mov(dword [rbx + rd], eax);
    //}
}

void Recompiler::CompileAnd(Emitter& e, u32 i)
{
    if (Rd(i) == 0) return;

    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rd = offsetof(Core, m_gpr) + Rd(i) * sizeof(u32);

    if (Rd(i) == Rs(i) && Rd(i) == Rt(i)) return;

    if (Rs(i) == 0 || Rt(i) == 0) {
        e.xor(eax, eax);
        e.mov(dword [rbx + rd], eax);
    } else if (Rd(i) == Rs(i)) {
        e.mov(eax, dword [rbx + rt]);
        e.and(dword [rbx + rd], eax);
    } else if (Rd(i) == Rt(i)) {
        e.mov(eax, dword [rbx + rs]);
        e.and(dword [rbx + rd], eax);
    } else { 
        e.mov(eax, dword [rbx + rs]);
        e.mov(ecx, dword [rbx + rt]);
        e.and(eax, ecx);
        e.mov(dword [rbx + rd], eax);
    }
}

void Recompiler::CompileOr(Emitter& e, u32 i)
{
    if (Rd(i) == 0) return;

    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rd = offsetof(Core, m_gpr) + Rd(i) * sizeof(u32);

    if (Rd(i) == Rs(i) && Rd(i) == Rt(i)) return;

    if (Rs(i) == 0 && Rt(i) == 0) {
        e.xor(eax, eax);
        e.mov(dword [rbx + rd], eax);
    } else if (Rs(i) == 0) {
        e.mov(eax, dword [rbx + rt]);
        e.mov(dword [rbx + rd], eax);
    } else if (Rt(i) == 0) {
        e.mov(eax, dword [rbx + rs]);
        e.mov(dword [rbx + rd], eax); 
    } else if (Rd(i) == Rs(i)) {
        e.mov(eax, dword [rbx + rt]);
        e.or(dword [rbx + rd], eax);
    } else if (Rd(i) == Rt(i)) {
        e.mov(eax, dword [rbx + rs]);
        e.or(dword [rbx + rd], eax);
    } else { 
        e.mov(eax, dword [rbx + rs]);
        e.mov(ecx, dword [rbx + rt]);
        e.or(eax, ecx);
        e.mov(dword [rbx + rd], eax);
    }
}

void Recompiler::CompileXor(Emitter& e, u32 i)
{
    if (Rd(i) == 0) return;

    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rd = offsetof(Core, m_gpr) + Rd(i) * sizeof(u32);

    if (Rs(i) == Rt(i)) {
        e.xor(eax, eax);
        e.mov(dword [rbx + rd], eax);
    } else if (Rs(i) == 0) {
        e.mov(eax, dword [rbx + rt]);
        e.mov(dword [rbx + rd], eax);
    } else if (Rt(i) == 0) {
        e.mov(eax, dword [rbx + rs]);
        e.mov(dword [rbx + rd], eax); 
    } else if (Rd(i) == Rs(i)) {
        e.mov(eax, dword [rbx + rt]);
        e.xor(dword [rbx + rd], eax);
    } else if (Rd(i) == Rt(i)) {
        e.mov(eax, dword [rbx + rs]);
        e.xor(dword [rbx + rd], eax);
    } else { 
        e.mov(eax, dword [rbx + rs]);
        e.mov(ecx, dword [rbx + rt]);
        e.xor(eax, ecx);
        e.mov(dword [rbx + rd], eax);
    }
}

void Recompiler::CompileNor(Emitter& e, u32 i)
{
    if (Rd(i) == 0) return;

    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rd = offsetof(Core, m_gpr) + Rd(i) * sizeof(u32);

    if (Rs(i) == Rt(i)) {
        e.xor(eax, eax);
        e.not(eax);
        e.mov(dword [rbx + rd], eax);
    } else if (Rs(i) == 0) {
        e.mov(eax, dword [rbx + rt]);
        e.not(eax);
        e.mov(dword [rbx + rd], eax);
    } else if (Rt(i) == 0) {
        e.mov(eax, dword [rbx + rs]);
        e.not(eax);
        e.mov(dword [rbx + rd], eax); 
    } else { 
        e.mov(eax, dword [rbx + rs]);
        e.mov(ecx, dword [rbx + rt]);
        e.or(eax, ecx);
        e.not(eax);
        e.mov(dword [rbx + rd], eax);
    }
}

void Recompiler::CompileSlt(Emitter& e, u32 i)
{
    if (Rd(i) == 0) return;

    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rd = offsetof(Core, m_gpr) + Rd(i) * sizeof(u32);

    if (Rs(i) == Rt(i)) {
        e.xor(eax, eax);
        e.mov(dword [rbx + rd], eax);
    } else {
        (Rs(i) == 0) ? e.xor(eax, eax) : e.mov(eax, dword [rbx + rs]);
        (Rt(i) == 0) ? e.xor(ecx, ecx) : e.mov(ecx, dword [rbx + rt]);

        e.cmp(eax, ecx);
        e.setl(al);
        e.movzx(eax, al);

        e.mov(dword [rbx + rd], eax);
    }
}

void Recompiler::CompileSltu(Emitter& e, u32 i)
{
    if (Rd(i) == 0) return;

    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rd = offsetof(Core, m_gpr) + Rd(i) * sizeof(u32);

    if (Rs(i) == Rt(i) || Rt(i) == 0) {
        e.xor(eax, eax);
        e.mov(dword [rbx + rd], eax);
    } else {
        (Rs(i) == 0) ? e.xor(eax, eax) : e.mov(eax, dword [rbx + rs]);
        e.mov(ecx, dword [rbx + rt]);

        e.cmp(eax, ecx);
        e.setb(al);
        e.movzx(eax, al);

        e.mov(dword [rbx + rd], eax);
    }
}

void Recompiler::CompileBcond(Emitter& e, u32 address, u32 i)
{
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);

    const u32 offset = Immse(i) << 2;

    const size_t pc = offsetof(Core, m_pc);
    const size_t next_pc = offsetof(Core, m_next_pc);
    const size_t ra = offsetof(Core, m_gpr) + 31 * sizeof(u32);

    const bool link = BitRange<20, 17>(i) == 0x8;
    const bool bgez = Bit::Check<16>(i);

    e.mov(dword [rbx + pc], address + 8);
    e.mov(dword [rbx + next_pc], address + 12); 

    if (link) e.mov(dword [rbx + ra], address + 8);

    if (Rs(i) == 0 && bgez) {
        e.add(dword [rbx + pc], offset - 4);
        e.add(dword [rbx + next_pc], offset - 4);
    } else if (Rs(i) == 0) {
        return;
    } else {
        e.mov(eax, dword [rbx + rs]);
        e.xor(ecx, ecx);
        e.mov(esi, offset - 4);

        e.cmp(eax, ecx);
        bgez ? e.cmovge(ecx, esi) : e.cmovl(ecx, esi);

        e.add(dword [rbx + pc], ecx);
        e.add(dword [rbx + next_pc], ecx);
    }
}

void Recompiler::CompileJ(Emitter& e, u32 address, u32 i)
{
    const u32 target = (address & 0xf0000000) | (Target(i) << 2);
    const size_t pc = offsetof(Core, m_pc);
    const size_t next_pc = offsetof(Core, m_next_pc);

    e.mov(dword [rbx + pc], target);
    e.mov(dword [rbx + next_pc], target + 4);
}

void Recompiler::CompileJal(Emitter& e, u32 address, u32 i)
{
    const u32 target = (address & 0xf0000000) | (Target(i) << 2);
    const size_t pc = offsetof(Core, m_pc);
    const size_t next_pc = offsetof(Core, m_next_pc);
    const size_t ra = offsetof(Core, m_gpr) + 31 * sizeof(u32);
    
    e.mov(dword [rbx + ra], address + 8);
    e.mov(dword [rbx + pc], target);
    e.mov(dword [rbx + next_pc], target + 4);
}

void Recompiler::CompileBeq(Emitter& e, u32 address, u32 i)
{
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);

    const u32 offset = Immse(i) << 2;

    const size_t pc = offsetof(Core, m_pc);
    const size_t next_pc = offsetof(Core, m_next_pc);

    e.mov(dword [rbx + pc], address + 8);
    e.mov(dword [rbx + next_pc], address + 12);

    if (Rs(i) == Rt(i)) {
        e.mov(eax, offset - 4);
        e.add(dword [rbx + pc], eax);
        e.add(dword [rbx + next_pc], eax);
    } else {
        (Rs(i) == 0) ? e.xor(eax, eax) : e.mov(eax, dword [rbx + rs]);
        (Rt(i) == 0) ? e.xor(ecx, ecx) : e.mov(ecx, dword [rbx + rt]);
        e.xor(edx, edx);
        e.mov(esi, offset - 4);

        e.cmp(eax, ecx);
        e.cmove(edx, esi);

        e.add(dword [rbx + pc], edx);
        e.add(dword [rbx + next_pc], edx);
    }
}

void Recompiler::CompileBne(Emitter& e, u32 address, u32 i)
{
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);

    const u32 offset = Immse(i) << 2;

    const size_t pc = offsetof(Core, m_pc);
    const size_t next_pc = offsetof(Core, m_next_pc);

    e.mov(dword [rbx + pc], address + 8);
    e.mov(dword [rbx + next_pc], address + 12);

    if (Rs(i) != Rt(i)) {
        (Rs(i) == 0) ? e.xor(eax, eax) : e.mov(eax, dword [rbx + rs]);
        (Rt(i) == 0) ? e.xor(ecx, ecx) : e.mov(ecx, dword [rbx + rt]);
        e.xor(edx, edx);
        e.mov(esi, offset - 4);

        e.cmp(eax, ecx);
        e.cmovne(edx, esi);

        e.add(dword [rbx + pc], edx);
        e.add(dword [rbx + next_pc], edx);
    }
}

void Recompiler::CompileBlez(Emitter& e, u32 address, u32 i)
{
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);

    const u32 offset = Immse(i) << 2;

    const size_t pc = offsetof(Core, m_pc);
    const size_t next_pc = offsetof(Core, m_next_pc);

    e.mov(dword [rbx + pc], address + 8);
    e.mov(dword [rbx + next_pc], address + 12);

    if (Rs(i) == 0) {
        e.mov(eax, offset - 4);
        e.add(dword [rbx + pc], eax);
        e.add(dword [rbx + next_pc], eax);
    } else {
        e.mov(eax, dword [rbx + rs]);
        e.xor(ecx, ecx);
        e.mov(edx, offset - 4);

        e.cmp(eax, 0);
        e.cmovle(ecx, edx);

        e.add(dword [rbx + pc], ecx);
        e.add(dword [rbx + next_pc], ecx);
    }
}

void Recompiler::CompileBgtz(Emitter& e, u32 address, u32 i)
{
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);

    const u32 offset = Immse(i) << 2;

    const size_t pc = offsetof(Core, m_pc);
    const size_t next_pc = offsetof(Core, m_next_pc);

    e.mov(dword [rbx + pc], address + 8);
    e.mov(dword [rbx + next_pc], address + 12);

    if (Rs(i) != 0) {
        e.mov(eax, dword [rbx + rs]);
        e.xor(ecx, ecx);
        e.mov(edx, offset - 4);

        e.cmp(eax, 0);
        e.cmovg(ecx, edx);

        e.add(dword [rbx + pc], ecx);
        e.add(dword [rbx + next_pc], ecx);
    }
}

void Recompiler::CompileAddiu(Emitter& e, u32 i)
{
    if (Rt(i) == 0) return;

    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const s32 imm = Immse(i);

    if (Rt(i) == Rs(i)) {
        if (imm) e.add(dword [rbx + rt], imm);
    } else if (Rs(i) == 0) {
        e.mov(dword [rbx + rt], imm);
    } else {
        e.mov(eax, dword [rbx + rs]);
        if (imm) e.add(eax, imm);
        e.mov(dword [rbx + rt], eax); 
    }
}

void Recompiler::CompileSlti(Emitter& e, u32 i)
{
    if (Rt(i) == 0) return;

    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const s32 imm = Immse(i);

    (Rs(i) == 0) ? e.xor(eax, eax) : e.mov(eax, dword [rbx + rs]);

    e.cmp(eax, imm);
    e.setl(al);
    e.movzx(eax, al);

    e.mov(dword [rbx + rt], eax); 
}

void Recompiler::CompileSltiu(Emitter& e, u32 i)
{
    if (Rt(i) == 0) return;

    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const s32 imm = Immse(i);

    (Rs(i) == 0) ? e.xor(eax, eax) : e.mov(eax, dword [rbx + rs]);

    e.cmp(eax, imm);
    e.setb(al);
    e.movzx(eax, al);

    e.mov(dword [rbx + rt], eax); 
}

void Recompiler::CompileAndi(Emitter& e, u32 i)
{
    if (Rt(i) == 0) return;

    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const u32 imm = Imm(i);

    if (Rt(i) == Rs(i)) {
        e.and(dword [rbx + rt], imm);
    } else if (Rs(i) == 0 || imm == 0) {
        e.xor(eax, eax);
        e.mov(dword [rbx + rt], eax);
    } else {
        e.mov(eax, dword [rbx + rs]);
        e.and(eax, imm);
        e.mov(dword [rbx + rt], eax); 
    }
}

void Recompiler::CompileOri(Emitter& e, u32 i)
{
    if (Rt(i) == 0) return;

    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const u32 imm = Imm(i);

    if (Rt(i) == Rs(i)) {
        if (imm) e.or(dword [rbx + rt], imm);
    } else if (Rs(i) == 0) {
        e.mov(dword [rbx + rt], imm);
    } else {
        e.mov(eax, dword [rbx + rs]);
        if (imm) e.or(eax, imm);
        e.mov(dword [rbx + rt], eax); 
    }
}

void Recompiler::CompileXori(Emitter& e, u32 i)
{
    if (Rt(i) == 0) return;

    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const u32 imm = Imm(i);

    if (Rt(i) == Rs(i)) {
        if (imm) e.xor(dword [rbx + rt], imm);
    } else if (Rs(i) == 0) {
        e.mov(dword [rbx + rt], imm);
    } else {
        e.mov(eax, dword [rbx + rs]);
        if (imm) e.xor(eax, imm);
        e.mov(dword [rbx + rt], eax); 
    }
}

void Recompiler::CompileLui(Emitter& e, u32 i)
{
    if (Rt(i) == 0) return;

    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const u32 imm = Imm(i) << 16;

    e.mov(dword [rbx + rt], imm);
}

void Recompiler::CompileMfc0(Emitter& e, u32 i)
{
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    
    const size_t status = offsetof(Core, m_status.raw);
    const size_t cause = offsetof(Core, m_cause.raw);
    const size_t epc = offsetof(Core, m_epc);

    switch (Rd(i)) {
    case 12:
        e.mov(eax, dword [rbx + status]);
        break;
    case 13:
        e.mov(eax, dword [rbx + cause]);
        break;
    case 14:
        e.mov(eax, dword [rbx + epc]);
        break;
    default:
        printf("mfc0 unknown register %lu\n", Rd(i));
        std::abort();
    }

    e.mov(dword [rbx + rt], eax);
}

void Recompiler::CompileMtc0(Emitter& e, u32 i)
{
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t status = offsetof(Core, m_status.raw);
    const size_t cause = offsetof(Core, m_cause.raw);

    switch (Rd(i)) {
    case 3:
    case 5:
    case 6:
    case 7:
    case 9:
    case 11:
        break;
    case 12:
        e.mov(eax, dword [rbx + rt]);
        e.and(eax, 0xf055ff3f);
        e.mov(dword [rbx + status], eax);
        break;
    case 13:
        e.mov(ecx, 0xfffffcff);
        e.and(dword [rbx + cause], ecx);

        e.mov(edx, dword [rbx + rt]);
        e.neg(ecx);
        e.and(edx, ecx);

        e.or(dword [rbx + cause], edx);
        break;
    default:
        printf("mtc0 unknown register %lu\n", Rd(i));
        std::abort();
    }
}

void Recompiler::CompileRfe(Emitter& e, u32 i)
{
    const size_t status = offsetof(Core, m_status.raw);

    e.mov(eax, dword [rbx + status]);
    e.mov(ecx, eax);
    e.shr(ecx, 2);
    e.and(ecx, 0xf);
    e.and(eax, ~0xf);
    e.or(eax, ecx);
    e.mov(dword [rbx + status], eax);
}

void Recompiler::CompileLb(Emitter& e, u32 i)
{
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const u32 imm = Immse(i);

    void *fn = reinterpret_cast<void *>(&Core::ReadByte);

    e.mov(rdi, rbx);
    (Rs(i) == 0) ? e.xor(esi, esi) : e.mov(esi, dword [rbx + rs]);
    if (imm) e.add(esi, imm);

    e.mov(rax, reinterpret_cast<uintptr_t>(fn));
    e.call(rax);

    if (Rt(i) != 0) {
        e.movsx(eax, al);
        e.mov(dword [rbx + rt], eax);
    }
}

void Recompiler::CompileLh(Emitter& e, u32 i)
{
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const u32 imm = Immse(i);

    void *fn = reinterpret_cast<void *>(&Core::ReadHalf);

    e.mov(rdi, rbx);
    (Rs(i) == 0) ? e.xor(esi, esi) : e.mov(esi, dword [rbx + rs]);
    if (imm) e.add(esi, imm);

    e.mov(rax, reinterpret_cast<uintptr_t>(fn));
    e.call(rax);

    if (Rt(i) != 0) {
        e.movsx(eax, ax);
        e.mov(dword [rbx + rt], eax);
    }
}

void Recompiler::CompileLw(Emitter& e, u32 i)
{
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const u32 imm = Immse(i);

    void *fn = reinterpret_cast<void *>(&Core::ReadWord);

    e.mov(rdi, rbx);
    (Rs(i) == 0) ? e.xor(esi, esi) : e.mov(esi, dword [rbx + rs]);
    if (imm) e.add(esi, imm);

    e.mov(rax, reinterpret_cast<uintptr_t>(fn));
    e.call(rax);

    if (Rt(i) != 0) e.mov(dword [rbx + rt], eax);
}

void Recompiler::CompileLbu(Emitter& e, u32 i)
{
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const u32 imm = Immse(i);

    void *fn = reinterpret_cast<void *>(&Core::ReadByte);

    e.mov(rdi, rbx);
    (Rs(i) == 0) ? e.xor(esi, esi) : e.mov(esi, dword [rbx + rs]);
    if (imm) e.add(esi, imm);

    e.mov(rax, reinterpret_cast<uintptr_t>(fn));
    e.call(rax);

    if (Rt(i) != 0) {
        e.movzx(eax, al);
        e.mov(dword [rbx + rt], eax);
    }
}

void Recompiler::CompileLhu(Emitter& e, u32 i)
{
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const u32 imm = Immse(i);

    void *fn = reinterpret_cast<void *>(&Core::ReadHalf);

    e.mov(rdi, rbx);
    (Rs(i) == 0) ? e.xor(esi, esi) : e.mov(esi, dword [rbx + rs]);
    if (imm) e.add(esi, imm);

    e.mov(rax, reinterpret_cast<uintptr_t>(fn));
    e.call(rax);

    if (Rt(i) != 0) {
        e.movzx(eax, ax);
        e.mov(dword [rbx + rt], eax);
    }
}

void Recompiler::CompileLwl(Emitter& e, u32 i)
{
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const u32 imm = Immse(i);

    void *fn = reinterpret_cast<void *>(&Core::ReadWord);

    e.mov(rdi, rbx);
    (Rs(i) == 0) ? e.xor(esi, esi) : e.mov(esi, dword [rbx + rs]);
    if (imm) e.add(esi, imm);

    e.mov(r12d, esi);
    e.and(r12d, 0x3);
    e.shl(r12d, 3);

    e.and(esi, ~0x3);

    e.mov(rax, reinterpret_cast<uintptr_t>(fn));
    e.call(rax);
 
    if (Rt(i) == 0) return;

    e.mov(edx, dword [rbx + rt]);

    e.mov(ecx, r12d);

    e.xor(esi, esi);
    e.not(esi);
    e.shr(esi, 8);
    e.shr(esi, cl);

    e.mov(ecx, 24);
    e.sub(ecx, r12d);
    e.shl(eax, cl);

    e.and(edx, esi);
    e.or(eax, edx);

    e.mov(dword [rbx + rt], eax);
}

void Recompiler::CompileLwr(Emitter& e, u32 i)
{
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const u32 imm = Immse(i);

    void *fn = reinterpret_cast<void *>(&Core::ReadWord);

    e.mov(rdi, rbx);
    (Rs(i) == 0) ? e.xor(esi, esi) : e.mov(esi, dword [rbx + rs]);
    if (imm) e.add(esi, imm);

    e.mov(r12d, esi);
    e.and(r12d, 0x3);
    e.shl(r12d, 0x3);

    e.and(esi, ~0x3);

    e.mov(rax, reinterpret_cast<uintptr_t>(fn));
    e.call(rax);
 
    if (Rt(i) == 0) return;

    e.mov(edx, dword [rbx + rt]);

    e.mov(ecx, 24);
    e.sub(ecx, r12d);

    e.xor(esi, esi);
    e.not(esi);
    e.shl(esi, 8);
    e.shl(esi, cl);

    e.mov(ecx, r12d);
    e.shr(eax, cl);

    e.and(edx, esi);
    e.or(eax, edx);

    e.mov(dword [rbx + rt], eax);
}


void Recompiler::CompileSb(Emitter& e, u32 i)
{
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const u32 imm = Immse(i);

    void *fn = reinterpret_cast<void *>(&Core::WriteByte);

    e.mov(rdi, rbx);
    (Rs(i) == 0) ? e.xor(esi, esi) : e.mov(esi, dword [rbx + rs]);
    if (imm) e.add(esi, imm);
    (Rt(i) == 0) ? e.xor(edx, edx) : e.mov(edx, dword [rbx + rt]);

    e.mov(rax, reinterpret_cast<uintptr_t>(fn));
    e.call(rax);
}

void Recompiler::CompileSh(Emitter& e, u32 i)
{
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const u32 imm = Immse(i);

    void *fn = reinterpret_cast<void *>(&Core::WriteHalf);

    e.mov(rdi, rbx);
    (Rs(i) == 0) ? e.xor(esi, esi) : e.mov(esi, dword [rbx + rs]);
    if (imm) e.add(esi, imm);
    (Rt(i) == 0) ? e.xor(edx, edx) : e.mov(edx, dword [rbx + rt]);

    e.mov(rax, reinterpret_cast<uintptr_t>(fn));
    e.call(rax);
}

void Recompiler::CompileSw(Emitter& e, u32 i)
{
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const u32 imm = Immse(i);

    void *fn = reinterpret_cast<void *>(&Core::WriteWord);

    e.mov(rdi, rbx);
    (Rs(i) == 0) ? e.xor(esi, esi) : e.mov(esi, dword [rbx + rs]);
    if (imm) e.add(esi, imm);
    (Rt(i) == 0) ? e.xor(edx, edx) : e.mov(edx, dword [rbx + rt]);

    e.mov(rax, reinterpret_cast<uintptr_t>(fn));
    e.call(rax);
}

void Recompiler::CompileSwl(Emitter& e, u32 i)
{
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const u32 imm = Immse(i);

    void *ld = reinterpret_cast<void *>(&Core::ReadWord);
    void *st = reinterpret_cast<void *>(&Core::WriteWord);

    e.mov(rdi, rbx);
    (Rs(i) == 0) ? e.xor(esi, esi) : e.mov(esi, dword [rbx + rs]);
    if (imm) e.add(esi, imm);

    e.mov(r12d, esi);
    e.and(r12d, 0x3);
    e.shl(r12d, 3);

    e.and(esi, ~0x3);
    e.mov(r13d, esi);

    e.mov(rax, reinterpret_cast<uintptr_t>(ld));
    e.call(rax);
 
    (Rt(i) == 0) ? e.xor(edx, edx) : e.mov(edx, dword [rbx + rt]);

    e.mov(ecx, r12d);

    e.xor(esi, esi);
    e.not(esi);
    e.shl(esi, 8);
    e.shl(esi, cl);
    e.and(eax, esi);

    e.mov(ecx, 24);
    e.sub(ecx, r12d);
    e.shr(edx, cl);

    e.or(edx, eax);

    e.mov(rdi, rbx);
    e.mov(esi, r13d);

    e.mov(rax, reinterpret_cast<uintptr_t>(st));
    e.call(rax);
}

void Recompiler::CompileSwr(Emitter& e, u32 i)
{
    const size_t rt = offsetof(Core, m_gpr) + Rt(i) * sizeof(u32);
    const size_t rs = offsetof(Core, m_gpr) + Rs(i) * sizeof(u32);
    const u32 imm = Immse(i);

    void *ld = reinterpret_cast<void *>(&Core::ReadWord);
    void *st = reinterpret_cast<void *>(&Core::WriteWord);

    e.mov(rdi, rbx);
    (Rs(i) == 0) ? e.xor(esi, esi) : e.mov(esi, dword [rbx + rs]);
    if (imm) e.add(esi, imm);

    e.mov(r12d, esi);
    e.and(r12d, 0x3);
    e.shl(r12d, 3);

    e.and(esi, ~0x3);
    e.mov(r13d, esi);

    e.mov(rax, reinterpret_cast<uintptr_t>(ld));
    e.call(rax);
 
    (Rt(i) == 0) ? e.xor(edx, edx) : e.mov(edx, dword [rbx + rt]);

    e.mov(ecx, 24);
    e.sub(ecx, r12d);
    e.xor(esi, esi);
    e.not(esi);
    e.shr(esi, 8);
    e.shr(esi, cl);
    e.and(eax, esi);

    e.mov(ecx, r12d);
    e.shl(edx, cl);

    e.or(edx, eax);

    e.mov(rdi, rbx);
    e.mov(esi, r13d);

    e.mov(rax, reinterpret_cast<uintptr_t>(st));
    e.call(rax);
}

void Recompiler::CompileGte(Emitter& e, OpClass op, u32 i)
{
    e.mov(rdi, rbx);
    e.mov(esi, i);

    void *addr;

    switch (op) {
    case OpClass::Mfc2:
        addr = reinterpret_cast<void *>(&Core::OpMfc2);
        break;
    case OpClass::Mtc2:
        addr = reinterpret_cast<void *>(&Core::OpMtc2);
        break;
    case OpClass::Cfc2:
        addr = reinterpret_cast<void *>(&Core::OpCfc2);
        break;
    case OpClass::Ctc2:
        addr = reinterpret_cast<void *>(&Core::OpCtc2);
        break;
    case OpClass::Cop2cmd:
        addr = reinterpret_cast<void *>(&Core::OpCop2cmd);
        break;
    case OpClass::Lwc2:
        addr = reinterpret_cast<void *>(&Core::OpLwc2);
        break;
    case OpClass::Swc2:
        addr = reinterpret_cast<void *>(&Core::OpSwc2);
        break;
    default:
        printf("unhandled gte op %s\n", OpTable[static_cast<int>(op)].name);
        std::abort();
    }

    e.mov(rax, reinterpret_cast<uintptr_t>(addr));
    e.call(rax);
}

void Recompiler::CompileIllegal(Emitter& e, OpClass op, u32 i)
{
    (void)e;

    /* TODO */
    printf("unhandled instruction: %s %08x\n", OpTable[static_cast<int>(op)].name, i);
    std::abort();
}

}
