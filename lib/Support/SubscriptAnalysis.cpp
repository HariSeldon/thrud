#include "thrud/Support/SubscriptAnalysis.h"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#include "llvm/Support/raw_ostream.h"

#include "thrud/Support/NDRange.h"
#include "thrud/Support/NDRangePoint.h"

#include <algorithm>
#include <functional>
#include <iterator>

// 32 threads in a warp.
int SubscriptAnalysis::WARP_SIZE = 32;
// 128 Bytes in a cache line.
int SubscriptAnalysis::CACHELINE_SIZE = 128;

SubscriptAnalysis::SubscriptAnalysis(ScalarEvolution *SE, NDRange *NDR,
                                     unsigned int Dir)
    : SE(SE), NDR(NDR), Dir(Dir) {}

//------------------------------------------------------------------------------
int getTypeWidth(Type *type) {
  assert(type->isPointerTy() && "Type is not a pointer");
  type = type->getPointerElementType();
  int result = type->getPrimitiveSizeInBits();
  if(result == 0) {
    return 32;
  }
  return result / 8;
}

//------------------------------------------------------------------------------
float SubscriptAnalysis::getThreadStride(Value *value) {
  int width = getTypeWidth(value->getType());

  if (!isa<GetElementPtrInst>(value)) {
    return 0;
  }

  if (!SE->isSCEVable(value->getType())) {
    return -1;
  }

  const SCEV *scev = SE->getSCEV(value);
  return analyzeSubscript(scev);
}

// FIXME this is broken. Restore the old function so to compute the interval
// of consecutive threads.
bool SubscriptAnalysis::isConsecutive(Value *value) {
  return getThreadStride(value) == 1.f;
}

//------------------------------------------------------------------------------
float SubscriptAnalysis::analyzeSubscript(const SCEV *scev) {
  std::vector<const SCEV *> resultVector;
  resultVector.reserve(WARP_SIZE);

  for (int index = 0; index < WARP_SIZE; ++index) {
    NDRangePoint point(index, 0, 0, 0, 0, 0, 1024, 1024, 1, 128, 128, 1);

    SCEVMap processed;
    const SCEV *expr = replaceInExpr(scev, point, processed);
    if (isa<SCEVCouldNotCompute>(expr)) {
      return 0;
    }

    resultVector.push_back(expr);
  }

  assert((int)resultVector.size() == WARP_SIZE && "Missing expressions");

  return computeTransactionNumber(resultVector);
}

//------------------------------------------------------------------------------
int SubscriptAnalysis::computeTransactionNumber(const std::vector<const SCEV *> &scevs) {
  // Get type width.
  const SCEV *first = scevs[0];
//  int width = getTypeWidth(first->getType());

  const SCEV *unknown = getUnknownSCEV(first);
  if(unknown == NULL) {
    return 32; 
  }

  verifyUnknown(scevs, unknown);

  // This could be done with a std::transform.
  //std::transform(scevs.begin(), scevs.end(), scevs.begin(),
  //std::bind2nd(std::mem_fun_ref(&SubscriptAnalysis::getMinusSCEV), unknown));
  std::vector<const SCEV *> offsets;
  for (std::vector<const SCEV *>::const_iterator iter = scevs.begin(),
                                                 iterEnd = scevs.end();
       iter != iterEnd; ++iter) {
    const SCEV *currentSCEV = *iter;
    offsets.push_back(SE->getMinusSCEV(currentSCEV, unknown));
  }

  std::vector<int> indices;
  // This is a std::transform, again.
  for (std::vector<const SCEV *>::const_iterator iter = offsets.begin(),
                                                 iterEnd = offsets.end();
       iter != iterEnd; ++iter) {

    const SCEV *currentSCEV = *iter;

    if (const SCEVAddRecExpr *AddRecSCEV =
            dyn_cast<SCEVAddRecExpr>(currentSCEV)) {
      currentSCEV = AddRecSCEV->getStart();
    }

    if (const SCEVConstant *ConstSCEV = dyn_cast<SCEVConstant>(currentSCEV)) {
      const ConstantInt *value = ConstSCEV->getValue();
      indices.push_back((int)value->getValue().roundToDouble());
    }
  }

  std::transform(indices.begin(), indices.end(), indices.begin(),
                 std::bind2nd(std::divides<int>(), CACHELINE_SIZE));

  std::vector<int>::iterator uniqueEnd = std::unique(indices.begin(), indices.end());
  int uniqueCacheLines = std::distance(indices.begin(), uniqueEnd);
 
  return uniqueCacheLines;
}

