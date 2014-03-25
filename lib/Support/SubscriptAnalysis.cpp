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

//  if (Instruction *I = dyn_cast<Instruction>(value)) {
//    I->dump();
//    I->getParent()->getParent()->dump();
//  }

  const SCEV *scev = SE->getSCEV(value);
  int result = AnalyzeSubscript(scev);
  return result;
}

//------------------------------------------------------------------------------
// WARNING: SCEV does not support %.
int SubscriptAnalysis::AnalyzeSubscript(const SCEV *Scev) {
  NDRangePoint firstPoint(0, 0, 0, 0, 0, 0, 1024, 1024, 1, 128, 128, 1);
  NDRangePoint secondPoint(1, 0, 0, 0, 0, 0, 1024, 1024, 1, 128, 128, 1);

//  llvm::errs() << "Original SCEV: ";
//  Scev->dump();

  SCEVMap Processed1;
  SCEVMap Processed2;
  const SCEV *firstExpr = ReplaceInExpr(Scev, firstPoint, Processed1);
//  llvm::errs() << "Result1: ";
//  firstExpr->dump();
  const SCEV *secondExpr = ReplaceInExpr(Scev, secondPoint, Processed2);
//  llvm::errs() << "Result2: ";
//  secondExpr->dump();

  if(isa<SCEVCouldNotCompute>(firstExpr) || isa<SCEVCouldNotCompute>(secondExpr)) {
    return -1; 
  }

  const SCEV *result = SE->getMinusSCEV(secondExpr, firstExpr);

//  llvm::errs() << "Difference result.\n";
//  result->dump();

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
                                 SCEVMap &Processed) {

//  llvm::errs() << "Replace In Expr: ";
//  Expr->dump();

  SCEVMap::iterator iter = Processed.find(Expr);
  if(iter != Processed.end()) {
    return Processed[Expr];    
  }

  const SCEV* result = NULL;

  // FIXME: This is ugly.
  if (const SCEVCommutativeExpr *tmp = dyn_cast<SCEVCommutativeExpr>(Expr))
    result = ReplaceInExpr(tmp, point, Processed);
  if (const SCEVConstant *tmp = dyn_cast<SCEVConstant>(Expr))
    result = ReplaceInExpr(tmp, point, Processed);
  if (const SCEVUnknown *tmp = dyn_cast<SCEVUnknown>(Expr))
    result = ReplaceInExpr(tmp, point, Processed);
  if (const SCEVUDivExpr *tmp = dyn_cast<SCEVUDivExpr>(Expr)) 
    result = ReplaceInExpr(tmp, point, Processed);
  if (const SCEVAddRecExpr *tmp = dyn_cast<SCEVAddRecExpr>(Expr))
    result = ReplaceInExpr(tmp, point, Processed);

  Processed[Expr] = result;
//  llvm::errs() << "-- Expr: ";
//  Expr->dump();
//  llvm::errs() << "-- Result: ";
//  result->dump();

  return result; 
}

//------------------------------------------------------------------------------
const SCEV *
SubscriptAnalysis::ReplaceInExpr(const SCEVAddRecExpr *Expr,
                                 const NDRangePoint &point,
                                 SCEVMap &Processed) {
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
                                 SCEVMap &Processed) {

//  llvm::errs() << "SCEVCommutativeExpr:";
//  Expr->dump();

  SmallVector<const SCEV *, 8> Operands;
  for (SCEVNAryExpr::op_iterator I = Expr->op_begin(), E = Expr->op_end();
       I != E; ++I) {
    const SCEV *NewOperand = ReplaceInExpr(*I, point, Processed);
    if(isa<SCEVCouldNotCompute>(NewOperand))
      return SE->getCouldNotCompute();
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
                                 SCEVMap &Processed) {
  return Expr;
}

//------------------------------------------------------------------------------
const SCEV *
SubscriptAnalysis::ReplaceInExpr(const SCEVUnknown *Expr,
                                 const NDRangePoint &point,
                                 SCEVMap &Processed) {
//  llvm::errs() << "SCEVUnknown: ";
//  Expr->dump();
  Value *V = Expr->getValue();
  // Implement actual replacement.
  if (Instruction *Inst = dyn_cast<Instruction>(V)) {

    // Manage binary operations.
    if (BinaryOperator *BinOp = dyn_cast<BinaryOperator>(Inst)) {      
      // Modulo.
      if(BinOp->getOpcode() == Instruction::URem) {
        const SCEV *Arg = SE->getSCEV(BinOp->getOperand(0));
        const SCEV *Modulo = SE->getSCEV(BinOp->getOperand(1));
        const SCEV *Result = SE->getMinusSCEV(Arg, SE->getMulExpr(SE->getUDivExpr(Arg, Modulo), Modulo));
        return ReplaceInExpr(Result, point, Processed);
      }
  
      // Signed division.
      if(BinOp->getOpcode() == Instruction::SDiv) {
        const SCEV *First = SE->getSCEV(BinOp->getOperand(0));
        const SCEV *Second = SE->getSCEV(BinOp->getOperand(1));
        const SCEV *Div = SE->getUDivExpr(First, Second);
        return ReplaceInExpr(Div, point, Processed);
      }

      llvm::errs() << "Could not compute: ";
      // All the rest.
      return SE->getCouldNotCompute();
    }

    // Manage casts.
    if (IsIntCast(Inst)) {
//      llvm::errs() << "Cast!\n";
      CallInst *Call = dyn_cast<CallInst>(Inst);
      const SCEV *ArgSCEV = SE->getSCEV(Call->getArgOperand(0));
//      ArgSCEV->dump();
      return ReplaceInExpr(ArgSCEV, point, Processed);
    }

    // Manage phi nodes.
    if (PHINode *phi = dyn_cast<PHINode>(V))
      return ReplaceInPhi(phi, point, Processed);

    std::string type = NDR->getType(Inst);
    if(type == "")
      return SE->getCouldNotCompute();
    unsigned int direction = NDR->getDirection(Inst);
    unsigned int coordinate = point.getCoordinate(type, direction);

//    llvm::errs() << "coo: " << type << " " << coordinate << "\n";

    return SE->getConstant(APInt(32, coordinate));
  } 
  return Expr;
}

//------------------------------------------------------------------------------
const SCEV *
SubscriptAnalysis::ReplaceInExpr(const SCEVUDivExpr *Expr,
                                 const NDRangePoint &point,
                                 SCEVMap &Processed) {

//  llvm::errs() << "SCEVUDiv: ";
//  Expr->dump();
  const SCEV *newLHS = ReplaceInExpr(Expr->getLHS(), point, Processed);
  if(isa<SCEVCouldNotCompute>(newLHS)) 
    return SE->getCouldNotCompute();
  const SCEV *newRHS = ReplaceInExpr(Expr->getRHS(), point, Processed);
  if(isa<SCEVCouldNotCompute>(newRHS)) 
    return SE->getCouldNotCompute();
    
  return SE->getUDivExpr(newLHS, newRHS);
}

//------------------------------------------------------------------------------
const SCEV *
SubscriptAnalysis::ReplaceInPhi(PHINode *Phi, const NDRangePoint &point,
                                SCEVMap &Processed) {
//  llvm::errs() << "Phi: ";
//  Phi->dump();
  // FIXME: Pick the first argument of the phi node.
  Value *param = Phi->getIncomingValue(0);
  assert(SE->isSCEVable(param->getType()) && "PhiNode argument non-SCEVable");

  const SCEV *scev = SE->getSCEV(param);

  Processed[SE->getSCEV(Phi)] = scev;

  return ReplaceInExpr(scev, point, Processed);
}
