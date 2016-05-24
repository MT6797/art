/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_COMPILER_OPTIMIZING_CODE_GENERATOR_ARM64_H_
#define ART_COMPILER_OPTIMIZING_CODE_GENERATOR_ARM64_H_

#include "code_generator.h"
#include "dex/compiler_enums.h"
#include "driver/compiler_options.h"
#include "nodes.h"
#include "parallel_move_resolver.h"
#include "utils/arm64/assembler_arm64.h"
#include "vixl/a64/disasm-a64.h"
#include "vixl/a64/macro-assembler-a64.h"
#include "arch/arm64/quick_method_frame_info_arm64.h"

namespace art {
namespace arm64 {

class CodeGeneratorARM64;

// Use a local definition to prevent copying mistakes.
static constexpr size_t kArm64WordSize = kArm64PointerSize;

static const vixl::Register kParameterCoreRegisters[] = {
  vixl::x1, vixl::x2, vixl::x3, vixl::x4, vixl::x5, vixl::x6, vixl::x7
};
static constexpr size_t kParameterCoreRegistersLength = arraysize(kParameterCoreRegisters);
static const vixl::FPRegister kParameterFPRegisters[] = {
  vixl::d0, vixl::d1, vixl::d2, vixl::d3, vixl::d4, vixl::d5, vixl::d6, vixl::d7
};
static constexpr size_t kParameterFPRegistersLength = arraysize(kParameterFPRegisters);

const vixl::Register tr = vixl::x18;                        // Thread Register
static const vixl::Register kArtMethodRegister = vixl::x0;  // Method register on invoke.

const vixl::CPURegList vixl_reserved_core_registers(vixl::ip0, vixl::ip1);
const vixl::CPURegList vixl_reserved_fp_registers(vixl::d31);

const vixl::CPURegList runtime_reserved_core_registers(tr, vixl::lr);

// Callee-saved registers defined by AAPCS64.
const vixl::CPURegList callee_saved_core_registers(vixl::CPURegister::kRegister,
                                                   vixl::kXRegSize,
                                                   vixl::x19.code(),
                                                   vixl::x30.code());
const vixl::CPURegList callee_saved_fp_registers(vixl::CPURegister::kFPRegister,
                                                 vixl::kDRegSize,
                                                 vixl::d8.code(),
                                                 vixl::d15.code());
Location ARM64ReturnLocation(Primitive::Type return_type);

class SlowPathCodeARM64 : public SlowPathCode {
 public:
  SlowPathCodeARM64() : entry_label_(), exit_label_() {}

  vixl::Label* GetEntryLabel() { return &entry_label_; }
  vixl::Label* GetExitLabel() { return &exit_label_; }

 private:
  vixl::Label entry_label_;
  vixl::Label exit_label_;

  DISALLOW_COPY_AND_ASSIGN(SlowPathCodeARM64);
};

static const vixl::Register kRuntimeParameterCoreRegisters[] =
    { vixl::x0, vixl::x1, vixl::x2, vixl::x3, vixl::x4, vixl::x5, vixl::x6, vixl::x7 };
static constexpr size_t kRuntimeParameterCoreRegistersLength =
    arraysize(kRuntimeParameterCoreRegisters);
static const vixl::FPRegister kRuntimeParameterFpuRegisters[] =
    { vixl::d0, vixl::d1, vixl::d2, vixl::d3, vixl::d4, vixl::d5, vixl::d6, vixl::d7 };
static constexpr size_t kRuntimeParameterFpuRegistersLength =
    arraysize(kRuntimeParameterCoreRegisters);

class InvokeRuntimeCallingConvention : public CallingConvention<vixl::Register, vixl::FPRegister> {
 public:
  static constexpr size_t kParameterCoreRegistersLength = arraysize(kParameterCoreRegisters);

  InvokeRuntimeCallingConvention()
      : CallingConvention(kRuntimeParameterCoreRegisters,
                          kRuntimeParameterCoreRegistersLength,
                          kRuntimeParameterFpuRegisters,
                          kRuntimeParameterFpuRegistersLength,
                          kArm64PointerSize) {}

  Location GetReturnLocation(Primitive::Type return_type);

