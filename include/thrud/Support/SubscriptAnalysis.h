#ifndef SUBSCRIPT_ANALYSIS_H
#define SUBSCRIPT_ANALYSIS_H

#include "thrud/Support/Utils.h"
#include "thrud/Support/Warp.h"

#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

class NDRangePoint;
class OCLEnv;

class SubscriptAnalysis {
public:
  SubscriptAnalysis(ScalarEvolution *se, OCLEnv *ocl, const Warp &warp);

public:
  int getTransactionNumber(Value *value);
  float analyzeSubscript(const SCEV *scev);

private:
  ScalarEvolution *scalarEvolution;
  OCLEnv *ocl;
  Warp warp;
  typedef std::map<const SCEV*, const SCEV*> SCEVMap;

private:
  int computeTransactionNumber(const std::vector<const SCEV*> &scevs);
  bool verifyUnknown(const SCEV* scev, const SCEV* unknown);
  bool verifyUnknown(const std::vector<const SCEV*> &scevs, const SCEV* unknown);
  const SCEVUnknown* getUnknownSCEV(const SCEV* scev);

  // Replacing methods.
  const SCEV *replaceInExpr(const SCEV *expr, const NDRangePoint &point,
                            SCEVMap &processed);
  const SCEV *replaceInExpr(const SCEVAddRecExpr *expr,
                            const NDRangePoint &point,
                            SCEVMap &processed);
  const SCEV *replaceInExpr(const SCEVCommutativeExpr *expr,
                            const NDRangePoint &point,
                            SCEVMap &processed);
  const SCEV *replaceInExpr(const SCEVConstant *expr, const NDRangePoint &point,
                            SCEVMap &processed);
  const SCEV *replaceInExpr(const SCEVUnknown *expr, const NDRangePoint &point,
                            SCEVMap &processed);
  const SCEV *replaceInExpr(const SCEVUDivExpr *expr, const NDRangePoint &point,
                            SCEVMap &processed);
  const SCEV *replaceInExpr(const SCEVCastExpr *expr, const NDRangePoint &point,
                            SCEVMap &processed);
  const SCEV *replaceInPhi(PHINode *Phi, const NDRangePoint &point,
                           SCEVMap &processed);

  const SCEV *resolveInstruction(llvm::Instruction *instruction, const NDRangePoint &point);
};

#endif