//------------------------------------------------------------------------------
const SCEVUnknown *SubscriptAnalysis::getUnknownSCEV(const SCEV *scev) {
  if (const SCEVUnknown *unknown = dyn_cast<SCEVUnknown>(scev)) {
    return unknown;
  }

  if (const SCEVAddExpr *add = dyn_cast<SCEVAddExpr>(scev)) {
    if (add->getNumOperands() != 2)
      return NULL;

    if (isa<SCEVConstant>(add->getOperand(0)) &&
        isa<SCEVUnknown>(add->getOperand(1))) {
      return dyn_cast<SCEVUnknown>(add->getOperand(1));
    }

    if (isa<SCEVConstant>(add->getOperand(1)) &&
        isa<SCEVUnknown>(add->getOperand(0))) {
      return dyn_cast<SCEVUnknown>(add->getOperand(0));
    }

    return NULL;
  }

  return NULL;
}

bool SubscriptAnalysis::verifyUnknown(const SCEV *scev, const SCEV *unknown) {
  if (const SCEVAddExpr *add = dyn_cast<SCEVAddExpr>(scev)) {
    return std::find(add->op_begin(), add->op_end(), unknown) != add->op_end();
  }
  return false;
}

bool SubscriptAnalysis::verifyUnknown(const std::vector<const SCEV *> &scevs,
                                      const SCEV *unknown) {
  bool ok = true;
  for (std::vector<const SCEV *>::const_iterator iter = scevs.begin(),
                                                 iterEnd = scevs.end();
       iter != iterEnd; ++iter) {
    ok &= verifyUnknown(*iter, unknown);
  }
  return ok;
}

//------------------------------------------------------------------------------
const SCEV *SubscriptAnalysis::replaceInExpr(const SCEV *expr,
                                             const NDRangePoint &point,
                                             SCEVMap &processed) {
  SCEVMap::iterator iter = processed.find(expr);
  if (iter != processed.end()) {
    return processed[expr];
  }

  const SCEV *result = NULL;

  // FIXME: This is ugly.
  if (const SCEVCommutativeExpr *tmp = dyn_cast<SCEVCommutativeExpr>(expr))
    result = replaceInExpr(tmp, point, processed);
  if (const SCEVConstant *tmp = dyn_cast<SCEVConstant>(expr))
    result = replaceInExpr(tmp, point, processed);
  if (const SCEVUnknown *tmp = dyn_cast<SCEVUnknown>(expr))
    result = replaceInExpr(tmp, point, processed);
  if (const SCEVUDivExpr *tmp = dyn_cast<SCEVUDivExpr>(expr))
    result = replaceInExpr(tmp, point, processed);
  if (const SCEVAddRecExpr *tmp = dyn_cast<SCEVAddRecExpr>(expr))
    result = replaceInExpr(tmp, point, processed);
  if (const SCEVCastExpr *tmp = dyn_cast<SCEVCastExpr>(expr))
    result = replaceInExpr(tmp, point, processed);

  processed[expr] = result;

  return result;
}