 private:
  DISALLOW_COPY_AND_ASSIGN(InvokeRuntimeCallingConvention);
};

class InvokeDexCallingConvention : public CallingConvention<vixl::Register, vixl::FPRegister> {
 public:
  InvokeDexCallingConvention()
      : CallingConvention(kParameterCoreRegisters,
                          kParameterCoreRegistersLength,
                          kParameterFPRegisters,
                          kParameterFPRegistersLength,
                          kArm64PointerSize) {}

  Location GetReturnLocation(Primitive::Type return_type) {
    return ARM64ReturnLocation(return_type);
  }


 private:
  DISALLOW_COPY_AND_ASSIGN(InvokeDexCallingConvention);
};

class InvokeDexCallingConventionVisitorARM64 : public InvokeDexCallingConventionVisitor {
 public:
  InvokeDexCallingConventionVisitorARM64() {}
  virtual ~InvokeDexCallingConventionVisitorARM64() {}

  Location GetNextLocation(Primitive::Type type) OVERRIDE;
  Location GetReturnLocation(Primitive::Type return_type) {
    return calling_convention.GetReturnLocation(return_type);
  }

 private:
  InvokeDexCallingConvention calling_convention;

  DISALLOW_COPY_AND_ASSIGN(InvokeDexCallingConventionVisitorARM64);
};

#ifdef MTK_ART_COMMON
class Arm64AsmWrapper {
 public:
  explicit Arm64AsmWrapper(CodeGeneratorARM64* codegen)
      : codegen_(codegen) {}

