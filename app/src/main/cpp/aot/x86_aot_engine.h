#pragma once

#include "ventura_types.h"
#include "../core/memory_arena.h"
#include "../bridge/win32_bridge.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ventura {

enum class AotIrOpcode : uint8_t {
    Nop = 0,
    PushReg,
    PopReg,
    MovRegImm,
    MovRegReg,
    MovRegMem,
    MovMemReg,
    AddRegImm,
    SubRegImm,
    XorRegReg,
    CmpRegImm,
    CallRel32,
    CallIatThunk,
    JmpRel32,
    JccRel32,
    FpuOp,
    SseOp,
    Ret,
    Int3
};

struct DecodedX86Inst {
    uint32_t va = 0;
    uint8_t length = 1;
    AotIrOpcode ir_op = AotIrOpcode::Nop;
    uint8_t reg_dst = 0;
    uint8_t reg_src = 0;
    uint32_t imm_or_target = 0;
    uint32_t arm64_VerifyEncoding = 0;
    std::string mnemonic;
    std::string arm64_asm;
};

struct AotCompilationReport {
    bool success = false;
    uint32_t code_va_start = 0;
    uint32_t code_size_bytes = 0;
    uint32_t decoded_count = 0;
    uint32_t basic_block_count = 0;
    uint32_t iat_thunk_calls = 0;
    uint32_t samp_hook_sites = 0;
    std::string cache_path;
    std::vector<DecodedX86Inst> sample_instructions;
};

class X86AotEngine {
public:
    static AotCompilationReport CompileSectionToAot(
        const MemoryArena& arena,
        uint32_t section_va,
        uint32_t section_size,
        const std::string& cache_output_path
    );

    static bool ExecuteSlice(
        MemoryArena& arena,
        Win32Bridge& win32_bridge,
        CpuState32& cpu,
        uint32_t max_instructions
    );

    static bool StepInstruction(
        MemoryArena& arena,
        Win32Bridge& win32_bridge,
        CpuState32& cpu
    );

    static uint32_t EmitArm64Equivalent(const DecodedX86Inst& inst);

private:
    static uint32_t DecodeModRmEffectiveAddress(
        const MemoryArena& arena,
        const CpuState32& cpu,
        uint32_t modrm_va,
        bool fs_override,
        uint8_t* out_reg_field,
        uint8_t* out_rm_field,
        uint8_t* out_mod_field,
        uint32_t* out_bytes_consumed
    );

    static void UpdateArithmeticFlags(CpuState32& cpu, uint32_t result, bool carry, bool overflow);
};

}  // namespace ventura