//------------------------------------------------------------------------------
const SCEV *SubscriptAnalysis::replaceInExpr(const SCEVAddRecExpr *expr,
                                             const NDRangePoint &point,
                                             SCEVMap &processed) {
  //  expr->dump();
  const SCEV *start = expr->getStart();
  // const SCEV* step = addRecExpr->getStepRecurrence(*SE);
  // Check that the step is independent of the TID. TODO.
  return replaceInExpr(start, point, processed);
}

//------------------------------------------------------------------------------
const SCEV *SubscriptAnalysis::replaceInExpr(const SCEVCommutativeExpr *expr,
                                             const NDRangePoint &point,
                                             SCEVMap &processed) {

  //  llvm::errs() << "SCEVCommutativeExpr:";
  //  expr->dump();

  SmallVector<const SCEV *, 8> operands;
  for (SCEVNAryExpr::op_iterator I = expr->op_begin(), E = expr->op_end();
       I != E; ++I) {
    const SCEV *NewOperand = replaceInExpr(*I, point, processed);
    if (isa<SCEVCouldNotCompute>(NewOperand))
      return SE->getCouldNotCompute();
    operands.push_back(NewOperand);
  }
  const SCEV *result = NULL;

  if (isa<SCEVAddExpr>(expr))
    result = SE->getAddExpr(operands);
  if (isa<SCEVMulExpr>(expr))
    result = SE->getMulExpr(operands);
  if (isa<SCEVSMaxExpr>(expr))
    result = SE->getSMaxExpr(operands);
  if (isa<SCEVUMaxExpr>(expr))
    result = SE->getUMaxExpr(operands);

  return (result);
}

//------------------------------------------------------------------------------
const SCEV *SubscriptAnalysis::replaceInExpr(const SCEVConstant *expr,
                                             const NDRangePoint &point,
                                             SCEVMap &processed) {
  return expr;
}

//------------------------------------------------------------------------------
const SCEV *SubscriptAnalysis::replaceInExpr(const SCEVUnknown *expr,
                                             const NDRangePoint &point,
                                             SCEVMap &processed) {
  Value *V = expr->getValue();
  // Implement actual replacement.
  if (Instruction *Inst = dyn_cast<Instruction>(V)) {

    // Manage binary operations.
    if (BinaryOperator *BinOp = dyn_cast<BinaryOperator>(Inst)) {
      //llvm::errs() << "BinaryOperator: ";
      //BinOp->dump();

      // Modulo.
      if (BinOp->getOpcode() == Instruction::URem) {
        const SCEV *Arg = SE->getSCEV(BinOp->getOperand(0));
        const SCEV *Modulo = SE->getSCEV(BinOp->getOperand(1));
        const SCEV *Result = SE->getMinusSCEV(
            Arg, SE->getMulExpr(SE->getUDivExpr(Arg, Modulo), Modulo));
        return replaceInExpr(Result, point, processed);
      }

      // Signed division.
      if (BinOp->getOpcode() == Instruction::SDiv) {
        const SCEV *First = SE->getSCEV(BinOp->getOperand(0));
        const SCEV *Second = SE->getSCEV(BinOp->getOperand(1));
        const SCEV *Div = SE->getUDivExpr(First, Second);
        return replaceInExpr(Div, point, processed);
      }

      llvm::errs() << "Could not compute!\n";
      // All the rest.
      return SE->getCouldNotCompute();
    }

    // Manage casts.
    if (IsIntCast(Inst)) {
      CallInst *Call = dyn_cast<CallInst>(Inst);
      const SCEV *ArgSCEV = SE->getSCEV(Call->getArgOperand(0));
      return replaceInExpr(ArgSCEV, point, processed);
    }

    // Manage phi nodes.
    if (PHINode *phi = dyn_cast<PHINode>(V))
      return replaceInPhi(phi, point, processed);

    std::string type = NDR->getType(Inst);
    if (type == "")
      return SE->getCouldNotCompute();
    unsigned int direction = NDR->getDirection(Inst);
    unsigned int coordinate = point.getCoordinate(type, direction);

    return SE->getConstant(APInt(32, coordinate));
  }
  return expr;
}