  #define MACRO_LIST(V)  \
    V(B, 1, vixl::Label*, label)  \
    V(B, 2, vixl::Label*, label, vixl::Condition, cond)  \
    V(B, 2, vixl::Condition, cond, vixl::Label*, label)  \
    V(Cbnz, 2, const vixl::Register&, rt, vixl::Label*, label)  \
    V(Cbz, 2, const vixl::Register&, rt, vixl::Label*, label)  \
    V(Tbnz, 3, const vixl::Register&, rt, unsigned, bit_pos, vixl::Label*, label)  \
    V(Tbz, 3, const vixl::Register&, rt, unsigned, bit_pos, vixl::Label*, label)  \
    V(Bl, 1, vixl::Label*, label)  \
    V(Blr, 1, const vixl::Register&, xn)  \
    V(Ret, 0)  \
    V(Ldr, 2, const vixl::CPURegister&, rt, const vixl::MemOperand&, addr)  \
    V(Ldrb, 2, const vixl::Register&, rt, const vixl::MemOperand&, addr)  \
    V(Ldrsb, 2, const vixl::Register&, rt, const vixl::MemOperand&, addr)  \
    V(Ldrh, 2, const vixl::Register&, rt, const vixl::MemOperand&, addr)  \
    V(Ldrsh, 2, const vixl::Register&, rt, const vixl::MemOperand&, addr)  \
    V(Ldrsw, 2, const vixl::Register&, rt, const vixl::MemOperand&, addr)  \
    V(Str, 2, const vixl::CPURegister&, rt, const vixl::MemOperand&, addr)  \
    V(Strb, 2, const vixl::Register&, rt, const vixl::MemOperand&, addr)  \
    V(Strh, 2, const vixl::Register&, rt, const vixl::MemOperand&, addr)  \
    V(Ldar, 2, const vixl::Register&, rt, const vixl::MemOperand&, src)  \
    V(Ldarb, 2, const vixl::Register&, rt, const vixl::MemOperand&, src)  \
    V(Ldarh, 2, const vixl::Register&, rt, const vixl::MemOperand&, src)  \
    V(Stlr, 2, const vixl::Register&, rt, const vixl::MemOperand&, dst)  \
    V(Stlrb, 2, const vixl::Register&, rt, const vixl::MemOperand&, dst)  \
    V(Stlrh, 2, const vixl::Register&, rt, const vixl::MemOperand&, dst)  \
    V(Add, 3, const vixl::Register&, rd, const vixl::Register&, rn, const vixl::Operand&, operand)  \
    V(Sub, 3, const vixl::Register&, rd, const vixl::Register&, rn, const vixl::Operand&, operand)  \
    V(Mul, 3, const vixl::Register&, rd, const vixl::Register&, rn, const vixl::Register&, rm)  \
    V(Neg, 2, const vixl::Register&, rd, const vixl::Operand&, operand)  \
    V(And, 3, const vixl::Register&, rd, const vixl::Register&, rn, const vixl::Operand&, operand)  \
    V(Orr, 3, const vixl::Register&, rd, const vixl::Register&, rn, const vixl::Operand&, operand)  \
    V(Eor, 3, const vixl::Register&, rd, const vixl::Register&, rn, const vixl::Operand&, operand)  \
    V(Lsl, 3, const vixl::Register&, rd, const vixl::Register&, rn, unsigned, shift)  \
    V(Lsl, 3, const vixl::Register&, rd, const vixl::Register&, rn, const vixl::Register&, rm)  \
    V(Lsr, 3, const vixl::Register&, rd, const vixl::Register&, rn, unsigned, shift)  \
    V(Lsr, 3, const vixl::Register&, rd, const vixl::Register&, rn, const vixl::Register&, rm)  \
    V(Asr, 3, const vixl::Register&, rd, const vixl::Register&, rn, unsigned, shift)  \
    V(Asr, 3, const vixl::Register&, rd, const vixl::Register&, rn, const vixl::Register&, rm)  \
    V(Sdiv, 3, const vixl::Register&, rd, const vixl::Register&, rn, const vixl::Register&, rm)  \
    V(Madd, 4, const vixl::Register&, rd, const vixl::Register&, rn, const vixl::Register&, rm, const vixl::Register&, ra)  \
    V(Msub, 4, const vixl::Register&, rd, const vixl::Register&, rn, const vixl::Register&, rm, const vixl::Register&, ra)  \
    V(Ubfx, 4, const vixl::Register&, rd, const vixl::Register&, rn, unsigned, lsb, unsigned, width)  \
    V(Sbfx, 4, const vixl::Register&, rd, const vixl::Register&, rn, unsigned, lsb, unsigned, width)  \
    V(Cmp, 2, const vixl::Register&, rn, const vixl::Operand&, operand)  \
    V(Fadd, 3, const vixl::VRegister&, vd, const vixl::VRegister&, vn, const vixl::VRegister&, vm)  \
    V(Fsub, 3, const vixl::VRegister&, vd, const vixl::VRegister&, vn, const vixl::VRegister&, vm)  \
    V(Fmul, 3, const vixl::VRegister&, vd, const vixl::VRegister&, vn, const vixl::VRegister&, vm)  \
    V(Fdiv, 3, const vixl::VRegister&, vd, const vixl::VRegister&, vn, const vixl::VRegister&, vm)  \
    V(Fneg, 2, const vixl::VRegister&, vd, const vixl::VRegister&, vn)  \
    V(Fmadd, 4, const vixl::VRegister&, vd, const vixl::VRegister&, vn, const vixl::VRegister&, vm, const vixl::VRegister&, va)\
    V(Cneg, 3, const vixl::Register&, rd, const vixl::Register&, rn, vixl::Condition, cond)  \
    V(Fcmp, 2, const vixl::VRegister&, vn, const vixl::VRegister&, vm)  \
    V(Fcmp, 2, const vixl::VRegister&, vn, double, value)  \
    V(Cset, 2, const vixl::Register&, rd, vixl::Condition, cond)  \
    V(Csetm, 2, const vixl::Register&, rd, vixl::Condition, cond)  \
    V(Fcvt, 2, const vixl::VRegister&, vd, const vixl::VRegister&, vn)  \
    V(Fcvtzs, 2, const vixl::Register&, rd, const vixl::VRegister&, vn)  \
    V(Fcvtzs, 2, const vixl::VRegister&, vd, const vixl::VRegister&, vn)  \
    V(Scvtf, 2, const vixl::VRegister&, vd, const vixl::Register&, rn)  \
    V(Scvtf, 2, const vixl::VRegister&, vd, const vixl::VRegister&, vn)  \
    V(Mov, 2, const vixl::Register&, rd, uint64_t, imm)  \
    V(Mov, 2, const vixl::Register&, rd, const vixl::Register&, rn)  \
    V(Mvn, 2, const vixl::Register&, rd, const vixl::Operand&, operand)  \
    V(Fmov, 2, vixl::VRegister, vd, vixl::VRegister, vn)  \
    V(Fmov, 2, vixl::VRegister, vd, vixl::Register, rn)  \
    V(Fmov, 2, vixl::Register, rd, vixl::VRegister, vn)  \
    V(Fmov, 2, vixl::VRegister, vd, double, imm)  \
    V(Fmov, 2, vixl::VRegister, vd, float, imm)  \
    V(Dmb, 2, vixl::BarrierDomain, domain, vixl::BarrierType, type)  \
    V(Drop, 1, const vixl::Operand&, size)  \
    V(Bind, 1, vixl::Label*, label)  \
    V(FinalizeCode, 0)  \

