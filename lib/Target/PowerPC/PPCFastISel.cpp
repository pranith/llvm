//===-- PPCFastISel.cpp - PowerPC FastISel implementation -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the PowerPC-specific support for the FastISel class. Some
// of the target-specific code is generated by tablegen in the file
// PPCGenFastISel.inc, which is #included here.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "ppcfastisel"
#include "PPC.h"
#include "PPCISelLowering.h"
#include "PPCSubtarget.h"
#include "PPCTargetMachine.h"
#include "MCTargetDesc/PPCPredicates.h"
#include "llvm/ADT/Optional.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/FastISel.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "llvm/Target/TargetLowering.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

namespace {

typedef struct Address {
  enum {
    RegBase,
    FrameIndexBase
  } BaseType;

  union {
    unsigned Reg;
    int FI;
  } Base;

  int Offset;

  // Innocuous defaults for our address.
  Address()
   : BaseType(RegBase), Offset(0) {
     Base.Reg = 0;
   }
} Address;

class PPCFastISel : public FastISel {

  const TargetMachine &TM;
  const TargetInstrInfo &TII;
  const TargetLowering &TLI;
  const PPCSubtarget &PPCSubTarget;
  LLVMContext *Context;

  public:
    explicit PPCFastISel(FunctionLoweringInfo &FuncInfo,
                         const TargetLibraryInfo *LibInfo)
    : FastISel(FuncInfo, LibInfo),
      TM(FuncInfo.MF->getTarget()),
      TII(*TM.getInstrInfo()),
      TLI(*TM.getTargetLowering()),
      PPCSubTarget(
       *((static_cast<const PPCTargetMachine *>(&TM))->getSubtargetImpl())
      ),
      Context(&FuncInfo.Fn->getContext()) { }

  // Backend specific FastISel code.
  private:
    virtual bool TargetSelectInstruction(const Instruction *I);
    virtual unsigned TargetMaterializeConstant(const Constant *C);
    virtual unsigned TargetMaterializeAlloca(const AllocaInst *AI);
    virtual bool tryToFoldLoadIntoMI(MachineInstr *MI, unsigned OpNo,
                                     const LoadInst *LI);
    virtual bool FastLowerArguments();
    virtual unsigned FastEmit_i(MVT Ty, MVT RetTy, unsigned Opc, uint64_t Imm);

  // Instruction selection routines.
  private:
    bool SelectBranch(const Instruction *I);
    bool SelectIndirectBr(const Instruction *I);

  // Utility routines.
  private:
    bool PPCEmitCmp(const Value *Src1Value, const Value *Src2Value,
                    bool isZExt, unsigned DestReg);
    bool PPCEmitIntExt(MVT SrcVT, unsigned SrcReg, MVT DestVT,
                           unsigned DestReg, bool IsZExt);
    unsigned PPCMaterializeFP(const ConstantFP *CFP, MVT VT);
    unsigned PPCMaterializeInt(const Constant *C, MVT VT);
    unsigned PPCMaterialize32BitInt(int64_t Imm,
                                    const TargetRegisterClass *RC);
    unsigned PPCMaterialize64BitInt(int64_t Imm,
                                    const TargetRegisterClass *RC);

  private:
  #include "PPCGenFastISel.inc"

};

} // end anonymous namespace

static Optional<PPC::Predicate> getComparePred(CmpInst::Predicate Pred) {
  switch (Pred) {
    // These are not representable with any single compare.
    case CmpInst::FCMP_FALSE:
    case CmpInst::FCMP_UEQ:
    case CmpInst::FCMP_UGT:
    case CmpInst::FCMP_UGE:
    case CmpInst::FCMP_ULT:
    case CmpInst::FCMP_ULE:
    case CmpInst::FCMP_UNE:
    case CmpInst::FCMP_TRUE:
    default:
      return Optional<PPC::Predicate>();

    case CmpInst::FCMP_OEQ:
    case CmpInst::ICMP_EQ:
      return PPC::PRED_EQ;

    case CmpInst::FCMP_OGT:
    case CmpInst::ICMP_UGT:
    case CmpInst::ICMP_SGT:
      return PPC::PRED_GT;

    case CmpInst::FCMP_OGE:
    case CmpInst::ICMP_UGE:
    case CmpInst::ICMP_SGE:
      return PPC::PRED_GE;

    case CmpInst::FCMP_OLT:
    case CmpInst::ICMP_ULT:
    case CmpInst::ICMP_SLT:
      return PPC::PRED_LT;

    case CmpInst::FCMP_OLE:
    case CmpInst::ICMP_ULE:
    case CmpInst::ICMP_SLE:
      return PPC::PRED_LE;

    case CmpInst::FCMP_ONE:
    case CmpInst::ICMP_NE:
      return PPC::PRED_NE;

    case CmpInst::FCMP_ORD:
      return PPC::PRED_NU;

    case CmpInst::FCMP_UNO:
      return PPC::PRED_UN;
  }
}

