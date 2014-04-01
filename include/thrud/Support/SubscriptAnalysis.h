#ifndef SUBSCRIPT_ANALYSIS_H
#define SUBSCRIPT_ANALYSIS_H

#include "thrud/Support/Utils.h"

#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

class NDRange;
class NDRangePoint;

class SubscriptAnalysis {
public:
  SubscriptAnalysis(ScalarEvolution *SE, NDRange *NDR, unsigned int Dir);

public:
  int AnalyzeSubscript(const SCEV *Scev);
  int getThreadStride(Value *value);
  bool isConsecutive(Value *value);

private:
  ScalarEvolution *SE;
  NDRange *NDR;
  unsigned int Dir;
  typedef std::map<const SCEV*, const SCEV*> SCEVMap;

private:
  const SCEV *ReplaceInExpr(const SCEV *Expr, const NDRangePoint &point,
                            SCEVMap &Processed);
  const SCEV *ReplaceInExpr(const SCEVAddRecExpr *Expr,
                            const NDRangePoint &point,
                            SCEVMap &Processed);
  const SCEV *ReplaceInExpr(const SCEVCommutativeExpr *Expr,
                            const NDRangePoint &point,
                            SCEVMap &Processed);
  const SCEV *ReplaceInExpr(const SCEVConstant *Expr, const NDRangePoint &point,
                            SCEVMap &Processed);
  const SCEV *ReplaceInExpr(const SCEVUnknown *Expr, const NDRangePoint &point,
                            SCEVMap &Processed);
  const SCEV *ReplaceInExpr(const SCEVUDivExpr *Expr, const NDRangePoint &point,
                            SCEVMap &Processed);
  const SCEV *ReplaceInPhi(PHINode *Phi, const NDRangePoint &point,
                           SCEVMap &Processed);
};

#endif