  #define DECLARE_ASM_WRAPPER_FUNC_0(NAME, ...)                           \
    void NAME();
  #define DECLARE_ASM_WRAPPER_FUNC_1(NAME, T1, V1)                         \
    void NAME(T1 V1);
  #define DECLARE_ASM_WRAPPER_FUNC_2(NAME, T1, V1, T2, V2)                   \
    void NAME(T1 V1, T2 V2);
  #define DECLARE_ASM_WRAPPER_FUNC_3(NAME, T1, V1, T2, V2, T3, V3)             \
    void NAME(T1 V1, T2 V2, T3 V3);
  #define DECLARE_ASM_WRAPPER_FUNC_4(NAME, T1, V1, T2, V2, T3, V3, T4, V4)       \
    void NAME(T1 V1, T2 V2, T3 V3, T4 V4);
  #define DECLARE_ASM_WRAPPER_FUNC_5(NAME, T1, V1, T2, V2, T3, V3, T4, V4, T5, V5) \
    void NAME(T1 V1, T2 V2, T3 V3, T4 V4, T5 V5);

  #define DECLARE_ASM_WRAPPER_FUNC(NAME, NUMARG, ...)                              \
    DECLARE_ASM_WRAPPER_FUNC_##NUMARG(NAME, __VA_ARGS__)
    MACRO_LIST(DECLARE_ASM_WRAPPER_FUNC)
  #undef DECLARE_ASM_WRAPPER_FUNC

 private:
  CodeGeneratorARM64* const codegen_;
};
#endif

class InstructionCodeGeneratorARM64 : public HGraphVisitor {
 public:
  InstructionCodeGeneratorARM64(HGraph* graph, CodeGeneratorARM64* codegen);

#define DECLARE_VISIT_INSTRUCTION(name, super) \
  void Visit##name(H##name* instr) OVERRIDE;
  FOR_EACH_CONCRETE_INSTRUCTION(DECLARE_VISIT_INSTRUCTION)
  #ifdef MTK_ART_COMMON
  FOR_EACH_MTK_INSTRUCTION(DECLARE_VISIT_INSTRUCTION)
  #endif
#undef DECLARE_VISIT_INSTRUCTION

  void LoadCurrentMethod(XRegister reg);

  Arm64Assembler* GetAssembler() const { return assembler_; }
  vixl::MacroAssembler* GetVIXLAssembler() { return GetAssembler()->vixl_masm_; }
#ifdef MTK_ART_COMMON
  Arm64AsmWrapper* GetArm64AsmWrapper();
#endif

 private:
  void GenerateClassInitializationCheck(SlowPathCodeARM64* slow_path, vixl::Register class_reg);
  void GenerateMemoryBarrier(MemBarrierKind kind);
  void GenerateSuspendCheck(HSuspendCheck* instruction, HBasicBlock* successor);
  void HandleBinaryOp(HBinaryOperation* instr);
  void HandleFieldSet(HInstruction* instruction, const FieldInfo& field_info);
  void HandleFieldGet(HInstruction* instruction, const FieldInfo& field_info);
  void HandleShift(HBinaryOperation* instr);
  void GenerateImplicitNullCheck(HNullCheck* instruction);
  void GenerateExplicitNullCheck(HNullCheck* instruction);
  void GenerateTestAndBranch(HInstruction* instruction,
                             vixl::Label* true_target,
                             vixl::Label* false_target,
                             vixl::Label* always_true_target);

  Arm64Assembler* const assembler_;
  CodeGeneratorARM64* const codegen_;

  DISALLOW_COPY_AND_ASSIGN(InstructionCodeGeneratorARM64);
};

class LocationsBuilderARM64 : public HGraphVisitor {
 public:
  explicit LocationsBuilderARM64(HGraph* graph, CodeGeneratorARM64* codegen)
      : HGraphVisitor(graph), codegen_(codegen) {}

#define DECLARE_VISIT_INSTRUCTION(name, super) \
  void Visit##name(H##name* instr) OVERRIDE;
  FOR_EACH_CONCRETE_INSTRUCTION(DECLARE_VISIT_INSTRUCTION)
  #ifdef MTK_ART_COMMON
  FOR_EACH_MTK_INSTRUCTION(DECLARE_VISIT_INSTRUCTION)
  #endif
#undef DECLARE_VISIT_INSTRUCTION

