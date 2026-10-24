#include "x86_aot_engine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace ventura {

namespace {
const char* kReg32Names[8] = {"EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI"};
const char* kArm64RegNames[8] = {"W0", "W1", "W2", "W3", "W28", "W29", "W4", "W5"};

constexpr uint32_t kFlagCF = 0x0001u;
constexpr uint32_t kFlagZF = 0x0040u;
constexpr uint32_t kFlagSF = 0x0080u;
constexpr uint32_t kFlagDF = 0x0400u;
constexpr uint32_t kFlagOF = 0x0800u;

uint32_t* GetCpuRegPtr(CpuState32& cpu, uint8_t reg_idx) {
    switch (reg_idx & 7u) {
        case 0: return &cpu.eax;
        case 1: return &cpu.ecx;
        case 2: return &cpu.edx;
        case 3: return &cpu.ebx;
        case 4: return &cpu.esp;
        case 5: return &cpu.ebp;
        case 6: return &cpu.esi;
        default: return &cpu.edi;
    }
}

uint32_t ReadCpuReg(const CpuState32& cpu, uint8_t reg_idx) {
    switch (reg_idx & 7u) {
        case 0: return cpu.eax;
        case 1: return cpu.ecx;
        case 2: return cpu.edx;
        case 3: return cpu.ebx;
        case 4: return cpu.esp;
        case 5: return cpu.ebp;
        case 6: return cpu.esi;
        default: return cpu.edi;
    }
}

uint8_t ReadCpuReg8(const CpuState32& cpu, uint8_t reg_idx) {
    uint32_t r = ReadCpuReg(cpu, reg_idx & 3u);
    return (reg_idx & 4u) ? static_cast<uint8_t>((r >> 8) & 0xFFu) : static_cast<uint8_t>(r & 0xFFu);
}

void WriteCpuReg8(CpuState32& cpu, uint8_t reg_idx, uint8_t val) {
    uint32_t* p = GetCpuRegPtr(cpu, reg_idx & 3u);
    if (reg_idx & 4u) {
        *p = (*p & 0xFFFF00FFu) | (static_cast<uint32_t>(val) << 8);
    } else {
        *p = (*p & 0xFFFFFF00u) | static_cast<uint32_t>(val);
    }
}

bool CheckCondition(const CpuState32& cpu, uint8_t cc) {
    bool cf = (cpu.eflags & kFlagCF) != 0;
    bool zf = (cpu.eflags & kFlagZF) != 0;
    bool sf = (cpu.eflags & kFlagSF) != 0;
    bool of = (cpu.eflags & kFlagOF) != 0;
    switch (cc & 0x0Fu) {
        case 0x0: return of;
        case 0x1: return !of;
        case 0x2: return cf;
        case 0x3: return !cf;
        case 0x4: return zf;
        case 0x5: return !zf;
        case 0x6: return cf || zf;
        case 0x7: return !cf && !zf;
        case 0x8: return sf;
        case 0x9: return !sf;
        case 0xA: return false;
        case 0xB: return true;
        case 0xC: return sf != of;
        case 0xD: return sf == of;
        case 0xE: return zf || (sf != of);
        case 0xF: return !zf && (sf == of);
        default: return false;
    }
}

uint32_t EvalAlu32(CpuState32& cpu, uint8_t alu_op, uint32_t lhs, uint32_t rhs, bool* write_back) {
    *write_back = true;
    uint32_t cf_in = (cpu.eflags & kFlagCF) ? 1u : 0u;
    uint32_t res = lhs;
    bool cf = false;
    bool of = false;

    switch (alu_op & 7u) {
        case 0: // ADD
            res = lhs + rhs;
            cf = res < lhs;
            of = ((~(lhs ^ rhs) & (lhs ^ res)) & 0x80000000u) != 0;
            break;
        case 1: // OR
            res = lhs | rhs;
            break;
        case 2: { // ADC
            uint64_t full = static_cast<uint64_t>(lhs) + static_cast<uint64_t>(rhs) + cf_in;
            res = static_cast<uint32_t>(full);
            cf = (full >> 32) != 0;
            of = ((~(lhs ^ rhs) & (lhs ^ res)) & 0x80000000u) != 0;
            break;
        }
        case 3: { // SBB
            uint64_t sub = static_cast<uint64_t>(rhs) + cf_in;
            cf = static_cast<uint64_t>(lhs) < sub;
            res = static_cast<uint32_t>(static_cast<uint64_t>(lhs) - sub);
            of = (((lhs ^ rhs) & (lhs ^ res)) & 0x80000000u) != 0;
            break;
        }
        case 4: // AND
            res = lhs & rhs;
            break;
        case 5: // SUB
        case 7: // CMP
            res = lhs - rhs;
            cf = lhs < rhs;
            of = (((lhs ^ rhs) & (lhs ^ res)) & 0x80000000u) != 0;
            if ((alu_op & 7u) == 7) *write_back = false;
            break;
        case 6: // XOR
            res = lhs ^ rhs;
            break;
    }

    cpu.eflags &= ~(kFlagCF | kFlagZF | kFlagSF | kFlagOF);
    if (cf) cpu.eflags |= kFlagCF;
    if (res == 0) cpu.eflags |= kFlagZF;
    if ((res & 0x80000000u) != 0) cpu.eflags |= kFlagSF;
    if (of) cpu.eflags |= kFlagOF;
    return res;
}

uint8_t EvalAlu8(CpuState32& cpu, uint8_t alu_op, uint8_t lhs, uint8_t rhs, bool* write_back) {
    *write_back = true;
    uint8_t cf_in = (cpu.eflags & kFlagCF) ? 1u : 0u;
    uint8_t res = lhs;
    bool cf = false;
    bool of = false;

    switch (alu_op & 7u) {
        case 0:
            res = static_cast<uint8_t>(lhs + rhs);
            cf = res < lhs;
            of = ((~(lhs ^ rhs) & (lhs ^ res)) & 0x80u) != 0;
            break;
        case 1:
            res = lhs | rhs;
            break;
        case 2: {
            uint16_t full = static_cast<uint16_t>(lhs) + static_cast<uint16_t>(rhs) + cf_in;
            res = static_cast<uint8_t>(full);
            cf = (full >> 8) != 0;
            of = ((~(lhs ^ rhs) & (lhs ^ res)) & 0x80u) != 0;
            break;
        }
        case 3: {
            uint16_t sub = static_cast<uint16_t>(rhs) + cf_in;
            cf = static_cast<uint16_t>(lhs) < sub;
            res = static_cast<uint8_t>(lhs - sub);
            of = (((lhs ^ rhs) & (lhs ^ res)) & 0x80u) != 0;
            break;
        }
        case 4:
            res = lhs & rhs;
            break;
        case 5:
        case 7:
            res = static_cast<uint8_t>(lhs - rhs);
            cf = lhs < rhs;
            of = (((lhs ^ rhs) & (lhs ^ res)) & 0x80u) != 0;
            if ((alu_op & 7u) == 7) *write_back = false;
            break;
        case 6:
            res = lhs ^ rhs;
            break;
    }

    cpu.eflags &= ~(kFlagCF | kFlagZF | kFlagSF | kFlagOF);
    if (cf) cpu.eflags |= kFlagCF;
    if (res == 0) cpu.eflags |= kFlagZF;
    if ((res & 0x80u) != 0) cpu.eflags |= kFlagSF;
    if (of) cpu.eflags |= kFlagOF;
    return res;
}

void FpuPush(CpuState32& cpu, double val) {
    cpu.fpu_top = (cpu.fpu_top - 1) & 7u;
    cpu.fpu_st[cpu.fpu_top] = val;
}

double FpuPop(CpuState32& cpu) {
    double v = cpu.fpu_st[cpu.fpu_top];
    cpu.fpu_top = (cpu.fpu_top + 1) & 7u;
    return v;
}

void FpuCompare(CpuState32& cpu, double a, double b) {
    // x87 status word condition bits C0 (bit 8), C2 (bit 10), C3 (bit 14)
    cpu.fpu_status_word &= ~(0x4500u);
    if (std::isnan(a) || std::isnan(b)) {
        cpu.fpu_status_word |= 0x4500u;
    } else if (a == b) {
        cpu.fpu_status_word |= 0x4000u; // C3 = 1
    } else if (a < b) {
        cpu.fpu_status_word |= 0x0100u; // C0 = 1
    }
}
}  // namespace

void X86AotEngine::UpdateArithmeticFlags(CpuState32& cpu, uint32_t result, bool carry, bool overflow) {
    cpu.eflags &= ~(kFlagCF | kFlagZF | kFlagSF | kFlagOF);
    if (carry) cpu.eflags |= kFlagCF;
    if (result == 0) cpu.eflags |= kFlagZF;
    if ((result & 0x80000000u) != 0) cpu.eflags |= kFlagSF;
    if (overflow) cpu.eflags |= kFlagOF;
}