// Attempt to fast-select a branch instruction.
bool PPCFastISel::SelectBranch(const Instruction *I) {
  const BranchInst *BI = cast<BranchInst>(I);
  MachineBasicBlock *BrBB = FuncInfo.MBB;
  MachineBasicBlock *TBB = FuncInfo.MBBMap[BI->getSuccessor(0)];
  MachineBasicBlock *FBB = FuncInfo.MBBMap[BI->getSuccessor(1)];

  // For now, just try the simplest case where it's fed by a compare.
  if (const CmpInst *CI = dyn_cast<CmpInst>(BI->getCondition())) {
    Optional<PPC::Predicate> OptPPCPred = getComparePred(CI->getPredicate());
    if (!OptPPCPred)
      return false;

    PPC::Predicate PPCPred = OptPPCPred.getValue();

    // Take advantage of fall-through opportunities.
    if (FuncInfo.MBB->isLayoutSuccessor(TBB)) {
      std::swap(TBB, FBB);
      PPCPred = PPC::InvertPredicate(PPCPred);
    }

    unsigned CondReg = createResultReg(&PPC::CRRCRegClass);

    if (!PPCEmitCmp(CI->getOperand(0), CI->getOperand(1), CI->isUnsigned(),
                    CondReg))
      return false;

    BuildMI(*BrBB, FuncInfo.InsertPt, DL, TII.get(PPC::BCC))
      .addImm(PPCPred).addReg(CondReg).addMBB(TBB);
    FastEmitBranch(FBB, DL);
    FuncInfo.MBB->addSuccessor(TBB);
    return true;

  } else if (const ConstantInt *CI =
             dyn_cast<ConstantInt>(BI->getCondition())) {
    uint64_t Imm = CI->getZExtValue();
    MachineBasicBlock *Target = (Imm == 0) ? FBB : TBB;
    FastEmitBranch(Target, DL);
    return true;
  }

  // FIXME: ARM looks for a case where the block containing the compare
  // has been split from the block containing the branch.  If this happens,
  // there is a vreg available containing the result of the compare.  I'm
  // not sure we can do much, as we've lost the predicate information with
  // the compare instruction -- we have a 4-bit CR but don't know which bit
  // to test here.
  return false;
}