 private:
  void HandleBinaryOp(HBinaryOperation* instr);
  void HandleFieldSet(HInstruction* instruction);
  void HandleFieldGet(HInstruction* instruction);
  void HandleInvoke(HInvoke* instr);
  void HandleShift(HBinaryOperation* instr);

  CodeGeneratorARM64* const codegen_;
  InvokeDexCallingConventionVisitorARM64 parameter_visitor_;

  DISALLOW_COPY_AND_ASSIGN(LocationsBuilderARM64);
};

class ParallelMoveResolverARM64 : public ParallelMoveResolverNoSwap {
 public:
  ParallelMoveResolverARM64(ArenaAllocator* allocator, CodeGeneratorARM64* codegen)
      : ParallelMoveResolverNoSwap(allocator), codegen_(codegen), vixl_temps_() {}

 protected:
  void PrepareForEmitNativeCode() OVERRIDE;
  void FinishEmitNativeCode() OVERRIDE;
  Location AllocateScratchLocationFor(Location::Kind kind) OVERRIDE;
  void FreeScratchLocation(Location loc) OVERRIDE;
  void EmitMove(size_t index) OVERRIDE;

 private:
  Arm64Assembler* GetAssembler() const;
  vixl::MacroAssembler* GetVIXLAssembler() const {
    return GetAssembler()->vixl_masm_;
  }

  CodeGeneratorARM64* const codegen_;
  vixl::UseScratchRegisterScope vixl_temps_;

  DISALLOW_COPY_AND_ASSIGN(ParallelMoveResolverARM64);
};

class CodeGeneratorARM64 : public CodeGenerator {
 public:
  CodeGeneratorARM64(HGraph* graph,
                     const Arm64InstructionSetFeatures& isa_features,
                     const CompilerOptions& compiler_options);
  virtual ~CodeGeneratorARM64() {}

  void GenerateFrameEntry() OVERRIDE;
  void GenerateFrameExit() OVERRIDE;

  vixl::CPURegList GetFramePreservedCoreRegisters() const {
    return vixl::CPURegList(vixl::CPURegister::kRegister, vixl::kXRegSize,
                            core_spill_mask_);
  }

  vixl::CPURegList GetFramePreservedFPRegisters() const {
    return vixl::CPURegList(vixl::CPURegister::kFPRegister, vixl::kDRegSize,
                            fpu_spill_mask_);
  }

  void Bind(HBasicBlock* block) OVERRIDE;

  vixl::Label* GetLabelOf(HBasicBlock* block) const {
    return CommonGetLabelOf<vixl::Label>(block_labels_, block);
  }

  void Move(HInstruction* instruction, Location location, HInstruction* move_for) OVERRIDE;

  size_t GetWordSize() const OVERRIDE {
    return kArm64WordSize;
  }

  size_t GetFloatingPointSpillSlotSize() const OVERRIDE {
    // Allocated in D registers, which are word sized.
    return kArm64WordSize;
  }

  uintptr_t GetAddressOf(HBasicBlock* block) const OVERRIDE {
    vixl::Label* block_entry_label = GetLabelOf(block);
    DCHECK(block_entry_label->IsBound());
    return block_entry_label->location();
  }

  HGraphVisitor* GetLocationBuilder() OVERRIDE { return &location_builder_; }
  HGraphVisitor* GetInstructionVisitor() OVERRIDE { return &instruction_visitor_; }
  Arm64Assembler* GetAssembler() OVERRIDE { return &assembler_; }
  vixl::MacroAssembler* GetVIXLAssembler() { return GetAssembler()->vixl_masm_; }
#ifdef MTK_ART_COMMON
  Arm64AsmWrapper* GetArm64AsmWrapper() { return &asm_wrapper_; }
#endif

  // Emit a write barrier.
  void MarkGCCard(vixl::Register object, vixl::Register value);

  // Register allocation.

  void SetupBlockedRegisters(bool is_baseline) const OVERRIDE;
  // AllocateFreeRegister() is only used when allocating registers locally
  // during CompileBaseline().
  Location AllocateFreeRegister(Primitive::Type type) const OVERRIDE;

