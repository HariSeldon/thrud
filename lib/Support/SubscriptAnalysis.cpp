#include "thrud/Support/SubscriptAnalysis.h"

#include "llvm/IR/Instructions.h"

#include "llvm/Support/raw_ostream.h"

//------------------------------------------------------------------------------
int GetThreadStride(Value *value, ScalarEvolution *SE, ValueVector& TIds) {
//  llvm::errs() << "================ GetThreadStride ==================\n"; 

  if (!isa<GetElementPtrInst>(value)) {
    return 0;
  }

  if (!SE->isSCEVable(value->getType())) {
    return -1;
  }

//  if(Instruction *I = dyn_cast<Instruction>(value)) {
//    I->dump();
//    I->getParent()->dump();
//  }

  const SCEV *scev = SE->getSCEV(value);
  SmallPtrSet<const SCEV *, 8> Processed;
  int result = AnalyzeSubscript(SE, scev, TIds, Processed);
  return result;
}

//------------------------------------------------------------------------------
// WARNING: SCEV does not support %.
int AnalyzeSubscript(ScalarEvolution *SE, const SCEV *Scev, ValueVector &TIds,
                     SmallPtrSet<const SCEV *, 8> &Processed) {
//  Scev->dump();

  if (!Processed.insert(Scev))
    return false;

  APInt One = APInt(32, 1);
  APInt Two = APInt(32, 2);
  const SCEV *first = ReplaceInExpr(SE, Scev, TIds, One);
  const SCEV *second = ReplaceInExpr(SE, Scev, TIds, Two);
  const SCEV* result = SE->getMinusSCEV(second, first);
  
  if(result == NULL) {
    return -1;
  }

  int numericResult = -1;
  if(const SCEVConstant *ConstSCEV = dyn_cast<SCEVConstant>(result)) {
    const ConstantInt *value = ConstSCEV->getValue();
    numericResult = (int)value->getValue().roundToDouble();
  }

  return numericResult;
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
  if(const SCEVAddRecExpr* tmp = dyn_cast<SCEVAddRecExpr>(Expr)) 
    return ReplaceInExpr(SE, tmp, TIds, value);
  return NULL;
}

//------------------------------------------------------------------------------
const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEVAddRecExpr *Expr,
                          ValueVector &TIds, const APInt &value) {
  const SCEV *start = Expr->getStart();
  // const SCEV* step = addRecExpr->getStepRecurrence(*SE);
  // Check that the step is independent of the TID. TODO.
  return ReplaceInExpr(SE, start, TIds, value);
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
//  return 0;
//  for (PHINode::const_op_iterator I = Phi->op_begin(), E = Phi->op_end();
//       I != E; ++I) {
//    Value *V = I->get();
//    const SCEV *Scev = SE->getSCEV(V);
//    int result = AnalyzeSubscript(SE, Scev, TIds, Processed);
//  }
}