// Attempt to emit a compare of the two source values.  Signed and unsigned
// comparisons are supported.  Return false if we can't handle it.
bool PPCFastISel::PPCEmitCmp(const Value *SrcValue1, const Value *SrcValue2,
                             bool IsZExt, unsigned DestReg) {
  Type *Ty = SrcValue1->getType();
  EVT SrcEVT = TLI.getValueType(Ty, true);
  if (!SrcEVT.isSimple())
    return false;
  MVT SrcVT = SrcEVT.getSimpleVT();

  // See if operand 2 is an immediate encodeable in the compare.
  // FIXME: Operands are not in canonical order at -O0, so an immediate
  // operand in position 1 is a lost opportunity for now.  We are
  // similar to ARM in this regard.
  long Imm = 0;
  bool UseImm = false;

  // Only 16-bit integer constants can be represented in compares for 
  // PowerPC.  Others will be materialized into a register.
  if (const ConstantInt *ConstInt = dyn_cast<ConstantInt>(SrcValue2)) {
    if (SrcVT == MVT::i64 || SrcVT == MVT::i32 || SrcVT == MVT::i16 ||
        SrcVT == MVT::i8 || SrcVT == MVT::i1) {
      const APInt &CIVal = ConstInt->getValue();
      Imm = (IsZExt) ? (long)CIVal.getZExtValue() : (long)CIVal.getSExtValue();
      if ((IsZExt && isUInt<16>(Imm)) || (!IsZExt && isInt<16>(Imm)))
        UseImm = true;
    }
  }

  unsigned CmpOpc;
  bool NeedsExt = false;
  switch (SrcVT.SimpleTy) {
    default: return false;
    case MVT::f32:
      CmpOpc = PPC::FCMPUS;
      break;
    case MVT::f64:
      CmpOpc = PPC::FCMPUD;
      break;
    case MVT::i1:
    case MVT::i8:
    case MVT::i16:
      NeedsExt = true;
      // Intentional fall-through.
    case MVT::i32:
      if (!UseImm)
        CmpOpc = IsZExt ? PPC::CMPLW : PPC::CMPW;
      else
        CmpOpc = IsZExt ? PPC::CMPLWI : PPC::CMPWI;
      break;
    case MVT::i64:
      if (!UseImm)
        CmpOpc = IsZExt ? PPC::CMPLD : PPC::CMPD;
      else
        CmpOpc = IsZExt ? PPC::CMPLDI : PPC::CMPDI;
      break;
  }

  unsigned SrcReg1 = getRegForValue(SrcValue1);
  if (SrcReg1 == 0)
    return false;

  unsigned SrcReg2 = 0;
  if (!UseImm) {
    SrcReg2 = getRegForValue(SrcValue2);
    if (SrcReg2 == 0)
      return false;
  }

  if (NeedsExt) {
    unsigned ExtReg = createResultReg(&PPC::GPRCRegClass);
    if (!PPCEmitIntExt(SrcVT, SrcReg1, MVT::i32, ExtReg, IsZExt))
      return false;
    SrcReg1 = ExtReg;

    if (!UseImm) {
      unsigned ExtReg = createResultReg(&PPC::GPRCRegClass);
      if (!PPCEmitIntExt(SrcVT, SrcReg2, MVT::i32, ExtReg, IsZExt))
        return false;
      SrcReg2 = ExtReg;
    }
  }

  if (!UseImm)
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(CmpOpc), DestReg)
      .addReg(SrcReg1).addReg(SrcReg2);
  else
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(CmpOpc), DestReg)
      .addReg(SrcReg1).addImm(Imm);

  return true;
}

// Attempt to emit an integer extend of SrcReg into DestReg.  Both
// signed and zero extensions are supported.  Return false if we
// can't handle it.  Not yet implemented.
bool PPCFastISel::PPCEmitIntExt(MVT SrcVT, unsigned SrcReg, MVT DestVT,
                                unsigned DestReg, bool IsZExt) {
  return (SrcVT == MVT::i8 && SrcReg && DestVT == MVT::i8 && DestReg
          && IsZExt && false);
}

// Attempt to fast-select an indirect branch instruction.
bool PPCFastISel::SelectIndirectBr(const Instruction *I) {
  unsigned AddrReg = getRegForValue(I->getOperand(0));
  if (AddrReg == 0)
    return false;

  BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(PPC::MTCTR8))
    .addReg(AddrReg);
  BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(PPC::BCTR8));

  const IndirectBrInst *IB = cast<IndirectBrInst>(I);
  for (unsigned i = 0, e = IB->getNumSuccessors(); i != e; ++i)
    FuncInfo.MBB->addSuccessor(FuncInfo.MBBMap[IB->getSuccessor(i)]);

  return true;
}

// Attempt to fast-select an instruction that wasn't handled by
// the table-generated machinery.
bool PPCFastISel::TargetSelectInstruction(const Instruction *I) {

  switch (I->getOpcode()) {
    case Instruction::Br:
      return SelectBranch(I);
    case Instruction::IndirectBr:
      return SelectIndirectBr(I);
    // Here add other flavors of Instruction::XXX that automated
    // cases don't catch.  For example, switches are terminators
    // that aren't yet handled.
    default:
      break;
  }
  return false;
}