  Location GetStackLocation(HLoadLocal* load) const OVERRIDE;

  size_t SaveCoreRegister(size_t stack_index, uint32_t reg_id);
  size_t RestoreCoreRegister(size_t stack_index, uint32_t reg_id);
  size_t SaveFloatingPointRegister(size_t stack_index, uint32_t reg_id);
  size_t RestoreFloatingPointRegister(size_t stack_index, uint32_t reg_id);

  // The number of registers that can be allocated. The register allocator may
  // decide to reserve and not use a few of them.
  // We do not consider registers sp, xzr, wzr. They are either not allocatable
  // (xzr, wzr), or make for poor allocatable registers (sp alignment
  // requirements, etc.). This also facilitates our task as all other registers
  // can easily be mapped via to or from their type and index or code.
  static const int kNumberOfAllocatableRegisters = vixl::kNumberOfRegisters - 1;
  static const int kNumberOfAllocatableFPRegisters = vixl::kNumberOfFPRegisters;
  static constexpr int kNumberOfAllocatableRegisterPairs = 0;

  void DumpCoreRegister(std::ostream& stream, int reg) const OVERRIDE;
  void DumpFloatingPointRegister(std::ostream& stream, int reg) const OVERRIDE;

  InstructionSet GetInstructionSet() const OVERRIDE {
    return InstructionSet::kArm64;
  }

  const Arm64InstructionSetFeatures& GetInstructionSetFeatures() const {
    return isa_features_;
  }

  void Initialize() OVERRIDE {
    HGraph* graph = GetGraph();
    int length = graph->GetBlocks().Size();
    block_labels_ = graph->GetArena()->AllocArray<vixl::Label>(length);
    for (int i = 0; i < length; ++i) {
      new(block_labels_ + i) vixl::Label();
    }
  }

  void Finalize(CodeAllocator* allocator) OVERRIDE;

  // Code generation helpers.
  void MoveConstant(vixl::CPURegister destination, HConstant* constant);
  // The type is optional. When specified it must be coherent with the
  // locations, and is used for optimisation and debugging.
  void MoveLocation(Location destination, Location source,
                    Primitive::Type type = Primitive::kPrimVoid);
  void Load(Primitive::Type type, vixl::CPURegister dst, const vixl::MemOperand& src);
  void Store(Primitive::Type type, vixl::CPURegister rt, const vixl::MemOperand& dst);
  void LoadCurrentMethod(vixl::Register current_method);
  void LoadAcquire(HInstruction* instruction, vixl::CPURegister dst, const vixl::MemOperand& src);
  void StoreRelease(Primitive::Type type, vixl::CPURegister rt, const vixl::MemOperand& dst);

  // Generate code to invoke a runtime entry point.
  void InvokeRuntime(int32_t offset,
                     HInstruction* instruction,
                     uint32_t dex_pc,
                     SlowPathCode* slow_path);

  ParallelMoveResolverARM64* GetMoveResolver() { return &move_resolver_; }

  bool NeedsTwoRegisters(Primitive::Type type ATTRIBUTE_UNUSED) const OVERRIDE {
    return false;
  }

  void GenerateStaticOrDirectCall(HInvokeStaticOrDirect* invoke, vixl::Register temp);

 private:
  // Labels for each block that will be compiled.
  vixl::Label* block_labels_;
  vixl::Label frame_entry_label_;

  LocationsBuilderARM64 location_builder_;
  InstructionCodeGeneratorARM64 instruction_visitor_;
  ParallelMoveResolverARM64 move_resolver_;
  Arm64Assembler assembler_;
#ifdef MTK_ART_COMMON
  Arm64AsmWrapper asm_wrapper_;
#endif
  const Arm64InstructionSetFeatures& isa_features_;

  DISALLOW_COPY_AND_ASSIGN(CodeGeneratorARM64);
};

inline Arm64Assembler* ParallelMoveResolverARM64::GetAssembler() const {
  return codegen_->GetAssembler();
}
#ifdef MTK_ART_COMMON
inline Arm64AsmWrapper* InstructionCodeGeneratorARM64::GetArm64AsmWrapper() {
  return down_cast<CodeGeneratorARM64*>(codegen_)->GetArm64AsmWrapper();
}
#endif

}  // namespace arm64
}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_CODE_GENERATOR_ARM64_H_