//------------------------------------------------------------------------------
const SCEV *SubscriptAnalysis::replaceInExpr(const SCEVUDivExpr *expr,
                                             const NDRangePoint &point,
                                             SCEVMap &processed) {

  //  llvm::errs() << "SCEVUDiv: ";
  //  Expr->dump();
  const SCEV *newLHS = replaceInExpr(expr->getLHS(), point, processed);
  if (isa<SCEVCouldNotCompute>(newLHS))
    return SE->getCouldNotCompute();
  const SCEV *newRHS = replaceInExpr(expr->getRHS(), point, processed);
  if (isa<SCEVCouldNotCompute>(newRHS))
    return SE->getCouldNotCompute();

  return SE->getUDivExpr(newLHS, newRHS);
}

//------------------------------------------------------------------------------
const SCEV *SubscriptAnalysis::replaceInExpr(const SCEVCastExpr *expr,
                                             const NDRangePoint &point,
                                             SCEVMap &processed) {
  return expr->getOperand();
}

//------------------------------------------------------------------------------
const SCEV *SubscriptAnalysis::replaceInPhi(PHINode *Phi,
                                            const NDRangePoint &point,
                                            SCEVMap &processed) {
  //  llvm::errs() << "Phi: ";
  //  Phi->dump();
  // FIXME: Pick the first argument of the phi node.
  Value *param = Phi->getIncomingValue(0);
  assert(SE->isSCEVable(param->getType()) && "PhiNode argument non-SCEVable");

  const SCEV *scev = SE->getSCEV(param);

  processed[SE->getSCEV(Phi)] = scev;

  return replaceInExpr(scev, point, processed);
}

////------------------------------------------------------------------------------
//int SubscriptAnalysis::analyzeSubscript(const SCEV *scev,
//                                        std::vector<int> &resultVector) {
//  resultVector.clear();
//  resultVector.reserve(WARP_SIZE - 1);
//
//  //  llvm::errs() << "Original SCEV: ";
//  //  scev->dump();
//  // Try with many points.
//  for (unsigned int index = 0; index < WARP_SIZE - 1; ++index) {
//    NDRangePoint firstPoint(index, 0, 0, 0, 0, 0, 1024, 1024, 1, 128, 128, 1);
//    NDRangePoint secondPoint(index + 1, 0, 0, 0, 0, 0, 1024, 1024, 1, 128,
// 128,
//                             1);
//
//    SCEVMap processed1;
//    SCEVMap processed2;
//    const SCEV *firstExpr = replaceInExpr(scev, firstPoint, processed1);
//    llvm::errs() << "RESULT1: ";
//    firstExpr->dump();
//    const SCEV *secondExpr = replaceInExpr(scev, secondPoint, processed2);
//    llvm::errs() << "RESULT2: ";
//    secondExpr->dump();
//
//    if (isa<SCEVCouldNotCompute>(firstExpr) ||
//        isa<SCEVCouldNotCompute>(secondExpr)) {
//      return -1;
//    }
//
//    const SCEV *result = SE->getMinusSCEV(secondExpr, firstExpr);
//
//    //  llvm::errs() << "Difference result.\n";
//    //  result->dump();
//
//    if (result == NULL) {
//      return -1;
//    }
//
//    int numericResult = -1;
//    if (const SCEVAddRecExpr *AddRecSCEV = dyn_cast<SCEVAddRecExpr>(result)) {
//      result = AddRecSCEV->getStart();
//    }
//
//    if (const SCEVConstant *ConstSCEV = dyn_cast<SCEVConstant>(result)) {
//      const ConstantInt *value = ConstSCEV->getValue();
//      numericResult = (int)value->getValue().roundToDouble();
//    }
//
//    resultVector.push_back(numericResult);
//  }
//  return 0;
//}
