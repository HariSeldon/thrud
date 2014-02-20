#include "thrud/Support/SubscriptAnalysis.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#include "llvm/Support/raw_ostream.h"

//------------------------------------------------------------------------------
int GetThreadStride(Value *value, ScalarEvolution *SE, ValueVector &TIds) {
//  llvm::errs() << "================ GetThreadStride ==================\n";
//  dumpVector(TIds);

  if (!isa<GetElementPtrInst>(value)) {
    return 0;
  }

  if (!SE->isSCEVable(value->getType())) {
    return -1;
  }

//  if (Instruction *I = dyn_cast<Instruction>(value)) {
//    I->dump();
//  }

  const SCEV *scev = SE->getSCEV(value);
  int result = AnalyzeSubscript(SE, scev, TIds);
  llvm::errs() << "Result: " << result << "\n";
  return result;
}

//------------------------------------------------------------------------------
// WARNING: SCEV does not support %.
int AnalyzeSubscript(ScalarEvolution *SE, const SCEV *Scev, ValueVector &TIds) {
  SmallPtrSet<const SCEV *, 8> Processed1;
  SmallPtrSet<const SCEV *, 8> Processed2;
  APInt One = APInt(32, 1);
  APInt Two = APInt(32, 2);
  const SCEV *first = ReplaceInExpr(SE, Scev, TIds, One, Processed1);
//  llvm::errs() << "Result1: ";
//  first->dump();
  const SCEV *second = ReplaceInExpr(SE, Scev, TIds, Two, Processed2);
//  llvm::errs() << "Result2: ";
//  second->dump();
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
const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEV *Expr,
                          ValueVector &TIds, const APInt &value,
                          SmallPtrSet<const SCEV *, 8> &Processed) {

//  llvm::errs() << "Replace In Expr: ";
//  Expr->dump();

  if (!Processed.insert(Expr))
    return Expr;

  // FIXME: This is ugly.
  if (const SCEVCommutativeExpr *tmp = dyn_cast<SCEVCommutativeExpr>(Expr))
    return ReplaceInExpr(SE, tmp, TIds, value, Processed);
  if (const SCEVConstant *tmp = dyn_cast<SCEVConstant>(Expr))
    return ReplaceInExpr(SE, tmp, TIds, value, Processed);
  if (const SCEVUnknown *tmp = dyn_cast<SCEVUnknown>(Expr))
    return ReplaceInExpr(SE, tmp, TIds, value, Processed);
  if (const SCEVUDivExpr *tmp = dyn_cast<SCEVUDivExpr>(Expr))
    return ReplaceInExpr(SE, tmp, TIds, value, Processed);
  if (const SCEVAddRecExpr *tmp = dyn_cast<SCEVAddRecExpr>(Expr))
    return ReplaceInExpr(SE, tmp, TIds, value, Processed);
  llvm::errs() << "NULL!\n";
  return NULL;
}

//------------------------------------------------------------------------------
const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEVAddRecExpr *Expr,
                          ValueVector &TIds, const APInt &value,
                          SmallPtrSet<const SCEV *, 8> &Processed) {
//  Expr->dump();
  const SCEV *start = Expr->getStart();
  // const SCEV* step = addRecExpr->getStepRecurrence(*SE);
  // Check that the step is independent of the TID. TODO.
  return ReplaceInExpr(SE, start, TIds, value, Processed);
}

//------------------------------------------------------------------------------
const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEVCommutativeExpr *Expr,
                          ValueVector &TIds, const APInt &value,
                          SmallPtrSet<const SCEV *, 8> &Processed) {

//  llvm::errs() << "SCEVCommutativeExpr: ";
//  Expr->dump();
  SmallVector<const SCEV *, 8> Operands;
  for (SCEVNAryExpr::op_iterator I = Expr->op_begin(), E = Expr->op_end();
       I != E; ++I) {
    const SCEV *NewOperand = ReplaceInExpr(SE, *I, TIds, value, Processed);
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
const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEVConstant *Expr,
                          ValueVector &TIds, const APInt &value,
                          SmallPtrSet<const SCEV *, 8> &Processed) {
//  llvm::errs() << "SCEVConstant:";
//  Expr->dump();
  return Expr;
}

//------------------------------------------------------------------------------
const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEVUnknown *Expr,
                          ValueVector &TIds, const APInt &value,
                          SmallPtrSet<const SCEV *, 8> &Processed) {
//  llvm::errs() << "SCEVUnknown: ";
//  Expr->dump();
  Value *V = Expr->getValue();
  if (IsPresent<Value>(V, TIds)) {
//    llvm::errs() << "Present!\n";
    return SE->getConstant(value);
  } else {
    if (PHINode *phi = dyn_cast<PHINode>(V))
      return ReplaceInPhi(SE, phi, TIds, value, Processed);
    return Expr;
  }
}

//------------------------------------------------------------------------------
const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEVUDivExpr *Expr,
                          ValueVector &TIds, const APInt &value,
                          SmallPtrSet<const SCEV *, 8> &Processed) {

//  Expr->dump();
  const SCEV *newLHS =
      ReplaceInExpr(SE, Expr->getLHS(), TIds, value, Processed);
  const SCEV *newRHS =
      ReplaceInExpr(SE, Expr->getRHS(), TIds, value, Processed);

  return SE->getUDivExpr(newLHS, newRHS);
}

//------------------------------------------------------------------------------
const SCEV *ReplaceInPhi(ScalarEvolution *SE, PHINode *Phi, ValueVector &TIds,
                         const APInt &value,
                         SmallPtrSet<const SCEV *, 8> &Processed) {
//  llvm::errs() << "Phi: ";
//  Phi->dump();
  // FIXME: Pick the first argument of the phi node.
  Value *param = Phi->getIncomingValue(0);
//  llvm::errs() << "Param: ";
//  param->dump();
  assert(SE->isSCEVable(param->getType()) && "PhiNode argument non-SCEVable");

  const SCEV *scev = SE->getSCEV(param);

  return ReplaceInExpr(SE, scev, TIds, value, Processed);
}
