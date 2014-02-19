#include "thrud/Support/SubscriptAnalysis.h"

#include "llvm/IR/Instructions.h"

#include "llvm/Support/raw_ostream.h"

//------------------------------------------------------------------------------
int GetThreadStride(Value *value, ScalarEvolution *SE, ValueVector& TIds) {
  llvm::errs() << "=============== GetThreadStride ====================\n";
  if (!isa<GetElementPtrInst>(value)) {
    return 0;
  }

  if (!SE->isSCEVable(value->getType())) {
    return -1;
  }

  const SCEV *scev = SE->getSCEV(value);
  SmallPtrSet<const SCEV *, 8> Processed;
  int result = AnalyzeSubscript(SE, scev, TIds, Processed);
  return result;
}

//------------------------------------------------------------------------------
// FIXME: for multiplications this goes down just one level.
// WARNING: SCEV does not support %.
int AnalyzeSubscript(ScalarEvolution *SE, const SCEV *Scev, ValueVector &TIds,
                     SmallPtrSet<const SCEV *, 8> &Processed) {
  if (!Processed.insert(Scev))
    return false;

//  Scev->dump();
  const SCEV *result = NULL;

  return 1;

//  switch (Scev->getSCEVType()) {
//  case scAddRecExpr: {
//    const SCEVAddRecExpr *addRecExpr = cast<SCEVAddRecExpr>(Scev);
//    const SCEV *start = addRecExpr->getStart();
//    // const SCEV* step = addRecExpr->getStepRecurrence(*SE);
//    // Check that the step is independent of the TID. TODO.
//    return AnalyzeSubscript(SE, start, TIds, Processed);
//  }
//  case scAddExpr: {
//    const SCEVAddExpr *AddExpr = cast<SCEVAddExpr>(Scev);
//    APInt One = APInt(32, 1);
//    APInt Two = APInt(32, 2);
//    const SCEV *first = ReplaceInAdd(SE, AddExpr, TIds, One);
//    const SCEV *second = ReplaceInAdd(SE, AddExpr, TIds, Two);
//    result = SE->getMinusSCEV(second, first);
//    break;
//  }
//  case scMulExpr: {
//    const SCEVMulExpr *MulExpr = cast<SCEVMulExpr>(Scev);
//    APInt One = APInt(32, 1);
//    APInt Two = APInt(32, 2);
//    const SCEV *first = ReplaceInMul(SE, MulExpr, TIds, One);
//    const SCEV *second = ReplaceInMul(SE, MulExpr, TIds, Two);
//    result = SE->getMinusSCEV(second, first);
//    break;
//  }
//  case scUnknown: {
//    const SCEVUnknown *U = cast<SCEVUnknown>(Scev);
//    Value *UV = U->getValue();
//    if (IsPresent<Value>(UV, TIds))
//      return 0;
//    else {
//      // If it is a phinode look inside it.
//      if (PHINode *Phi = dyn_cast<PHINode>(UV))
//        return AnalyzePHI(SE, Phi, TIds, Processed);
//      else
//        result = NULL;
//    }
//    break;
//  }
//  default: {
//    result = NULL;
//    break;
//  }
//  }
//
//  if(result == NULL) {
//    return -1;
//  }
//
//  int numericResult = -1;
//  if(const SCEVConstant *ConstSCEV = dyn_cast<SCEVConstant>(result)) {
//    const ConstantInt *value = ConstSCEV->getValue();
//    numericResult = (int)value->getValue().roundToDouble();
//  }
//
//  return numericResult;
}

//------------------------------------------------------------------------------
const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEV *Expr,
                          ValueVector &TIds, const APInt &value) {
  // FIXME: This is ugly.
  if(const SCEVCommutativeExpr* tmp = dyn_cast<SCEVCommutativeExpr>(Expr)) 
    return ReplaceInExpr(SE, tmp, TIds, value);
  if(const SCEVConstant* tmp = dyn_cast<SCEVConstant>(Expr)) 
    return ReplaceInExpr(SE, tmp, TIds, value);
  if(const SCEVUnknown* tmp = dyn_cast<SCEVUnknown>(Expr)) 
    return ReplaceInExpr(SE, tmp, TIds, value);
  if(const SCEVUDivExpr* tmp = dyn_cast<SCEVUDivExpr>(Expr)) 
    return ReplaceInExpr(SE, tmp, TIds, value);
//  if(SCEVExpr* tmp = dyn_cast<SCEVExpr>(Expr)) 
//    return ReplaceInExpr(SE, tmp, TIds, value);
//  if(SCEVExpr* tmp = dyn_cast<SCEVExpr>(Expr)) 
//    return ReplaceInExpr(SE, tmp, TIds, value);
}

//------------------------------------------------------------------------------
const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEVCommutativeExpr *Expr,
                          ValueVector &TIds, const APInt &value) {
   
  SmallVector<const SCEV *, 8> Operands;
  for (SCEVNAryExpr::op_iterator I = Expr->op_begin(), E = Expr->op_end();
       I != E; ++I) {
    const SCEV *NewOperand = ReplaceInExpr(SE, *I, TIds, value); 
    Operands.push_back(NewOperand);
  }
  const SCEV *result = NULL;
  
  if(isa<SCEVAddExpr>(Expr)) 
    result = SE->getAddExpr(Operands);
  if(isa<SCEVMulExpr>(Expr)) 
    result = SE->getMulExpr(Operands); 
  if(isa<SCEVSMaxExpr>(Expr)) 
    result = SE->getSMaxExpr(Operands); 
  if(isa<SCEVUMaxExpr>(Expr)) 
    result = SE->getUMaxExpr(Operands); 

  return(result);
}

//------------------------------------------------------------------------------
const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEVConstant *Expr,
                          ValueVector &TIds, const APInt &value) {
  return Expr; 
}

//------------------------------------------------------------------------------
const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEVUnknown *Expr,
                          ValueVector &TIds, const APInt &value) {
  Value* V = Expr->getValue();
  if (IsPresent<Value>(V, TIds)) {
    return SE->getConstant(value);
  } else {
    return Expr;
  }
}

//------------------------------------------------------------------------------
const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEVUDivExpr *Expr,
                          ValueVector &TIds, const APInt &value) {
  const SCEV* newLHS = ReplaceInExpr(SE, Expr->getLHS(), TIds, value);
  const SCEV* newRHS = ReplaceInExpr(SE, Expr->getRHS(), TIds, value);

  return SE->getUDivExpr(newLHS, newRHS); 
}


//------------------------------------------------------------------------------
int AnalyzePHI(ScalarEvolution *SE, PHINode *Phi, ValueVector &TIds,
               SmallPtrSet<const SCEV *, 8> &Processed) {
  llvm::errs() << "AnalyzePHI\n";
  exit(1);
  return 0;
  //  int sum = 0;
  //  int absSum = 0;
  //  for (PHINode::const_op_iterator I = Phi->op_begin(), E = Phi->op_end();
  //       I != E; ++I) {
  //    Value *V = I->get();
  //    const SCEV *Scev = SE->getSCEV(V);
  //    int result = AnalyzeSubscript(SE, Scev, TIds, Processed);
  //    sum += result;
  //    absSum += abs(result);
  //  }
  //
  //  if (absSum > 1)
  //    return 0;
  //  return sum;
}