uint32_t X86AotEngine::DecodeModRmEffectiveAddress(
    const MemoryArena& arena,
    const CpuState32& cpu,
    uint32_t modrm_va,
    bool fs_override,
    uint8_t* out_reg_field,
    uint8_t* out_rm_field,
    uint8_t* out_mod_field,
    uint32_t* out_bytes_consumed
) {
    uint8_t modrm = arena.ReadU8(modrm_va);
    uint8_t mod = (modrm >> 6) & 3u;
    uint8_t reg = (modrm >> 3) & 7u;
    uint8_t rm  = modrm & 7u;

    if (out_reg_field) *out_reg_field = reg;
    if (out_rm_field)  *out_rm_field  = rm;
    if (out_mod_field) *out_mod_field = mod;

    uint32_t consumed = 1;
    uint32_t ea = 0;

    if (mod == 3) {
        if (out_bytes_consumed) *out_bytes_consumed = consumed;
        return 0;
    }

    if (rm == 4) {
        uint8_t sib = arena.ReadU8(modrm_va + consumed);
        consumed++;
        uint8_t scale = 1u << ((sib >> 6) & 3u);
        uint8_t index = (sib >> 3) & 7u;
        uint8_t base  = sib & 7u;

        uint32_t base_val = (mod == 0 && base == 5) ? 0 : ReadCpuReg(cpu, base);
        if (mod == 0 && base == 5) {
            base_val = arena.ReadU32(modrm_va + consumed);
            consumed += 4;
        }
        uint32_t index_val = (index == 4) ? 0 : ReadCpuReg(cpu, index);
        ea = base_val + (index_val * scale);
    } else if (mod == 0 && rm == 5) {
        ea = arena.ReadU32(modrm_va + consumed);
        consumed += 4;
    } else {
        ea = ReadCpuReg(cpu, rm);
    }

    if (mod == 1) {
        int8_t disp8 = static_cast<int8_t>(arena.ReadU8(modrm_va + consumed));
        consumed += 1;
        ea = static_cast<uint32_t>(static_cast<int64_t>(ea) + disp8);
    } else if (mod == 2) {
        int32_t disp32 = static_cast<int32_t>(arena.ReadU32(modrm_va + consumed));
        consumed += 4;
        ea = static_cast<uint32_t>(static_cast<int64_t>(ea) + disp32);
    }

    if (fs_override) {
        ea += cpu.fs_base;
    }

    if (out_bytes_consumed) *out_bytes_consumed = consumed;
    return ea;
}

uint32_t X86AotEngine::EmitArm64Equivalent(const DecodedX86Inst& inst) {
    switch (inst.ir_op) {
        case AotIrOpcode::Nop:
            return 0xD503201Fu;
        case AotIrOpcode::MovRegImm:
            return 0x52800000u | ((inst.imm_or_target & 0xFFFFu) << 5) | (inst.reg_dst & 7u);
        case AotIrOpcode::MovRegReg:
            return 0x2A0003E0u | ((inst.reg_src & 7u) << 16) | (inst.reg_dst & 7u);
        case AotIrOpcode::AddRegImm:
            return 0x11000000u | ((inst.imm_or_target & 0xFFFu) << 10) | ((inst.reg_dst & 7u) << 5) | (inst.reg_dst & 7u);
        case AotIrOpcode::SubRegImm:
            return 0x51000000u | ((inst.imm_or_target & 0xFFFu) << 10) | ((inst.reg_dst & 7u) << 5) | (inst.reg_dst & 7u);
        case AotIrOpcode::XorRegReg:
            return 0x4A000000u | ((inst.reg_src & 7u) << 16) | ((inst.reg_dst & 7u) << 5) | (inst.reg_dst & 7u);
        case AotIrOpcode::CallRel32:
        case AotIrOpcode::CallIatThunk:
            return 0x94000000u | ((inst.imm_or_target >> 2) & 0x03FFFFFFu);
        case AotIrOpcode::JmpRel32:
            return 0x14000000u | ((inst.imm_or_target >> 2) & 0x03FFFFFFu);
        case AotIrOpcode::Ret:
            return 0xD65F03C0u;
        default:
            return 0xD503201Fu;
    }
}

AotCompilationReport X86AotEngine::CompileSectionToAot(
    const MemoryArena& arena,
    uint32_t section_va,
    uint32_t section_size,
    const std::string& cache_output_path
) {
    AotCompilationReport report;
    report.code_va_start = section_va;
    report.code_size_bytes = section_size;
    report.cache_path = cache_output_path;

    const uint8_t* code = arena.GetHostPointer(section_va);
    if (code == nullptr || section_size == 0) {
        return report;
    }

    uint32_t max_scan = section_size > 524288 ? 524288 : section_size;
    uint32_t pc = 0;
    std::vector<uint32_t> arm64_stream;
    arm64_stream.reserve(max_scan / 2);

    while (pc < max_scan) {
        uint32_t cur_va = section_va + pc;
        uint8_t op = code[pc];
        DecodedX86Inst inst;
        inst.va = cur_va;

        if (cur_va == kGtaSaInitialiseGameVa || cur_va == kGtaSaCGameProcessVa || cur_va == kGtaSaRenderLoopVa) {
            report.samp_hook_sites++;
        }

        if (op >= 0x50 && op <= 0x57) {
            uint8_t reg = op - 0x50;
            inst.length = 1;
            inst.ir_op = AotIrOpcode::PushReg;
            inst.reg_src = reg;
            inst.mnemonic = std::string("PUSH ") + kReg32Names[reg];
            inst.arm64_asm = std::string("STR ") + kArm64RegNames[reg] + ", [X28, #-4]!";
        } else if (op >= 0x58 && op <= 0x5F) {
            uint8_t reg = op - 0x58;
            inst.length = 1;
            inst.ir_op = AotIrOpcode::PopReg;
            inst.reg_dst = reg;
            inst.mnemonic = std::string("POP ") + kReg32Names[reg];
            inst.arm64_asm = std::string("LDR ") + kArm64RegNames[reg] + ", [X28], #4";
        } else if (op >= 0xB8 && op <= 0xBF && (pc + 5) <= max_scan) {
            uint8_t reg = op - 0xB8;
            uint32_t imm = 0;
            std::memcpy(&imm, code + pc + 1, sizeof(uint32_t));
            inst.length = 5;
            inst.ir_op = AotIrOpcode::MovRegImm;
            inst.reg_dst = reg;
            inst.imm_or_target = imm;
        } else if (op == 0x89 && (pc + 2) <= max_scan) {
            uint8_t modrm = code[pc + 1];
            inst.length = 2;
            inst.ir_op = AotIrOpcode::MovRegReg;
            inst.reg_dst = modrm & 7u;
            inst.reg_src = (modrm >> 3) & 7u;
        } else if (op == 0xE8 && (pc + 5) <= max_scan) {
            int32_t rel32 = 0;
            std::memcpy(&rel32, code + pc + 1, sizeof(int32_t));
            uint32_t target = static_cast<uint32_t>(static_cast<int64_t>(cur_va) + 5 + rel32);
            inst.length = 5;
            inst.ir_op = AotIrOpcode::CallRel32;
            inst.imm_or_target = target;
            report.basic_block_count++;
        } else if (op == 0xFF && (pc + 6) <= max_scan && code[pc + 1] == 0x15) {
            uint32_t iat_addr = 0;
            std::memcpy(&iat_addr, code + pc + 2, sizeof(uint32_t));
            inst.length = 6;
            inst.ir_op = AotIrOpcode::CallIatThunk;
            inst.imm_or_target = iat_addr;
            report.iat_thunk_calls++;
            report.basic_block_count++;
        } else if (op >= 0xD8 && op <= 0xDF && (pc + 2) <= max_scan) {
            inst.length = 2;
            inst.ir_op = AotIrOpcode::FpuOp;
        } else if (op == 0xC3 || op == 0xC2) {
            inst.length = (op == 0xC2 && (pc + 3) <= max_scan) ? 3 : 1;
            inst.ir_op = AotIrOpcode::Ret;
            report.basic_block_count++;
        } else {
            inst.length = 1;
            inst.ir_op = AotIrOpcode::Nop;
        }

        inst.arm64_VerifyEncoding = EmitArm64Equivalent(inst);
        arm64_stream.push_back(inst.arm64_VerifyEncoding);
        report.decoded_count++;
        pc += inst.length;
    }

    if (!cache_output_path.empty()) {
        FILE* fp = std::fopen(cache_output_path.c_str(), "wb");
        if (fp) {
            const char magic[8] = {'V', 'T', 'A', 'O', 'T', '6', '4', '\0'};
            std::fwrite(magic, 1, 8, fp);
            std::fwrite(&report.code_va_start, sizeof(uint32_t), 1, fp);
            std::fwrite(&report.decoded_count, sizeof(uint32_t), 1, fp);
            std::fwrite(arm64_stream.data(), sizeof(uint32_t), arm64_stream.size(), fp);
            std::fclose(fp);
        }
    }

    report.success = true;
    return report;
}

