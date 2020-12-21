#pragma once

#include <common/types.hpp>
#include <xbyak/xbyak.h>

#include "code_buffer.hpp"

namespace Cpu
{

class Bus;
class Block;
class Core;

class Recompiler {
public:
    Recompiler(Bus *bus, Core *cpu, size_t cache_size);

    int Run(u32 address);

    static void InvalidateAddress(u32 address);

    void ClearCache();

private:
    void AddBlockRange(Block& block, u32 address, int size);
    static void InvalidateBlock(Block& block);

    using Emitter = Xbyak::CodeGenerator;

    void CompileBlock(Block& block, u32 address);

    void CompilePrologue(Emitter& e);
    void CompileEpilogue(Emitter& e);

    void CompileInstruction(Emitter& e, OpClass op, u32 address, u32 i);

    void CompileSll(Emitter& e, u32 i);
    void CompileSrl(Emitter& e, u32 i);
    void CompileSra(Emitter& e, u32 i);

    void CompileSllv(Emitter& e, u32 i);
    void CompileSrlv(Emitter& e, u32 i);
    void CompileSrav(Emitter& e, u32 i);

    void CompileJr(Emitter& e, u32 i);
    void CompileJalr(Emitter& e, u32 address, u32 i);

    void CompileSyscall(Emitter& e, u32 address, u32 i);

    void CompileMfhi(Emitter& e, u32 i);
    void CompileMthi(Emitter& e, u32 i);
    void CompileMflo(Emitter& e, u32 i);
    void CompileMtlo(Emitter& e, u32 i);

    void CompileMult(Emitter& e, u32 i);
    void CompileMultu(Emitter& e, u32 i);
    void CompileDiv(Emitter& e, u32 i);
    void CompileDivu(Emitter& e, u32 i);

    void CompileAddu(Emitter& e, u32 i);
    void CompileSubu(Emitter& e, u32 i);

    void CompileAnd(Emitter& e, u32 i);
    void CompileOr(Emitter& e, u32 i);
    void CompileXor(Emitter& e, u32 i);
    void CompileNor(Emitter& e, u32 i);

    void CompileSlt(Emitter& e, u32 i);
    void CompileSltu(Emitter& e, u32 i);

    void CompileBcond(Emitter& e, u32 address, u32 i);

    void CompileJ(Emitter& e, u32 address, u32 i);
    void CompileJal(Emitter& e, u32 address, u32 i);

    void CompileBeq(Emitter& e, u32 address, u32 i);
    void CompileBne(Emitter& e, u32 address, u32 i);
    void CompileBlez(Emitter& e, u32 address, u32 i);
    void CompileBgtz(Emitter& e, u32 address, u32 i);

    void CompileAddiu(Emitter& e, u32 i);

    void CompileSlti(Emitter& e, u32 i);
    void CompileSltiu(Emitter& e, u32 i);

    void CompileAndi(Emitter& e, u32 i);
    void CompileOri(Emitter& e, u32 i);
    void CompileXori(Emitter& e, u32 i);
    void CompileLui(Emitter& e, u32 i);

    void CompileMfc0(Emitter& e, u32 i);
    void CompileMtc0(Emitter& e, u32 i);
    void CompileRfe(Emitter& e, u32 i);

    void CompileLb(Emitter& e, u32 i);
    void CompileLh(Emitter& e, u32 i);
    void CompileLw(Emitter& e, u32 i);

    void CompileLbu(Emitter& e, u32 i);
    void CompileLhu(Emitter& e, u32 i);

    void CompileLwl(Emitter& e, u32 i);
    void CompileLwr(Emitter& e, u32 i);

    void CompileSb(Emitter& e, u32 i);
    void CompileSh(Emitter& e, u32 i);
    void CompileSw(Emitter& e, u32 i);
    
    void CompileSwl(Emitter& e, u32 i);
    void CompileSwr(Emitter& e, u32 i);

    void CompileGte(Emitter& e, OpClass op, u32 i);

    void CompileIllegal(Emitter& e, OpClass op, u32 i);

    Bus *m_bus;
    Core *m_cpu;
    CodeBuffer m_cache;
};

}
