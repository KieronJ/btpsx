#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include <common/bitrange.hpp>
#include <common/types.hpp>

#include <spdlog/spdlog.h>

#include "core.hpp"
#include "recompiler.hpp"

namespace Cpu
{

static constexpr bool kShouldDisassemble = false;
static constexpr std::size_t kRecompilerCacheSize = 16 * 1024 * 1024;

Core::Core(Bus *bus) : m_bus(bus)
{
    m_recompiler = std::make_unique<Recompiler>(bus, this, kRecompilerCacheSize);
}

void Core::Reset()
{
    m_pc = 0xbfc00000;
    m_next_pc = 0xbfc00004;

    m_branch = m_branch_delay = false;

    m_status.raw = m_cause.raw = 0;
    m_status.bev = true;

    m_cache_enabled = false;

    for (auto& entry : m_instruction_cache) {
        entry.valid = false;
        entry.tag = 0xffffffff;
    }
}

int Core::Run()
{
    if ((m_pc & 0x3) != 0) {
        EnterException(Exception::AddressLoad);
    }

    const u32 i = Fetch();

    const bool ip = (m_status.im & m_cause.ip) != 0;

    if (m_status.iec && ip) {
        EnterException(Exception::Interrupt);
        return 0;
    }

    if (kShouldDisassemble) {
        const std::string disassembly = Disassemble(i, m_current_pc);
        spdlog::trace("0x{:08x}: {}", m_current_pc, disassembly);
    }

    switch (Decode(i)) {
    case OpClass::Nop:                   break;
    case OpClass::Sll:     OpSll(i);     break;
    case OpClass::Srl:     OpSrl(i);     break;
    case OpClass::Sra:     OpSra(i);     break;
    case OpClass::Sllv:    OpSll(i);     break;
    case OpClass::Srlv:    OpSrl(i);     break;
    case OpClass::Srav:    OpSra(i);     break;
    case OpClass::Jr:      OpJr(i);      break;
    case OpClass::Jalr:    OpJalr(i);    break;
    case OpClass::Syscall: OpSyscall(i); break;
    case OpClass::Break:   OpBreak(i);   break;
    case OpClass::Mfhi:    OpMfhi(i);    break;
    case OpClass::Mthi:    OpMthi(i);    break;
    case OpClass::Mflo:    OpMflo(i);    break;
    case OpClass::Mtlo:    OpMtlo(i);    break;
    case OpClass::Mult:    OpMult(i);    break;
    case OpClass::Multu:   OpMultu(i);   break;
    case OpClass::Div:     OpDiv(i);     break;
    case OpClass::Divu:    OpDivu(i);    break;
    case OpClass::Add:     OpAdd(i);     break;
    case OpClass::Addu:    OpAddu(i);    break;
    case OpClass::Sub:     OpSub(i);     break;
    case OpClass::Subu:    OpSubu(i);    break;
    case OpClass::And:     OpAnd(i);     break;
    case OpClass::Or:      OpOr(i);      break;
    case OpClass::Xor:     OpXor(i);     break;
    case OpClass::Nor:     OpNor(i);     break;
    case OpClass::Slt:     OpSlt(i);     break;
    case OpClass::Sltu:    OpSltu(i);    break;
    case OpClass::Bcond:   OpBcond(i);   break;
    case OpClass::J:       OpJ(i);       break;
    case OpClass::Jal:     OpJal(i);     break;
    case OpClass::Beq:     OpBeq(i);     break;
    case OpClass::Bne:     OpBne(i);     break;
    case OpClass::Blez:    OpBlez(i);    break;
    case OpClass::Bgtz:    OpBgtz(i);    break;
    case OpClass::Addi:    OpAddi(i);    break;
    case OpClass::Addiu:   OpAddiu(i);   break;
    case OpClass::Slti:    OpSlti(i);    break;
    case OpClass::Sltiu:   OpSltiu(i);   break;
    case OpClass::Andi:    OpAndi(i);    break;
    case OpClass::Ori:     OpOri(i);     break;
    case OpClass::Xori:    OpXori(i);    break;
    case OpClass::Lui:     OpLui(i);     break;
    case OpClass::Mfc0:    OpMfc0(i);    break;
    case OpClass::Mtc0:    OpMtc0(i);    break;
    case OpClass::Rfe:     OpRfe(i);     break;
    case OpClass::Mfc2:    OpMfc2(i);    break;
    case OpClass::Cfc2:    OpCfc2(i);    break;
    case OpClass::Mtc2:    OpMtc2(i);    break;
    case OpClass::Ctc2:    OpCtc2(i);    break;
    case OpClass::Cop2cmd: OpCop2cmd(i); break;
    case OpClass::Lb:      OpLb(i);      break;
    case OpClass::Lh:      OpLh(i);      break;
    case OpClass::Lwl:     OpLwl(i);     break;
    case OpClass::Lw:      OpLw(i);      break;
    case OpClass::Lbu:     OpLbu(i);     break;
    case OpClass::Lhu:     OpLhu(i);     break;
    case OpClass::Lwr:     OpLwr(i);     break;
    case OpClass::Sb:      OpSb(i);      break;
    case OpClass::Sh:      OpSh(i);      break;
    case OpClass::Swl:     OpSwl(i);     break;
    case OpClass::Sw:      OpSw(i);      break;
    case OpClass::Swr:     OpSwr(i);     break;
    case OpClass::Lwc2:    OpLwc2(i);    break;
    case OpClass::Swc2:    OpSwc2(i);    break;
    default: OpUnknown(i);
    }

    m_branch_delay = m_branch;
    m_branch = false;
    return 1;
}

int Core::RunRecompiler()
{
    if ((m_pc & 0x3) != 0) {
        EnterException(Exception::AddressLoad);
    }

    m_current_pc = m_pc;

    const bool ip = (m_status.im & m_cause.ip) != 0;

    if (m_status.iec && ip) {
        // printf("interrupt\n");
        EnterException(Exception::Interrupt);
    }

    return m_recompiler->Run(m_pc);
}

u32 Core::Fetch()
{
    m_current_pc = m_pc;
    m_pc = m_next_pc;
    m_next_pc += 4;

    m_bus->Tick(1);

    if (m_cache_enabled && m_current_pc < 0xa0000000) {
        if (!CacheHit(m_current_pc)) {
            CacheFill(m_current_pc);
        }

        return CacheFetch(m_current_pc);
    }

    return m_bus->ReadCode(TranslateAddress(m_current_pc));
}

bool Core::CacheHit(u32 addr) const
{
    const std::size_t entry = (addr >> 4) & 0xff;

    const CacheEntry& e = m_instruction_cache[entry];
    return e.valid && e.tag == (addr & 0x7ffff000);
}

u32 Core::CacheFetch(u32 addr) const
{
    const std::size_t entry = (addr >> 4) & 0xff;
    const std::size_t index = (addr >> 2) & 0x3;

    return m_instruction_cache[entry].data[index];
}

void Core::CacheFill(u32 addr)
{
    addr &= 0x7fffffff;

    const std::size_t entry = (addr >> 4) & 0xff;

    CacheEntry& e = m_instruction_cache[entry];

    e.valid = true;
    e.tag = addr & 0x7ffff000;

    /* TODO: only fill from index onwards? */
    m_bus->BurstFill(e.data, addr & ~(CacheLineSize - 1), CacheLineSize);
}

void Core::CacheInvalidate(u32 addr)
{
    const std::size_t entry = (addr >> 4) & 0xff;

    m_instruction_cache[entry].valid = false;
}

u32 Core::TranslateAddress(u32 addr)
{
    static const u32 map[] = {
        0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
        0x1fffffff, 0x1fffffff, 0xffffffff, 0xffffffff,
    };

    return addr & map[BitRange<31, 29>(addr)];
}

u8 Core::ReadByte(u32 addr)
{
    if (m_status.isc) {
        return 0;
    }

    return m_bus->ReadByte(TranslateAddress(addr)); 
}

u16 Core::ReadHalf(u32 addr)
{
    if (m_status.isc) {
        return 0;
    }

    return m_bus->ReadHalf(TranslateAddress(addr)); 
}

u32 Core::ReadWord(u32 addr)
{
    if (m_status.isc) {
        return 0;
    }

    return m_bus->ReadWord(TranslateAddress(addr)); 
}

void Core::WriteByte(u32 addr, u8 data)
{
    if (m_status.isc) {
        CacheInvalidate(addr);
        return;
    }
 
   // printf("writing byte : 0x%04x to 0x%08x\n", data, addr);
    m_bus->WriteByte(TranslateAddress(addr), data);
}

void Core::WriteHalf(u32 addr, u16 data)
{
    if (m_status.isc) {
        CacheInvalidate(addr);
        return;
    }
 
 //   printf("writing half : 0x%04x to 0x%08x\n", data, addr);
    m_bus->WriteHalf(TranslateAddress(addr), data);
}

void Core::WriteWord(u32 addr, u32 data)
{
    if (m_status.isc) {
        CacheInvalidate(addr);
        return;
    }

    if (addr == 0xfffe0130) {
        m_cache_enabled = (data & 0x800) != 0;
        return;
    }

//    printf("writing word : 0x%08x to 0x%08x\n", data, addr);
    m_bus->WriteWord(TranslateAddress(addr), data); 
}

void Core::EnterException(Exception e)
{
    m_epc = m_current_pc;

    if (m_branch_delay) {
        printf("uh oh\n");
        m_epc -= 4;
    }

    m_status.ieo = m_status.iep;
    m_status.iep = m_status.iec;
    m_status.iec = false;

    m_status.kuo = m_status.kup;
    m_status.kup = m_status.kuc;
    m_status.kuc = false;

    m_cause.exc_code = e;
    m_cause.bt = false;
    m_cause.bd = m_branch_delay;

    m_pc = m_status.bev ? 0xbfc00180 : 0x80000080;
    m_next_pc = m_pc + 4;

    // printf("exception : setting pc to %#08x\n", m_pc);

    m_branch = false;
    m_branch_delay = false;
}

}