// Materialize a floating-point constant into a register, and return
// the register number (or zero if we failed to handle it).
unsigned PPCFastISel::PPCMaterializeFP(const ConstantFP *CFP, MVT VT) {
  // No plans to handle long double here.
  if (VT != MVT::f32 && VT != MVT::f64)
    return 0;

  // All FP constants are loaded from the constant pool.
  unsigned Align = TD.getPrefTypeAlignment(CFP->getType());
  assert(Align > 0 && "Unexpectedly missing alignment information!");
  unsigned Idx = MCP.getConstantPoolIndex(cast<Constant>(CFP), Align);
  unsigned DestReg = createResultReg(TLI.getRegClassFor(VT));
  CodeModel::Model CModel = TM.getCodeModel();

  MachineMemOperand *MMO =
    FuncInfo.MF->getMachineMemOperand(
      MachinePointerInfo::getConstantPool(), MachineMemOperand::MOLoad,
      (VT == MVT::f32) ? 4 : 8, Align);

  unsigned Opc = (VT == MVT::f32) ? PPC::LFS : PPC::LFD;
  unsigned TmpReg = createResultReg(&PPC::G8RC_and_G8RC_NOX0RegClass);

  // For small code model, generate a LF[SD](0, LDtocCPT(Idx, X2)).
  if (CModel == CodeModel::Small || CModel == CodeModel::JITDefault) {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(PPC::LDtocCPT),
            TmpReg)
      .addConstantPoolIndex(Idx).addReg(PPC::X2);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(Opc), DestReg)
      .addImm(0).addReg(TmpReg).addMemOperand(MMO);
  } else {
    // Otherwise we generate LF[SD](Idx[lo], ADDIStocHA(X2, Idx)).
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(PPC::ADDIStocHA),
            TmpReg).addReg(PPC::X2).addConstantPoolIndex(Idx);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(Opc), DestReg)
      .addConstantPoolIndex(Idx, 0, PPCII::MO_TOC_LO)
      .addReg(TmpReg)
      .addMemOperand(MMO);
  }

  return DestReg;
}

// Materialize a 32-bit integer constant into a register, and return
// the register number (or zero if we failed to handle it).
unsigned PPCFastISel::PPCMaterialize32BitInt(int64_t Imm,
                                             const TargetRegisterClass *RC) {
  unsigned Lo = Imm & 0xFFFF;
  unsigned Hi = (Imm >> 16) & 0xFFFF;

  unsigned ResultReg = createResultReg(RC);
  bool IsGPRC = RC->hasSuperClassEq(&PPC::GPRCRegClass);

  if (isInt<16>(Imm))
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
            TII.get(IsGPRC ? PPC::LI : PPC::LI8), ResultReg)
      .addImm(Imm);
  else if (Lo) {
    // Both Lo and Hi have nonzero bits.
    unsigned TmpReg = createResultReg(RC);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
            TII.get(IsGPRC ? PPC::LIS : PPC::LIS8), TmpReg)
      .addImm(Hi);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
            TII.get(IsGPRC ? PPC::ORI : PPC::ORI8), ResultReg)
      .addReg(TmpReg).addImm(Lo);
  } else
    // Just Hi bits.
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL,
            TII.get(IsGPRC ? PPC::LIS : PPC::LIS8), ResultReg)
      .addImm(Hi);
  
  return ResultReg;
}

// Materialize a 64-bit integer constant into a register, and return
// the register number (or zero if we failed to handle it).
unsigned PPCFastISel::PPCMaterialize64BitInt(int64_t Imm,
                                             const TargetRegisterClass *RC) {
  unsigned Remainder = 0;
  unsigned Shift = 0;

  // If the value doesn't fit in 32 bits, see if we can shift it
  // so that it fits in 32 bits.
  if (!isInt<32>(Imm)) {
    Shift = countTrailingZeros<uint64_t>(Imm);
    int64_t ImmSh = static_cast<uint64_t>(Imm) >> Shift;

    if (isInt<32>(ImmSh))
      Imm = ImmSh;
    else {
      Remainder = Imm;
      Shift = 32;
      Imm >>= 32;
    }
  }

  // Handle the high-order 32 bits (if shifted) or the whole 32 bits
  // (if not shifted).
  unsigned TmpReg1 = PPCMaterialize32BitInt(Imm, RC);
  if (!Shift)
    return TmpReg1;

  // If upper 32 bits were not zero, we've built them and need to shift
  // them into place.
  unsigned TmpReg2;
  if (Imm) {
    TmpReg2 = createResultReg(RC);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(PPC::RLDICR),
            TmpReg2).addReg(TmpReg1).addImm(Shift).addImm(63 - Shift);
  } else
    TmpReg2 = TmpReg1;

  unsigned TmpReg3, Hi, Lo;
  if ((Hi = (Remainder >> 16) & 0xFFFF)) {
    TmpReg3 = createResultReg(RC);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(PPC::ORIS8),
            TmpReg3).addReg(TmpReg2).addImm(Hi);
  } else
    TmpReg3 = TmpReg2;

  if ((Lo = Remainder & 0xFFFF)) {
    unsigned ResultReg = createResultReg(RC);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(PPC::ORI8),
            ResultReg).addReg(TmpReg3).addImm(Lo);
    return ResultReg;
  }

  return TmpReg3;
}


