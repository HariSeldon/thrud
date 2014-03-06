#include "thrud/Support/SubscriptAnalysis.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#include "llvm/Support/raw_ostream.h"

#include "thrud/Support/NDRange.h"

SubscriptAnalysis::SubscriptAnalysis(ScalarEvolution *SE, NDRange *NDR,
                                     unsigned int Dir)
    : SE(SE), NDR(NDR), Dir(Dir) {}

//------------------------------------------------------------------------------
int SubscriptAnalysis::GetThreadStride(Value *value) {
  //  llvm::errs() << "================ GetThreadStride ==================\n";
  //  dumpVector(TIds);

  if (!isa<GetElementPtrInst>(value)) {
    return 0;
  }

  if (!SE->isSCEVable(value->getType())) {
    return -1;
  }

  if (Instruction *I = dyn_cast<Instruction>(value)) {
    I->dump();
    I->getParent()->getParent()->dump();
  }

  const SCEV *scev = SE->getSCEV(value);
  int result = AnalyzeSubscript(scev);
  llvm::errs() << "Result: " << result << "\n";
  return result;
}

//------------------------------------------------------------------------------
// WARNING: SCEV does not support %.
int SubscriptAnalysis::AnalyzeSubscript(const SCEV *Scev) {
  SmallPtrSet<const SCEV *, 8> Processed1;
  SmallPtrSet<const SCEV *, 8> Processed2;
  APInt Zero = APInt(32, 0);
  APInt One = APInt(32, 1);
  APInt Two = APInt(32, 2);
  const SCEV *first = ReplaceInExpr(Scev, Zero, Zero, Zero, Processed1);
  llvm::errs() << "Result1: ";
  first->dump();
  const SCEV *second = ReplaceInExpr(Scev, One, One, Zero, Processed2);
  llvm::errs() << "Result2: ";
  second->dump();
  const SCEV *result = SE->getMinusSCEV(second, first);

  if (result == NULL) {
    return -1;
  }

  //  llvm::errs() << "Difference: ";
  //  result->dump();

  int numericResult = -1;
  if (const SCEVAddRecExpr *AddRecSCEV = dyn_cast<SCEVAddRecExpr>(result)) {
    result = AddRecSCEV->getStart();
  }

  if (const SCEVConstant *ConstSCEV = dyn_cast<SCEVConstant>(result)) {
    const ConstantInt *value = ConstSCEV->getValue();
    numericResult = (int)value->getValue().roundToDouble();
  }

  return numericResult;
}

//------------------------------------------------------------------------------
const SCEV *SubscriptAnalysis::ReplaceInExpr(
    const SCEV *Expr, const APInt &globalValue, const APInt &localValue,
    const APInt &groupValue, SmallPtrSet<const SCEV *, 8> &Processed) {

  llvm::errs() << "Replace In Expr: ";
  Expr->dump();

  if (!Processed.insert(Expr))
    return Expr;

  // FIXME: This is ugly.
  if (const SCEVCommutativeExpr *tmp = dyn_cast<SCEVCommutativeExpr>(Expr))
    return ReplaceInExpr(tmp, globalValue, localValue, groupValue, Processed);
  if (const SCEVConstant *tmp = dyn_cast<SCEVConstant>(Expr))
    return ReplaceInExpr(tmp, globalValue, localValue, groupValue, Processed);
  if (const SCEVUnknown *tmp = dyn_cast<SCEVUnknown>(Expr))
    return ReplaceInExpr(tmp, globalValue, localValue, groupValue, Processed);
  if (const SCEVUDivExpr *tmp = dyn_cast<SCEVUDivExpr>(Expr))
    return ReplaceInExpr(tmp, globalValue, localValue, groupValue, Processed);
  if (const SCEVAddRecExpr *tmp = dyn_cast<SCEVAddRecExpr>(Expr))
    return ReplaceInExpr(tmp, globalValue, localValue, groupValue, Processed);
  llvm::errs() << "NULL!\n";
  return NULL;
}

//------------------------------------------------------------------------------
const SCEV *SubscriptAnalysis::ReplaceInExpr(
    const SCEVAddRecExpr *Expr, const APInt &globalValue,
    const APInt &localValue, const APInt &groupValue,
    SmallPtrSet<const SCEV *, 8> &Processed) {
  //  Expr->dump();
  const SCEV *start = Expr->getStart();
  // const SCEV* step = addRecExpr->getStepRecurrence(*SE);
  // Check that the step is independent of the TID. TODO.
  return ReplaceInExpr(start, globalValue, localValue, groupValue, Processed);
}

