#include "thrud/Support/SubscriptAnalysis.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#include "llvm/Support/raw_ostream.h"

#include "thrud/Support/NDRange.h"
#include "thrud/Support/NDRangePoint.h"

SubscriptAnalysis::SubscriptAnalysis(ScalarEvolution *SE, NDRange *NDR,
                                     unsigned int Dir)
    : SE(SE), NDR(NDR), Dir(Dir) {}

//------------------------------------------------------------------------------
int SubscriptAnalysis::GetThreadStride(Value *value) {
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
  NDRangePoint firstPoint(0, 0, 0, 0, 0, 0);
  NDRangePoint secondPoint(1, 0, 0, 0, 0, 0);

  SmallPtrSet<const SCEV *, 8> Processed1;
  SmallPtrSet<const SCEV *, 8> Processed2;
  const SCEV *firstExp = ReplaceInExpr(Scev, firstPoint, Processed1);
  llvm::errs() << "Result1: ";
  firstExp->dump();
  const SCEV *secondExp = ReplaceInExpr(Scev, secondPoint, Processed2);
  llvm::errs() << "Result2: ";
  secondExp->dump();
  const SCEV *result = SE->getMinusSCEV(secondExp, firstExp);

  llvm::errs() << "Difference result.\n";
  result->dump();

  if (result == NULL) {
    return -1;
  }

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
const SCEV *
SubscriptAnalysis::ReplaceInExpr(const SCEV *Expr, const NDRangePoint &point,
                                 SmallPtrSet<const SCEV *, 8> &Processed) {

//  llvm::errs() << "Replace In Expr: ";
//  Expr->dump();

//  if (!Processed.insert(Expr)) {
//    llvm::errs() << "Already processed!\n";
//    return Expr;
//  }

  // FIXME: This is ugly.
  if (const SCEVCommutativeExpr *tmp = dyn_cast<SCEVCommutativeExpr>(Expr))
    return ReplaceInExpr(tmp, point, Processed);
  if (const SCEVConstant *tmp = dyn_cast<SCEVConstant>(Expr))
    return ReplaceInExpr(tmp, point, Processed);
  if (const SCEVUnknown *tmp = dyn_cast<SCEVUnknown>(Expr))
    return ReplaceInExpr(tmp, point, Processed);
  if (const SCEVUDivExpr *tmp = dyn_cast<SCEVUDivExpr>(Expr)) 
    return ReplaceInExpr(tmp, point, Processed);
  if (const SCEVAddRecExpr *tmp = dyn_cast<SCEVAddRecExpr>(Expr))
    return ReplaceInExpr(tmp, point, Processed);
  llvm::errs() << "NULL!\n";
  return NULL;
}

//------------------------------------------------------------------------------
const SCEV *
SubscriptAnalysis::ReplaceInExpr(const SCEVAddRecExpr *Expr,
                                 const NDRangePoint &point,
                                 SmallPtrSet<const SCEV *, 8> &Processed) {
  //  Expr->dump();
  const SCEV *start = Expr->getStart();
  // const SCEV* step = addRecExpr->getStepRecurrence(*SE);
  // Check that the step is independent of the TID. TODO.
  return ReplaceInExpr(start, point, Processed);
}

//------------------------------------------------------------------------------
const SCEV *
SubscriptAnalysis::ReplaceInExpr(const SCEVCommutativeExpr *Expr,
                                 const NDRangePoint &point,
                                 SmallPtrSet<const SCEV *, 8> &Processed) {

//  llvm::errs() << "SCEVCommutativeExpr:";
//  Expr->dump();

  SmallVector<const SCEV *, 8> Operands;
  for (SCEVNAryExpr::op_iterator I = Expr->op_begin(), E = Expr->op_end();
       I != E; ++I) {
    const SCEV *NewOperand = ReplaceInExpr(*I, point, Processed);
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

  return (result);
}

//------------------------------------------------------------------------------
const SCEV *
SubscriptAnalysis::ReplaceInExpr(const SCEVConstant *Expr,
                                 const NDRangePoint &point,
                                 SmallPtrSet<const SCEV *, 8> &Processed) {
  //  llvm::errs() << "SCEVConstant:";
  //  Expr->dump();
  return Expr;
}

//------------------------------------------------------------------------------
const SCEV *
SubscriptAnalysis::ReplaceInExpr(const SCEVUnknown *Expr,
                                 const NDRangePoint &point,
                                 SmallPtrSet<const SCEV *, 8> &Processed) {
//  llvm::errs() << "SCEVUnknown: ";
//  Expr->dump();
  Value *V = Expr->getValue();
  // Implement proper replacement.
  if (Instruction *Inst = dyn_cast<Instruction>(V)) {
    if (BinaryOperator *BinOp = dyn_cast<BinaryOperator>(Inst)) {
      if(BinOp->getOpcode() == Instruction::URem) {
        // Expand remainder.
        const SCEV *Arg = SE->getSCEV(BinOp->getOperand(0));
        const SCEV *Modulo = SE->getSCEV(BinOp->getOperand(1));
        const SCEV *Result = SE->getMinusSCEV(Arg, SE->getMulExpr(SE->getUDivExpr(Arg, Modulo), Modulo));
        return ReplaceInExpr(Result, point, Processed);
      }
    }

    std::string type = NDR->getType(Inst);

    // FIXME
    if (type == NDRange::GET_LOCAL_SIZE)
      return SE->getConstant(APInt(32, 512));

    if (type != NDRange::GET_GLOBAL_ID && type != NDRange::GET_LOCAL_ID &&
        type != NDRange::GET_GROUP_ID) {
      return Expr;
    }

    if (type == NDRange::GET_GLOBAL_ID)
      type = NDRange::GET_LOCAL_ID;

    unsigned int direction = NDR->getDirection(Inst);
    unsigned int coordinate = point.getCoordinate(type, direction);
    return SE->getConstant(APInt(32, coordinate));
  } else {
    if (PHINode *phi = dyn_cast<PHINode>(V))
      return ReplaceInPhi(phi, point, Processed);
  }
  return Expr;
}

//------------------------------------------------------------------------------
const SCEV *
SubscriptAnalysis::ReplaceInExpr(const SCEVUDivExpr *Expr,
                                 const NDRangePoint &point,
                                 SmallPtrSet<const SCEV *, 8> &Processed) {

//  llvm::errs() << "SCEVUDiv: ";
//  Expr->dump();
  const SCEV *newLHS = ReplaceInExpr(Expr->getLHS(), point, Processed);
  const SCEV *newRHS = ReplaceInExpr(Expr->getRHS(), point, Processed);

  return SE->getUDivExpr(newLHS, newRHS);
}

//------------------------------------------------------------------------------
const SCEV *
SubscriptAnalysis::ReplaceInPhi(PHINode *Phi, const NDRangePoint &point,
                                SmallPtrSet<const SCEV *, 8> &Processed) {
  //  llvm::errs() << "Phi: ";
  //  Phi->dump();
  // FIXME: Pick the first argument of the phi node.
  Value *param = Phi->getIncomingValue(0);
  //  llvm::errs() << "Param: ";
  //  param->dump();
  assert(SE->isSCEVable(param->getType()) && "PhiNode argument non-SCEVable");

  const SCEV *scev = SE->getSCEV(param);

  return ReplaceInExpr(scev, point, Processed);
}