// Materialize an integer constant into a register, and return
// the register number (or zero if we failed to handle it).
unsigned PPCFastISel::PPCMaterializeInt(const Constant *C, MVT VT) {

  if (VT != MVT::i64 && VT != MVT::i32 && VT != MVT::i16 &&
      VT != MVT::i8 && VT != MVT::i1) 
    return 0;

  const TargetRegisterClass *RC = ((VT == MVT::i64) ? &PPC::G8RCRegClass :
                                   &PPC::GPRCRegClass);

  // If the constant is in range, use a load-immediate.
  const ConstantInt *CI = cast<ConstantInt>(C);
  if (isInt<16>(CI->getSExtValue())) {
    unsigned Opc = (VT == MVT::i64) ? PPC::LI8 : PPC::LI;
    unsigned ImmReg = createResultReg(RC);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DL, TII.get(Opc), ImmReg)
      .addImm(CI->getSExtValue());
    return ImmReg;
  }

  // Construct the constant piecewise.
  int64_t Imm = CI->getZExtValue();

  if (VT == MVT::i64)
    return PPCMaterialize64BitInt(Imm, RC);
  else if (VT == MVT::i32)
    return PPCMaterialize32BitInt(Imm, RC);

  return 0;
}

// Materialize a constant into a register, and return the register
// number (or zero if we failed to handle it).
unsigned PPCFastISel::TargetMaterializeConstant(const Constant *C) {
  EVT CEVT = TLI.getValueType(C->getType(), true);

  // Only handle simple types.
  if (!CEVT.isSimple()) return 0;
  MVT VT = CEVT.getSimpleVT();

  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(C))
    return PPCMaterializeFP(CFP, VT);
  else if (isa<ConstantInt>(C))
    return PPCMaterializeInt(C, VT);
  // TBD: Global values.

  return 0;
}

// Materialize the address created by an alloca into a register, and
// return the register number (or zero if we failed to handle it).  TBD.
unsigned PPCFastISel::TargetMaterializeAlloca(const AllocaInst *AI) {
  return AI && 0;
}

// Fold loads into extends when possible.  TBD.
bool PPCFastISel::tryToFoldLoadIntoMI(MachineInstr *MI, unsigned OpNo,
                                      const LoadInst *LI) {
  return MI && OpNo && LI && false;
}

// Attempt to lower call arguments in a faster way than done by
// the selection DAG code.
bool PPCFastISel::FastLowerArguments() {
  // Defer to normal argument lowering for now.  It's reasonably
  // efficient.  Consider doing something like ARM to handle the
  // case where all args fit in registers, no varargs, no float
  // or vector args.
  return false;
}

// Handle materializing integer constants into a register.  This is not
// automatically generated for PowerPC, so must be explicitly created here.
unsigned PPCFastISel::FastEmit_i(MVT Ty, MVT VT, unsigned Opc, uint64_t Imm) {
  
  if (Opc != ISD::Constant)
    return 0;

  if (VT != MVT::i64 && VT != MVT::i32 && VT != MVT::i16 &&
      VT != MVT::i8 && VT != MVT::i1) 
    return 0;

  const TargetRegisterClass *RC = ((VT == MVT::i64) ? &PPC::G8RCRegClass :
                                   &PPC::GPRCRegClass);
  if (VT == MVT::i64)
    return PPCMaterialize64BitInt(Imm, RC);
  else
    return PPCMaterialize32BitInt(Imm, RC);
}

namespace llvm {
  // Create the fast instruction selector for PowerPC64 ELF.
  FastISel *PPC::createFastISel(FunctionLoweringInfo &FuncInfo,
                                const TargetLibraryInfo *LibInfo) {
    const TargetMachine &TM = FuncInfo.MF->getTarget();

    // Only available on 64-bit ELF for now.
    const PPCSubtarget *Subtarget = &TM.getSubtarget<PPCSubtarget>();
    if (Subtarget->isPPC64() && Subtarget->isSVR4ABI())
      return new PPCFastISel(FuncInfo, LibInfo);

    return 0;
  }
}