//------------------------------------------------------------------------------
const SCEV *SubscriptAnalysis::ReplaceInExpr(
    const SCEVCommutativeExpr *Expr, const APInt &globalValue,
    const APInt &localValue, const APInt &groupValue,
    SmallPtrSet<const SCEV *, 8> &Processed) {

  //  llvm::errs() << "SCEVCommutativeExpr: ";
  //  Expr->dump();
  SmallVector<const SCEV *, 8> Operands;
  for (SCEVNAryExpr::op_iterator I = Expr->op_begin(), E = Expr->op_end();
       I != E; ++I) {
    const SCEV *NewOperand =
        ReplaceInExpr(*I, globalValue, localValue, groupValue, Processed);
    //    llvm::errs() << "New Operand: ";
    //    NewOperand->dump();
    Operands.push_back(NewOperand);
  }
  const SCEV *result = NULL;

  if (isa<SCEVAddExpr>(Expr))
    result = SE->getAddExpr(Operands);
  if (isa<SCEVMulExpr>(Expr))
    result = SE->getMulExpr(Operands);
  if (isa<SCEVSMaxExpr>(Expr))
    result = SE->getSMaxExpr(Operands);
  if (isa<SCEVUMaxExpr>(Expr))
    result = SE->getUMaxExpr(Operands);

  //  llvm::errs() << "SCEVCommutativeExpr Result: ";
  //  result->dump();

  return (result);
}

//------------------------------------------------------------------------------
const SCEV *SubscriptAnalysis::ReplaceInExpr(
    const SCEVConstant *Expr, const APInt &globalValue, const APInt &localValue,
    const APInt &groupValue, SmallPtrSet<const SCEV *, 8> &Processed) {
  //  llvm::errs() << "SCEVConstant:";
  //  Expr->dump();
  return Expr;
}

//------------------------------------------------------------------------------
const SCEV *SubscriptAnalysis::ReplaceInExpr(
    const SCEVUnknown *Expr, const APInt &globalValue, const APInt &localValue,
    const APInt &groupValue, SmallPtrSet<const SCEV *, 8> &Processed) {
  //  llvm::errs() << "SCEVUnknown: ";
  //  Expr->dump();
  Value *V = Expr->getValue();
  // Implement proper replacement.
  if (Instruction *Inst = dyn_cast<Instruction>(V)) {
    if (NDR->IsGlobal(Inst, Dir))
      return SE->getConstant(globalValue);
    if (NDR->IsLocal(Inst, Dir))
      return SE->getConstant(localValue);
    if (NDR->IsGroupId(Inst, Dir))
      return SE->getConstant(groupValue);
  } else {
    if (PHINode *phi = dyn_cast<PHINode>(V))
      return ReplaceInPhi(phi, globalValue, localValue, groupValue,
                          Processed);
  }
  return Expr;
}

//------------------------------------------------------------------------------
const SCEV *SubscriptAnalysis::ReplaceInExpr(
    const SCEVUDivExpr *Expr, const APInt &globalValue, const APInt &localValue,
    const APInt &groupValue, SmallPtrSet<const SCEV *, 8> &Processed) {

  //  Expr->dump();
  const SCEV *newLHS = ReplaceInExpr(Expr->getLHS(), globalValue, localValue,
                                     groupValue, Processed);
  const SCEV *newRHS = ReplaceInExpr(Expr->getRHS(), globalValue, localValue,
                                     groupValue, Processed);

  return SE->getUDivExpr(newLHS, newRHS);
}

//------------------------------------------------------------------------------
const SCEV *SubscriptAnalysis::ReplaceInPhi(
    PHINode *Phi, const APInt &globalValue, const APInt &localValue,
    const APInt &groupValue, SmallPtrSet<const SCEV *, 8> &Processed) {
  //  llvm::errs() << "Phi: ";
  //  Phi->dump();
  // FIXME: Pick the first argument of the phi node.
  Value *param = Phi->getIncomingValue(0);
  //  llvm::errs() << "Param: ";
  //  param->dump();
  assert(SE->isSCEVable(param->getType()) && "PhiNode argument non-SCEVable");

  const SCEV *scev = SE->getSCEV(param);

  return ReplaceInExpr(scev, globalValue, localValue, groupValue, Processed);
}