bool X86AotEngine::ExecuteSlice(
    MemoryArena& arena,
    Win32Bridge& win32_bridge,
    CpuState32& cpu,
    uint32_t max_instructions
) {
    for (uint32_t i = 0; i < max_instructions && !cpu.halted; ++i) {
        if (!StepInstruction(arena, win32_bridge, cpu)) {
            return false;
        }
    }
    return true;
}

bool X86AotEngine::StepInstruction(
    MemoryArena& arena,
    Win32Bridge& win32_bridge,
    CpuState32& cpu
) {
    // 1. Intercept Native Win32 & COM Thunk Table Addresses
    if (win32_bridge.IsThunkAddress(cpu.eip)) {
        return win32_bridge.DispatchThunkCall(cpu.eip, arena, cpu);
    }

    if (!arena.IsValidRange(cpu.eip, 1)) {
        cpu.eip = kGtaSaEntryPointVa;
    }

    uint32_t pc = cpu.eip;
    bool fs_override = false;
    uint8_t rep_mode = 0; // 0=none, 0xF2=REPNE, 0xF3=REP/REPE
    bool operand_16 = false;

    while (true) {
        uint8_t b = arena.ReadU8(pc);
        if (b == 0x64) {
            fs_override = true;
            pc++;
        } else if (b == 0xF2 || b == 0xF3) {
            rep_mode = b;
            pc++;
        } else if (b == 0x66) {
            operand_16 = true;
            pc++;
        } else if (b == 0x26 || b == 0x2E || b == 0x36 || b == 0x3E || b == 0xF0) {
            pc++;
        } else {
            break;
        }
    }

    uint8_t op = arena.ReadU8(pc++);

    // --- Standard ALU Matrix (0x00 .. 0x3D: ADD, OR, ADC, SBB, AND, SUB, XOR, CMP) ---
    if (op <= 0x3D && ((op & 7u) <= 5u)) {
        uint8_t alu_op = (op >> 3) & 7u;
        uint8_t dir = op & 7u;
        bool write_back = true;

        if (dir == 0 || dir == 2) { // 8-bit ModR/M
            uint8_t reg = 0, rm = 0, mod = 0;
            uint32_t consumed = 0;
            uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
            uint8_t rm_v = (mod == 3) ? ReadCpuReg8(cpu, rm) : arena.ReadU8(ea);
            uint8_t r_v = ReadCpuReg8(cpu, reg);
            uint8_t res = (dir == 0)
                ? EvalAlu8(cpu, alu_op, rm_v, r_v, &write_back)
                : EvalAlu8(cpu, alu_op, r_v, rm_v, &write_back);
            if (write_back) {
                if (dir == 0) {
                    if (mod == 3) WriteCpuReg8(cpu, rm, res); else arena.WriteU8(ea, res);
                } else {
                    WriteCpuReg8(cpu, reg, res);
                }
            }
            cpu.eip = pc + consumed;
        } else if (dir == 1 || dir == 3) { // 32-bit ModR/M
            uint8_t reg = 0, rm = 0, mod = 0;
            uint32_t consumed = 0;
            uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
            uint32_t rm_v = (mod == 3) ? ReadCpuReg(cpu, rm) : arena.ReadU32(ea);
            uint32_t r_v = ReadCpuReg(cpu, reg);
            uint32_t res = (dir == 1)
                ? EvalAlu32(cpu, alu_op, rm_v, r_v, &write_back)
                : EvalAlu32(cpu, alu_op, r_v, rm_v, &write_back);
            if (write_back) {
                if (dir == 1) {
                    if (mod == 3) *GetCpuRegPtr(cpu, rm) = res; else arena.WriteU32(ea, res);
                } else {
                    *GetCpuRegPtr(cpu, reg) = res;
                }
            }
            cpu.eip = pc + consumed;
        } else if (dir == 4) { // AL, imm8
            uint8_t imm8 = arena.ReadU8(pc);
            uint8_t res = EvalAlu8(cpu, alu_op, ReadCpuReg8(cpu, 0), imm8, &write_back);
            if (write_back) WriteCpuReg8(cpu, 0, res);
            cpu.eip = pc + 1;
        } else { // EAX, imm32
            uint32_t imm = operand_16 ? arena.ReadU16(pc) : arena.ReadU32(pc);
            uint32_t res = EvalAlu32(cpu, alu_op, cpu.eax, imm, &write_back);
            if (write_back) cpu.eax = res;
            cpu.eip = pc + (operand_16 ? 2 : 4);
        }
    }
    // INC r32 (0x40..0x47) & DEC r32 (0x48..0x4F)
    else if (op >= 0x40 && op <= 0x47) {
        uint32_t* r = GetCpuRegPtr(cpu, op - 0x40);
        bool cf = (cpu.eflags & kFlagCF) != 0;
        *r += 1;
        UpdateArithmeticFlags(cpu, *r, cf, *r == 0x80000000u);
        cpu.eip = pc;
    } else if (op >= 0x48 && op <= 0x4F) {
        uint32_t* r = GetCpuRegPtr(cpu, op - 0x48);
        bool cf = (cpu.eflags & kFlagCF) != 0;
        *r -= 1;
        UpdateArithmeticFlags(cpu, *r, cf, *r == 0x7FFFFFFFu);
        cpu.eip = pc;
    }
    // PUSH r32 (0x50..0x57)
    else if (op >= 0x50 && op <= 0x57) {
        uint32_t val = ReadCpuReg(cpu, op - 0x50);
        cpu.esp -= 4;
        arena.WriteU32(cpu.esp, val);
        cpu.eip = pc;
    }
    // POP r32 (0x58..0x5F)
    else if (op >= 0x58 && op <= 0x5F) {
        uint32_t val = arena.ReadU32(cpu.esp);
        cpu.esp += 4;
        *GetCpuRegPtr(cpu, op - 0x58) = val;
        cpu.eip = pc;
    }
    // PUSHAD (0x60) & POPAD (0x61)
    else if (op == 0x60) {
        uint32_t old_esp = cpu.esp;
        uint32_t regs[8] = {cpu.eax, cpu.ecx, cpu.edx, cpu.ebx, old_esp, cpu.ebp, cpu.esi, cpu.edi};
        for (int i = 0; i < 8; ++i) {
            cpu.esp -= 4;
            arena.WriteU32(cpu.esp, regs[i]);
        }
        cpu.eip = pc;
    } else if (op == 0x61) {
        cpu.edi = arena.ReadU32(cpu.esp); cpu.esp += 4;
        cpu.esi = arena.ReadU32(cpu.esp); cpu.esp += 4;
        cpu.ebp = arena.ReadU32(cpu.esp); cpu.esp += 4;
        cpu.esp += 4; // skip ESP
        cpu.ebx = arena.ReadU32(cpu.esp); cpu.esp += 4;
        cpu.edx = arena.ReadU32(cpu.esp); cpu.esp += 4;
        cpu.ecx = arena.ReadU32(cpu.esp); cpu.esp += 4;
        cpu.eax = arena.ReadU32(cpu.esp); cpu.esp += 4;
        cpu.eip = pc;
    }
    // PUSH imm32 (0x68) / PUSH imm8 (0x6A)
    else if (op == 0x68) {
        uint32_t imm = arena.ReadU32(pc);
        cpu.esp -= 4;
        arena.WriteU32(cpu.esp, imm);
        cpu.eip = pc + 4;
    } else if (op == 0x6A) {
        int32_t imm = static_cast<int8_t>(arena.ReadU8(pc));
        cpu.esp -= 4;
        arena.WriteU32(cpu.esp, static_cast<uint32_t>(imm));
        cpu.eip = pc + 1;
    }
    // IMUL r32, r/m32, imm32 (0x69) / imm8 (0x6B)
    else if (op == 0x69 || op == 0x6B) {
        uint8_t reg = 0, rm = 0, mod = 0;
        uint32_t consumed = 0;
        uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
        int64_t lhs = static_cast<int32_t>((mod == 3) ? ReadCpuReg(cpu, rm) : arena.ReadU32(ea));
        int64_t imm = (op == 0x69)
            ? static_cast<int32_t>(arena.ReadU32(pc + consumed))
            : static_cast<int8_t>(arena.ReadU8(pc + consumed));
        consumed += (op == 0x69) ? 4 : 1;
        *GetCpuRegPtr(cpu, reg) = static_cast<uint32_t>(lhs * imm);
        cpu.eip = pc + consumed;
    }
    // Short Conditional Jumps Jcc (0x70..0x7F)
    else if (op >= 0x70 && op <= 0x7F) {
        int8_t rel8 = static_cast<int8_t>(arena.ReadU8(pc));
        uint32_t next_eip = pc + 1;
        cpu.eip = CheckCondition(cpu, op - 0x70)
            ? static_cast<uint32_t>(static_cast<int64_t>(next_eip) + rel8)
            : next_eip;
    }
    // Arithmetic Group 1 (0x80/0x82 8-bit, 0x81 imm32, 0x83 sign-extended imm8)
    else if (op == 0x80 || op == 0x82) {
        uint8_t subop = 0, rm = 0, mod = 0;
        uint32_t consumed = 0;
        uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &subop, &rm, &mod, &consumed);
        uint8_t imm8 = arena.ReadU8(pc + consumed);
        consumed += 1;
        uint8_t lhs = (mod == 3) ? ReadCpuReg8(cpu, rm) : arena.ReadU8(ea);
        bool wb = true;
        uint8_t res = EvalAlu8(cpu, subop, lhs, imm8, &wb);
        if (wb) {
            if (mod == 3) WriteCpuReg8(cpu, rm, res); else arena.WriteU8(ea, res);
        }
        cpu.eip = pc + consumed;
    } else if (op == 0x81 || op == 0x83) {
        uint8_t subop = 0, rm = 0, mod = 0;
        uint32_t consumed = 0;
        uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &subop, &rm, &mod, &consumed);
        uint32_t imm = (op == 0x81)
            ? (operand_16 ? arena.ReadU16(pc + consumed) : arena.ReadU32(pc + consumed))
            : static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(arena.ReadU8(pc + consumed))));
        consumed += (op == 0x81) ? (operand_16 ? 2 : 4) : 1;

        uint32_t lhs = (mod == 3) ? ReadCpuReg(cpu, rm) : arena.ReadU32(ea);
        bool wb = true;
        uint32_t res = EvalAlu32(cpu, subop, lhs, imm, &wb);
        if (wb) {
            if (mod == 3) *GetCpuRegPtr(cpu, rm) = res;
            else arena.WriteU32(ea, res);
        }
        cpu.eip = pc + consumed;
    }
    // TEST r/m8, r8 (0x84) & TEST r/m32, r32 (0x85)
    else if (op == 0x84 || op == 0x85) {
        uint8_t reg = 0, rm = 0, mod = 0;
        uint32_t consumed = 0;
        uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
        if (op == 0x84) {
            uint8_t v = ((mod == 3) ? ReadCpuReg8(cpu, rm) : arena.ReadU8(ea)) & ReadCpuReg8(cpu, reg);
            UpdateArithmeticFlags(cpu, v ? (v & 0x80u ? 0x80000000u : 1u) : 0u, false, false);
        } else {
            uint32_t v = ((mod == 3) ? ReadCpuReg(cpu, rm) : arena.ReadU32(ea)) & ReadCpuReg(cpu, reg);
            UpdateArithmeticFlags(cpu, v, false, false);
        }
        cpu.eip = pc + consumed;
    }
    // XCHG r/m8, r8 (0x86) & XCHG r/m32, r32 (0x87)
    else if (op == 0x86 || op == 0x87) {
        uint8_t reg = 0, rm = 0, mod = 0;
        uint32_t consumed = 0;
        uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
        if (op == 0x86) {
            uint8_t r_val = ReadCpuReg8(cpu, reg);
            uint8_t rm_val = (mod == 3) ? ReadCpuReg8(cpu, rm) : arena.ReadU8(ea);
            WriteCpuReg8(cpu, reg, rm_val);
            if (mod == 3) WriteCpuReg8(cpu, rm, r_val); else arena.WriteU8(ea, r_val);
        } else {
            uint32_t r_val = ReadCpuReg(cpu, reg);
            uint32_t rm_val = (mod == 3) ? ReadCpuReg(cpu, rm) : arena.ReadU32(ea);
            *GetCpuRegPtr(cpu, reg) = rm_val;
            if (mod == 3) *GetCpuRegPtr(cpu, rm) = r_val; else arena.WriteU32(ea, r_val);
        }
        cpu.eip = pc + consumed;
    }
    // MOV r/m8, r8 (0x88) & MOV r8, r/m8 (0x8A)
    else if (op == 0x88 || op == 0x8A) {
        uint8_t reg = 0, rm = 0, mod = 0;
        uint32_t consumed = 0;
        uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
        if (op == 0x88) {
            uint8_t v = ReadCpuReg8(cpu, reg);
            if (mod == 3) WriteCpuReg8(cpu, rm, v); else arena.WriteU8(ea, v);
        } else {
            uint8_t v = (mod == 3) ? ReadCpuReg8(cpu, rm) : arena.ReadU8(ea);
            WriteCpuReg8(cpu, reg, v);
        }
        cpu.eip = pc + consumed;
    }
    // MOV r/m32, r32 (0x89) & MOV r32, r/m32 (0x8B)
    else if (op == 0x89 || op == 0x8B) {
        uint8_t reg = 0, rm = 0, mod = 0;
        uint32_t consumed = 0;
        uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
        if (op == 0x89) {
            uint32_t src_val = ReadCpuReg(cpu, reg);
            if (mod == 3) *GetCpuRegPtr(cpu, rm) = src_val;
            else if (operand_16) arena.WriteU16(ea, static_cast<uint16_t>(src_val));
            else arena.WriteU32(ea, src_val);
        } else {
            uint32_t val = (mod == 3) ? ReadCpuReg(cpu, rm) : (operand_16 ? arena.ReadU16(ea) : arena.ReadU32(ea));
            *GetCpuRegPtr(cpu, reg) = val;
        }
        cpu.eip = pc + consumed;
    }
    // LEA r32, m (0x8D)
    else if (op == 0x8D) {
        uint8_t reg = 0, rm = 0, mod = 0;
        uint32_t consumed = 0;
        uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
        *GetCpuRegPtr(cpu, reg) = ea;
        cpu.eip = pc + consumed;
    }
    // POP r/m32 (0x8F)
    else if (op == 0x8F) {
        uint8_t reg = 0, rm = 0, mod = 0;
        uint32_t consumed = 0;
        uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
        uint32_t val = arena.ReadU32(cpu.esp);
        cpu.esp += 4;
        if (mod == 3) *GetCpuRegPtr(cpu, rm) = val; else arena.WriteU32(ea, val);
        cpu.eip = pc + consumed;
    }
    // NOP (0x90) & XCHG EAX, r32 (0x91..0x97)
    else if (op >= 0x90 && op <= 0x97) {
        if (op != 0x90) {
            uint32_t* r = GetCpuRegPtr(cpu, op - 0x90);
            std::swap(cpu.eax, *r);
        }
        cpu.eip = pc;
    }
    // CWDE (0x98), CDQ (0x99), FWAIT (0x9B), PUSHFD (0x9C), POPFD (0x9D), SAHF (0x9E), LAHF (0x9F)
    else if (op == 0x98) {
        cpu.eax = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(cpu.eax & 0xFFFFu)));
        cpu.eip = pc;
    } else if (op == 0x99) {
        cpu.edx = (cpu.eax & 0x80000000u) ? 0xFFFFFFFFu : 0u;
        cpu.eip = pc;
    } else if (op == 0x9B) {
        cpu.eip = pc;
    } else if (op == 0x9C) {
        cpu.esp -= 4;
        arena.WriteU32(cpu.esp, cpu.eflags);
        cpu.eip = pc;
    } else if (op == 0x9D) {
        cpu.eflags = arena.ReadU32(cpu.esp);
        cpu.esp += 4;
        cpu.eip = pc;
    } else if (op == 0x9E) {
        uint8_t ah = static_cast<uint8_t>((cpu.eax >> 8) & 0xFFu);
        cpu.eflags = (cpu.eflags & ~0xD5u) | (ah & 0xD5u);
        cpu.eip = pc;
    } else if (op == 0x9F) {
        uint8_t flags_low = static_cast<uint8_t>((cpu.eflags & 0xD5u) | 0x02u);
        cpu.eax = (cpu.eax & 0xFFFF00FFu) | (static_cast<uint32_t>(flags_low) << 8);
        cpu.eip = pc;
    }
    // MOV AL/EAX, moffs (0xA0..0xA3)
    else if (op == 0xA0) {
        uint32_t addr = arena.ReadU32(pc) + (fs_override ? cpu.fs_base : 0);
        WriteCpuReg8(cpu, 0, arena.ReadU8(addr));
        cpu.eip = pc + 4;
    } else if (op == 0xA1) {
        uint32_t addr = arena.ReadU32(pc) + (fs_override ? cpu.fs_base : 0);
        cpu.eax = arena.ReadU32(addr);
        cpu.eip = pc + 4;
    } else if (op == 0xA2) {
        uint32_t addr = arena.ReadU32(pc) + (fs_override ? cpu.fs_base : 0);
        arena.WriteU8(addr, ReadCpuReg8(cpu, 0));
        cpu.eip = pc + 4;
    } else if (op == 0xA3) {
        uint32_t addr = arena.ReadU32(pc) + (fs_override ? cpu.fs_base : 0);
        arena.WriteU32(addr, cpu.eax);
        cpu.eip = pc + 4;
    }
    // --- String Operations (MOVSB/D, CMPSB/D, STOSB/D, LODSB/D, SCASB/D with REP/REPE/REPNE) ---
    else if (op >= 0xA4 && op <= 0xAF) {
        if (op == 0xA8) { // TEST AL, imm8
            uint8_t v = ReadCpuReg8(cpu, 0) & arena.ReadU8(pc);
            UpdateArithmeticFlags(cpu, v ? (v & 0x80u ? 0x80000000u : 1u) : 0u, false, false);
            cpu.eip = pc + 1;
        } else if (op == 0xA9) { // TEST EAX, imm32
            uint32_t v = cpu.eax & (operand_16 ? arena.ReadU16(pc) : arena.ReadU32(pc));
            UpdateArithmeticFlags(cpu, v, false, false);
            cpu.eip = pc + (operand_16 ? 2 : 4);
        } else {
            bool is_byte = (op & 1u) == 0;
            uint32_t step = is_byte ? 1u : (operand_16 ? 2u : 4u);
            int32_t delta = (cpu.eflags & kFlagDF) ? -static_cast<int32_t>(step) : static_cast<int32_t>(step);
            uint32_t count = (rep_mode != 0) ? std::min<uint32_t>(cpu.ecx, 0x00100000u) : 1u;

            while (count > 0) {
                if (op == 0xA4 || op == 0xA5) { // MOVSB / MOVSD
                    if (is_byte) arena.WriteU8(cpu.edi, arena.ReadU8(cpu.esi));
                    else if (step == 2) arena.WriteU16(cpu.edi, arena.ReadU16(cpu.esi));
                    else arena.WriteU32(cpu.edi, arena.ReadU32(cpu.esi));
                    cpu.esi = static_cast<uint32_t>(static_cast<int64_t>(cpu.esi) + delta);
                    cpu.edi = static_cast<uint32_t>(static_cast<int64_t>(cpu.edi) + delta);
                } else if (op == 0xAA || op == 0xAB) { // STOSB / STOSD
                    if (is_byte) arena.WriteU8(cpu.edi, ReadCpuReg8(cpu, 0));
                    else if (step == 2) arena.WriteU16(cpu.edi, static_cast<uint16_t>(cpu.eax));
                    else arena.WriteU32(cpu.edi, cpu.eax);
                    cpu.edi = static_cast<uint32_t>(static_cast<int64_t>(cpu.edi) + delta);
                } else if (op == 0xAC || op == 0xAD) { // LODSB / LODSD
                    if (is_byte) WriteCpuReg8(cpu, 0, arena.ReadU8(cpu.esi));
                    else cpu.eax = arena.ReadU32(cpu.esi);
                    cpu.esi = static_cast<uint32_t>(static_cast<int64_t>(cpu.esi) + delta);
                } else if (op == 0xA6 || op == 0xA7) { // CMPSB / CMPSD
                    bool wb = false;
                    if (is_byte) EvalAlu8(cpu, 7, arena.ReadU8(cpu.esi), arena.ReadU8(cpu.edi), &wb);
                    else EvalAlu32(cpu, 7, arena.ReadU32(cpu.esi), arena.ReadU32(cpu.edi), &wb);
                    cpu.esi = static_cast<uint32_t>(static_cast<int64_t>(cpu.esi) + delta);
                    cpu.edi = static_cast<uint32_t>(static_cast<int64_t>(cpu.edi) + delta);
                } else if (op == 0xAE || op == 0xAF) { // SCASB / SCASD
                    bool wb = false;
                    if (is_byte) EvalAlu8(cpu, 7, ReadCpuReg8(cpu, 0), arena.ReadU8(cpu.edi), &wb);
                    else EvalAlu32(cpu, 7, cpu.eax, arena.ReadU32(cpu.edi), &wb);
                    cpu.edi = static_cast<uint32_t>(static_cast<int64_t>(cpu.edi) + delta);
                }

                count--;
                if (rep_mode != 0) {
                    cpu.ecx--;
                    if (op == 0xA6 || op == 0xA7 || op == 0xAE || op == 0xAF) {
                        bool zf = (cpu.eflags & kFlagZF) != 0;
                        if (rep_mode == 0xF3 && !zf) break;
                        if (rep_mode == 0xF2 && zf) break;
                    }
                }
            }
            cpu.eip = pc;
        }
    }
    // MOV r8, imm8 (0xB0..0xB7) & MOV r32, imm32 (0xB8..0xBF)
    else if (op >= 0xB0 && op <= 0xB7) {
        WriteCpuReg8(cpu, op - 0xB0, arena.ReadU8(pc));
        cpu.eip = pc + 1;
    } else if (op >= 0xB8 && op <= 0xBF) {
        uint32_t imm = operand_16 ? arena.ReadU16(pc) : arena.ReadU32(pc);
        *GetCpuRegPtr(cpu, op - 0xB8) = imm;
        cpu.eip = pc + (operand_16 ? 2 : 4);
    }
    // Shift/Rotate Group 2 (0xC0, 0xC1, 0xD0, 0xD1, 0xD2, 0xD3)
    else if (op == 0xC0 || op == 0xC1 || (op >= 0xD0 && op <= 0xD3)) {
        uint8_t subop = 0, rm = 0, mod = 0;
        uint32_t consumed = 0;
        uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &subop, &rm, &mod, &consumed);
        uint8_t count = 1;
        if (op == 0xC0 || op == 0xC1) {
            count = arena.ReadU8(pc + consumed) & 31u;
            consumed += 1;
        } else if (op == 0xD2 || op == 0xD3) {
            count = cpu.ecx & 31u;
        }
        bool is_byte = (op == 0xC0 || op == 0xD0 || op == 0xD2);
        if (is_byte) {
            uint8_t v = (mod == 3) ? ReadCpuReg8(cpu, rm) : arena.ReadU8(ea);
            if ((count & 7u) > 0) {
                uint8_t c8 = count & 7u;
                switch (subop & 7u) {
                    case 0: v = static_cast<uint8_t>((v << c8) | (v >> (8 - c8))); break;
                    case 1: v = static_cast<uint8_t>((v >> c8) | (v << (8 - c8))); break;
                    case 4: case 6: v = static_cast<uint8_t>(v << count); break;
                    case 5: v = static_cast<uint8_t>(v >> count); break;
                    case 7: v = static_cast<uint8_t>(static_cast<int8_t>(v) >> count); break;
                    default: break;
                }
                UpdateArithmeticFlags(cpu, v ? (v & 0x80u ? 0x80000000u : 1u) : 0u, false, false);
            }
            if (mod == 3) WriteCpuReg8(cpu, rm, v); else arena.WriteU8(ea, v);
        } else {
            uint32_t v = (mod == 3) ? ReadCpuReg(cpu, rm) : arena.ReadU32(ea);
            if (count > 0) {
                switch (subop & 7u) {
                    case 0: v = (v << count) | (v >> (32 - count)); break; // ROL
                    case 1: v = (v >> count) | (v << (32 - count)); break; // ROR
                    case 4: case 6: v <<= count; break;                    // SHL
                    case 5: v >>= count; break;                            // SHR
                    case 7: v = static_cast<uint32_t>(static_cast<int32_t>(v) >> count); break; // SAR
                    default: break;
                }
                UpdateArithmeticFlags(cpu, v, false, false);
            }
            if (mod == 3) *GetCpuRegPtr(cpu, rm) = v; else arena.WriteU32(ea, v);
        }
        cpu.eip = pc + consumed;
    }
    // RET (0xC3) & RET imm16 (0xC2)
    else if (op == 0xC3 || op == 0xC2) {
        uint16_t pop_bytes = (op == 0xC2) ? arena.ReadU16(pc) : 0;
        uint32_t ret_va = arena.ReadU32(cpu.esp);
        cpu.esp += 4 + pop_bytes;
        cpu.eip = ret_va;
    }
    // MOV r/m8, imm8 (0xC6) & MOV r/m32, imm32 (0xC7)
    else if (op == 0xC6 || op == 0xC7) {
        uint8_t subop = 0, rm = 0, mod = 0;
        uint32_t consumed = 0;
        uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &subop, &rm, &mod, &consumed);
        if (op == 0xC6) {
            uint8_t imm8 = arena.ReadU8(pc + consumed);
            consumed += 1;
            if (mod == 3) WriteCpuReg8(cpu, rm, imm8); else arena.WriteU8(ea, imm8);
        } else {
            uint32_t imm = operand_16 ? arena.ReadU16(pc + consumed) : arena.ReadU32(pc + consumed);
            consumed += operand_16 ? 2 : 4;
            if (mod == 3) *GetCpuRegPtr(cpu, rm) = imm; else arena.WriteU32(ea, imm);
        }
        cpu.eip = pc + consumed;
    }
    // ENTER (0xC8) & LEAVE (0xC9)
    else if (op == 0xC8) {
        uint16_t alloc_bytes = arena.ReadU16(pc);
        cpu.esp -= 4;
        arena.WriteU32(cpu.esp, cpu.ebp);
        cpu.ebp = cpu.esp;
        cpu.esp -= alloc_bytes;
        cpu.eip = pc + 3;
    } else if (op == 0xC9) {
        cpu.esp = cpu.ebp;
        cpu.ebp = arena.ReadU32(cpu.esp);
        cpu.esp += 4;
        cpu.eip = pc;
    }
    // x87 FPU Instructions (0xD8..0xDF)
    else if (op >= 0xD8 && op <= 0xDF) {
        uint8_t reg = 0, rm = 0, mod = 0;
        uint32_t consumed = 0;
        uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);

        if (op == 0xD8) {
            double rhs = (mod == 3) ? cpu.fpu_st[(cpu.fpu_top + rm) & 7u] : static_cast<double>(arena.ReadF32(ea));
            double& st0 = cpu.fpu_st[cpu.fpu_top];
            switch (reg) {
                case 0: st0 += rhs; break;
                case 1: st0 *= rhs; break;
                case 2: FpuCompare(cpu, st0, rhs); break;
                case 3: FpuCompare(cpu, st0, rhs); FpuPop(cpu); break;
                case 4: st0 -= rhs; break;
                case 5: st0 = rhs - st0; break;
                case 6: if (rhs != 0.0) st0 /= rhs; break;
                case 7: if (st0 != 0.0) st0 = rhs / st0; break;
            }
        } else if (op == 0xD9) {
            if (mod != 3) {
                if (reg == 0) FpuPush(cpu, static_cast<double>(arena.ReadF32(ea)));
                else if (reg == 2) arena.WriteF32(ea, static_cast<float>(cpu.fpu_st[cpu.fpu_top]));
                else if (reg == 3) arena.WriteF32(ea, static_cast<float>(FpuPop(cpu)));
                else if (reg == 5) cpu.fpu_control_word = arena.ReadU16(ea);
                else if (reg == 7) arena.WriteU16(ea, cpu.fpu_control_word);
            } else {
                uint8_t sub = arena.ReadU8(pc);
                if (sub >= 0xC0 && sub <= 0xC7) FpuPush(cpu, cpu.fpu_st[(cpu.fpu_top + (sub - 0xC0)) & 7u]);
                else if (sub >= 0xC8 && sub <= 0xCF) std::swap(cpu.fpu_st[cpu.fpu_top], cpu.fpu_st[(cpu.fpu_top + (sub - 0xC8)) & 7u]);
                else if (sub == 0xE0) cpu.fpu_st[cpu.fpu_top] = -cpu.fpu_st[cpu.fpu_top];
                else if (sub == 0xE1) cpu.fpu_st[cpu.fpu_top] = std::fabs(cpu.fpu_st[cpu.fpu_top]);
                else if (sub == 0xE8) FpuPush(cpu, 1.0);
                else if (sub == 0xEB) FpuPush(cpu, 3.14159265358979323846);
                else if (sub == 0xEE) FpuPush(cpu, 0.0);
                else if (sub == 0xFA) cpu.fpu_st[cpu.fpu_top] = std::sqrt(std::max(0.0, cpu.fpu_st[cpu.fpu_top]));
                else if (sub == 0xFE) cpu.fpu_st[cpu.fpu_top] = std::sin(cpu.fpu_st[cpu.fpu_top]);
                else if (sub == 0xFF) cpu.fpu_st[cpu.fpu_top] = std::cos(cpu.fpu_st[cpu.fpu_top]);
            }
        } else if (op == 0xDA && mod != 3) { // Integer 32-bit FPU ops (FIADD/FIMUL/FISUB/FIDIV)
            double rhs = static_cast<double>(static_cast<int32_t>(arena.ReadU32(ea)));
            double& st0 = cpu.fpu_st[cpu.fpu_top];
            if (reg == 0) st0 += rhs;
            else if (reg == 1) st0 *= rhs;
            else if (reg == 4) st0 -= rhs;
            else if (reg == 6 && rhs != 0.0) st0 /= rhs;
        } else if (op == 0xDB && mod != 3) { // FILD / FIST / FISTP m32int
            if (reg == 0) FpuPush(cpu, static_cast<double>(static_cast<int32_t>(arena.ReadU32(ea))));
            else if (reg == 2) arena.WriteU32(ea, static_cast<uint32_t>(static_cast<int32_t>(cpu.fpu_st[cpu.fpu_top])));
            else if (reg == 3) arena.WriteU32(ea, static_cast<uint32_t>(static_cast<int32_t>(FpuPop(cpu))));
        } else if (op == 0xDC && mod != 3) { // 64-bit double memory FPU ops
            uint64_t raw = static_cast<uint64_t>(arena.ReadU32(ea)) | (static_cast<uint64_t>(arena.ReadU32(ea + 4)) << 32);
            double rhs = 0.0;
            std::memcpy(&rhs, &raw, sizeof(double));
            double& st0 = cpu.fpu_st[cpu.fpu_top];
            if (reg == 0) st0 += rhs;
            else if (reg == 1) st0 *= rhs;
            else if (reg == 2) FpuCompare(cpu, st0, rhs);
            else if (reg == 3) { FpuCompare(cpu, st0, rhs); FpuPop(cpu); }
            else if (reg == 4) st0 -= rhs;
            else if (reg == 5) st0 = rhs - st0;
            else if (reg == 6 && rhs != 0.0) st0 /= rhs;
        } else if (op == 0xDD) {
            if (mod != 3) {
                if (reg == 0) { // FLD m64fp
                    uint64_t raw = static_cast<uint64_t>(arena.ReadU32(ea)) | (static_cast<uint64_t>(arena.ReadU32(ea + 4)) << 32);
                    double v = 0.0;
                    std::memcpy(&v, &raw, sizeof(double));
                    FpuPush(cpu, v);
                } else if (reg == 2 || reg == 3) { // FST / FSTP m64fp
                    double v = (reg == 3) ? FpuPop(cpu) : cpu.fpu_st[cpu.fpu_top];
                    uint64_t raw = 0;
                    std::memcpy(&raw, &v, sizeof(double));
                    arena.WriteU32(ea, static_cast<uint32_t>(raw & 0xFFFFFFFFu));
                    arena.WriteU32(ea + 4, static_cast<uint32_t>(raw >> 32));
                }
            } else if (reg == 3) { // FSTP ST(i)
                double v = FpuPop(cpu);
                if (rm > 0) cpu.fpu_st[(cpu.fpu_top + rm - 1) & 7u] = v;
            }
        } else if (op == 0xDE && mod == 3) { // FADDP / FMULP / FSUBP / FDIVP / FCOMPP
            double st0 = FpuPop(cpu);
            double& dst = cpu.fpu_st[cpu.fpu_top];
            if (reg == 0) dst += st0;
            else if (reg == 1) dst *= st0;
            else if (reg == 3) { FpuCompare(cpu, st0, dst); FpuPop(cpu); }
            else if (reg == 4) dst = st0 - dst;
            else if (reg == 5) dst -= st0;
            else if (reg == 6 && st0 != 0.0) dst /= st0;
            else if (reg == 7 && dst != 0.0) dst = st0 / dst;
        } else if (op == 0xDF) {
            uint8_t sub = arena.ReadU8(pc);
            if (sub == 0xE0) { // FNSTSW AX
                cpu.eax = (cpu.eax & 0xFFFF0000u) | cpu.fpu_status_word;
            }
        }
        cpu.eip = pc + consumed;
    }
    // XLATB (0xD7)
    else if (op == 0xD7) {
        uint8_t al = ReadCpuReg8(cpu, 0);
        WriteCpuReg8(cpu, 0, arena.ReadU8(cpu.ebx + al));
        cpu.eip = pc;
    }
    // LOOPNE (0xE0), LOOPE (0xE1), LOOP (0xE2), JECXZ (0xE3)
    else if (op >= 0xE0 && op <= 0xE3) {
        int8_t rel8 = static_cast<int8_t>(arena.ReadU8(pc));
        uint32_t next_eip = pc + 1;
        bool take = false;
        if (op == 0xE3) {
            take = (cpu.ecx == 0);
        } else {
            cpu.ecx -= 1;
            bool zf = (cpu.eflags & kFlagZF) != 0;
            if (op == 0xE0) take = (cpu.ecx != 0 && !zf);
            else if (op == 0xE1) take = (cpu.ecx != 0 && zf);
            else take = (cpu.ecx != 0);
        }
        cpu.eip = take ? static_cast<uint32_t>(static_cast<int64_t>(next_eip) + rel8) : next_eip;
    }
    // CALL rel32 (0xE8)
    else if (op == 0xE8) {
        int32_t rel32 = static_cast<int32_t>(arena.ReadU32(pc));
        uint32_t ret_eip = pc + 4;
        uint32_t target = static_cast<uint32_t>(static_cast<int64_t>(ret_eip) + rel32);
        cpu.esp -= 4;
        arena.WriteU32(cpu.esp, ret_eip);
        cpu.eip = target;
    }
    // JMP rel32 (0xE9) / JMP rel8 (0xEB)
    else if (op == 0xE9) {
        int32_t rel32 = static_cast<int32_t>(arena.ReadU32(pc));
        uint32_t next_eip = pc + 4;
        cpu.eip = static_cast<uint32_t>(static_cast<int64_t>(next_eip) + rel32);
    } else if (op == 0xEB) {
        int8_t rel8 = static_cast<int8_t>(arena.ReadU8(pc));
        uint32_t next_eip = pc + 1;
        cpu.eip = static_cast<uint32_t>(static_cast<int64_t>(next_eip) + rel8);
    }
    // Unary Group 3 (0xF6 8-bit / 0xF7 32-bit: TEST imm, NOT, NEG, MUL, IMUL, DIV, IDIV)
    else if (op == 0xF6 || op == 0xF7) {
        uint8_t subop = 0, rm = 0, mod = 0;
        uint32_t consumed = 0;
        uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &subop, &rm, &mod, &consumed);
        if (op == 0xF6) {
            uint8_t v = (mod == 3) ? ReadCpuReg8(cpu, rm) : arena.ReadU8(ea);
            if (subop == 0 || subop == 1) {
                uint8_t imm8 = arena.ReadU8(pc + consumed);
                consumed += 1;
                uint8_t r = v & imm8;
                UpdateArithmeticFlags(cpu, r ? (r & 0x80u ? 0x80000000u : 1u) : 0u, false, false);
            } else if (subop == 2) {
                v = ~v;
                if (mod == 3) WriteCpuReg8(cpu, rm, v); else arena.WriteU8(ea, v);
            } else if (subop == 3) {
                uint8_t r = static_cast<uint8_t>(0 - v);
                UpdateArithmeticFlags(cpu, r ? (r & 0x80u ? 0x80000000u : 1u) : 0u, v != 0, false);
                if (mod == 3) WriteCpuReg8(cpu, rm, r); else arena.WriteU8(ea, r);
            }
        } else {
            uint32_t v = (mod == 3) ? ReadCpuReg(cpu, rm) : arena.ReadU32(ea);
            if (subop == 0 || subop == 1) {
                uint32_t imm = operand_16 ? arena.ReadU16(pc + consumed) : arena.ReadU32(pc + consumed);
                consumed += operand_16 ? 2 : 4;
                UpdateArithmeticFlags(cpu, v & imm, false, false);
            } else if (subop == 2) { // NOT
                v = ~v;
                if (mod == 3) *GetCpuRegPtr(cpu, rm) = v; else arena.WriteU32(ea, v);
            } else if (subop == 3) { // NEG
                uint32_t r = 0u - v;
                UpdateArithmeticFlags(cpu, r, v != 0, v == 0x80000000u);
                if (mod == 3) *GetCpuRegPtr(cpu, rm) = r; else arena.WriteU32(ea, r);
            } else if (subop == 4) { // MUL
                uint64_t prod = static_cast<uint64_t>(cpu.eax) * static_cast<uint64_t>(v);
                cpu.eax = static_cast<uint32_t>(prod & 0xFFFFFFFFu);
                cpu.edx = static_cast<uint32_t>(prod >> 32);
            } else if (subop == 5) { // IMUL
                int64_t prod = static_cast<int64_t>(static_cast<int32_t>(cpu.eax)) * static_cast<int64_t>(static_cast<int32_t>(v));
                cpu.eax = static_cast<uint32_t>(prod & 0xFFFFFFFFu);
                cpu.edx = static_cast<uint32_t>((static_cast<uint64_t>(prod) >> 32) & 0xFFFFFFFFu);
            } else if (subop == 6 && v != 0) { // DIV
                uint64_t num = (static_cast<uint64_t>(cpu.edx) << 32) | cpu.eax;
                cpu.eax = static_cast<uint32_t>(num / v);
                cpu.edx = static_cast<uint32_t>(num % v);
            } else if (subop == 7 && v != 0) { // IDIV
                int64_t num = (static_cast<int64_t>(static_cast<int32_t>(cpu.edx)) << 32) | cpu.eax;
                int32_t div = static_cast<int32_t>(v);
                cpu.eax = static_cast<uint32_t>(num / div);
                cpu.edx = static_cast<uint32_t>(num % div);
            }
        }
        cpu.eip = pc + consumed;
    }
    // CLC (0xF8), STC (0xF9), CLD (0xFC), STD (0xFD)
    else if (op == 0xF8) { cpu.eflags &= ~kFlagCF; cpu.eip = pc; }
    else if (op == 0xF9) { cpu.eflags |= kFlagCF; cpu.eip = pc; }
    else if (op == 0xFC) { cpu.eflags &= ~kFlagDF; cpu.eip = pc; }
    else if (op == 0xFD) { cpu.eflags |= kFlagDF; cpu.eip = pc; }
    // Group 4 (0xFE: INC r/m8, DEC r/m8)
    else if (op == 0xFE) {
        uint8_t subop = 0, rm = 0, mod = 0;
        uint32_t consumed = 0;
        uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &subop, &rm, &mod, &consumed);
        uint8_t v = (mod == 3) ? ReadCpuReg8(cpu, rm) : arena.ReadU8(ea);
        uint8_t res = (subop == 0) ? static_cast<uint8_t>(v + 1) : static_cast<uint8_t>(v - 1);
        if (mod == 3) WriteCpuReg8(cpu, rm, res); else arena.WriteU8(ea, res);
        UpdateArithmeticFlags(cpu, res ? (res & 0x80u ? 0x80000000u : 1u) : 0u, (cpu.eflags & kFlagCF) != 0, false);
        cpu.eip = pc + consumed;
    }
    // Group 5 (0xFF: INC, DEC, CALL r/m32, JMP r/m32, PUSH r/m32)
    else if (op == 0xFF) {
        uint8_t subop = 0, rm = 0, mod = 0;
        uint32_t consumed = 0;
        uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &subop, &rm, &mod, &consumed);
        uint32_t target_val = (mod == 3) ? ReadCpuReg(cpu, rm) : arena.ReadU32(ea);
        uint32_t next_eip = pc + consumed;

        if (subop == 0 || subop == 1) { // INC / DEC r/m32
            uint32_t res = (subop == 0) ? (target_val + 1) : (target_val - 1);
            if (mod == 3) *GetCpuRegPtr(cpu, rm) = res; else arena.WriteU32(ea, res);
            UpdateArithmeticFlags(cpu, res, (cpu.eflags & kFlagCF) != 0, false);
            cpu.eip = next_eip;
        } else if (subop == 2) { // CALL r/m32
            cpu.esp -= 4;
            arena.WriteU32(cpu.esp, next_eip);
            cpu.eip = target_val;
        } else if (subop == 4) { // JMP r/m32
            cpu.eip = target_val;
        } else if (subop == 6) { // PUSH r/m32
            cpu.esp -= 4;
            arena.WriteU32(cpu.esp, target_val);
            cpu.eip = next_eip;
        } else {
            cpu.eip = next_eip;
        }
    }
    // Two-Byte 0x0F Instructions (Jcc, SETcc, CMOVcc, MOVZX, MOVSX, IMUL, BSF, BSR, XADD, CMPXCHG, BT*, SHLD, SHRD, SSE)
    else if (op == 0x0F) {
        uint8_t op2 = arena.ReadU8(pc++);
        if (op2 >= 0x80 && op2 <= 0x8F) { // Jcc rel32
            int32_t rel32 = static_cast<int32_t>(arena.ReadU32(pc));
            uint32_t next_eip = pc + 4;
            cpu.eip = CheckCondition(cpu, op2 - 0x80)
                ? static_cast<uint32_t>(static_cast<int64_t>(next_eip) + rel32)
                : next_eip;
        } else if (op2 >= 0x90 && op2 <= 0x9F) { // SETcc r/m8
            uint8_t reg = 0, rm = 0, mod = 0;
            uint32_t consumed = 0;
            uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
            uint8_t bit = CheckCondition(cpu, op2 - 0x90) ? 1u : 0u;
            if (mod == 3) WriteCpuReg8(cpu, rm, bit); else arena.WriteU8(ea, bit);
            cpu.eip = pc + consumed;
        } else if (op2 >= 0x40 && op2 <= 0x4F) { // CMOVcc r32, r/m32
            uint8_t reg = 0, rm = 0, mod = 0;
            uint32_t consumed = 0;
            uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
            if (CheckCondition(cpu, op2 - 0x40)) {
                *GetCpuRegPtr(cpu, reg) = (mod == 3) ? ReadCpuReg(cpu, rm) : arena.ReadU32(ea);
            }
            cpu.eip = pc + consumed;
        } else if (op2 == 0xB6 || op2 == 0xB7 || op2 == 0xBE || op2 == 0xBF) { // MOVZX / MOVSX
            uint8_t reg = 0, rm = 0, mod = 0;
            uint32_t consumed = 0;
            uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
            uint32_t val = 0;
            if (op2 == 0xB6) val = (mod == 3) ? ReadCpuReg8(cpu, rm) : arena.ReadU8(ea);
            else if (op2 == 0xB7) val = (mod == 3) ? (ReadCpuReg(cpu, rm) & 0xFFFFu) : arena.ReadU16(ea);
            else if (op2 == 0xBE) val = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>((mod == 3) ? ReadCpuReg8(cpu, rm) : arena.ReadU8(ea))));
            else val = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>((mod == 3) ? (ReadCpuReg(cpu, rm) & 0xFFFFu) : arena.ReadU16(ea))));
            *GetCpuRegPtr(cpu, reg) = val;
            cpu.eip = pc + consumed;
        } else if (op2 == 0xAF) { // IMUL r32, r/m32
            uint8_t reg = 0, rm = 0, mod = 0;
            uint32_t consumed = 0;
            uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
            int64_t a = static_cast<int32_t>(ReadCpuReg(cpu, reg));
            int64_t b = static_cast<int32_t>((mod == 3) ? ReadCpuReg(cpu, rm) : arena.ReadU32(ea));
            *GetCpuRegPtr(cpu, reg) = static_cast<uint32_t>(a * b);
            cpu.eip = pc + consumed;
        } else if (op2 == 0xB0 || op2 == 0xB1) { // CMPXCHG r/m8, r8 / CMPXCHG r/m32, r32
            uint8_t reg = 0, rm = 0, mod = 0;
            uint32_t consumed = 0;
            uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
            if (op2 == 0xB1) {
                uint32_t dst = (mod == 3) ? ReadCpuReg(cpu, rm) : arena.ReadU32(ea);
                uint32_t src = ReadCpuReg(cpu, reg);
                if (cpu.eax == dst) {
                    cpu.eflags |= kFlagZF;
                    if (mod == 3) *GetCpuRegPtr(cpu, rm) = src; else arena.WriteU32(ea, src);
                } else {
                    cpu.eflags &= ~kFlagZF;
                    cpu.eax = dst;
                }
            }
            cpu.eip = pc + consumed;
        } else if (op2 == 0xC0 || op2 == 0xC1) { // XADD r/m8, r8 / XADD r/m32, r32
            uint8_t reg = 0, rm = 0, mod = 0;
            uint32_t consumed = 0;
            uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
            if (op2 == 0xC1) {
                uint32_t dst = (mod == 3) ? ReadCpuReg(cpu, rm) : arena.ReadU32(ea);
                uint32_t src = ReadCpuReg(cpu, reg);
                uint32_t sum = dst + src;
                *GetCpuRegPtr(cpu, reg) = dst;
                if (mod == 3) *GetCpuRegPtr(cpu, rm) = sum; else arena.WriteU32(ea, sum);
                UpdateArithmeticFlags(cpu, sum, sum < dst, false);
            }
            cpu.eip = pc + consumed;
        } else if (op2 == 0xA4 || op2 == 0xAC || op2 == 0xBA || op2 == 0x70 || op2 == 0x71 || op2 == 0x72 || op2 == 0x73 || op2 == 0xC2 || op2 == 0xC4 || op2 == 0xC5 || op2 == 0xC6) {
            uint8_t reg = 0, rm = 0, mod = 0;
            uint32_t consumed = 0;
            uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
            uint8_t imm8 = arena.ReadU8(pc + consumed) & 31u;
            consumed += 1;
            if (op2 == 0xBA) { // BT / BTS / BTR / BTC r/m32, imm8
                uint32_t v = (mod == 3) ? ReadCpuReg(cpu, rm) : arena.ReadU32(ea);
                if (v & (1u << imm8)) cpu.eflags |= kFlagCF; else cpu.eflags &= ~kFlagCF;
                if (reg == 5) v |= (1u << imm8);
                else if (reg == 6) v &= ~(1u << imm8);
                else if (reg == 7) v ^= (1u << imm8);
                if (reg >= 5) {
                    if (mod == 3) *GetCpuRegPtr(cpu, rm) = v; else arena.WriteU32(ea, v);
                }
            }
            cpu.eip = pc + consumed;
        } else if (op2 == 0xBC || op2 == 0xBD) { // BSF / BSR
            uint8_t reg = 0, rm = 0, mod = 0;
            uint32_t consumed = 0;
            uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
            uint32_t src = (mod == 3) ? ReadCpuReg(cpu, rm) : arena.ReadU32(ea);
            if (src == 0) {
                cpu.eflags |= kFlagZF;
            } else {
                cpu.eflags &= ~kFlagZF;
                uint32_t idx = (op2 == 0xBC) ? static_cast<uint32_t>(__builtin_ctz(src)) : static_cast<uint32_t>(31 - __builtin_clz(src));
                *GetCpuRegPtr(cpu, reg) = idx;
            }
            cpu.eip = pc + consumed;
        } else if (op2 >= 0xC8 && op2 <= 0xCF) { // BSWAP r32
            uint32_t* r = GetCpuRegPtr(cpu, op2 - 0xC8);
            *r = __builtin_bswap32(*r);
            cpu.eip = pc;
        } else if (op2 == 0x31) { // RDTSC
            cpu.eax = static_cast<uint32_t>(cpu.retired_instructions * 16u);
            cpu.edx = static_cast<uint32_t>((cpu.retired_instructions * 16u) >> 32);
            cpu.eip = pc;
        } else if (op2 == 0x10 || op2 == 0x11 || op2 == 0x28 || op2 == 0x29 || op2 == 0x58 || op2 == 0x59 || op2 == 0x5C || op2 == 0x57) {
            uint8_t reg = 0, rm = 0, mod = 0;
            uint32_t consumed = 0;
            uint32_t ea = DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
            float rhs = (mod == 3) ? cpu.xmm[rm & 7].f32[0] : arena.ReadF32(ea);
            if (op2 == 0x10 || op2 == 0x28) cpu.xmm[reg & 7].f32[0] = rhs;
            else if (op2 == 0x11 || op2 == 0x29) {
                if (mod == 3) cpu.xmm[rm & 7].f32[0] = cpu.xmm[reg & 7].f32[0];
                else arena.WriteF32(ea, cpu.xmm[reg & 7].f32[0]);
            }
            else if (op2 == 0x58) cpu.xmm[reg & 7].f32[0] += rhs;
            else if (op2 == 0x59) cpu.xmm[reg & 7].f32[0] *= rhs;
            else if (op2 == 0x5C) cpu.xmm[reg & 7].f32[0] -= rhs;
            else if (op2 == 0x57) cpu.xmm[reg & 7] = XmmRegister{};
            cpu.eip = pc + consumed;
        } else {
            uint8_t reg = 0, rm = 0, mod = 0;
            uint32_t consumed = 0;
            DecodeModRmEffectiveAddress(arena, cpu, pc, fs_override, &reg, &rm, &mod, &consumed);
            cpu.eip = pc + consumed;
        }
    } else {
        cpu.eip = pc;
    }

    cpu.retired_instructions++;
    return true;
}

}  // namespace ventura
