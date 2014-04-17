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
  int analyzeSubscript(const SCEV *Scev, std::vector<int> &result);
  float getThreadStride(Value *value);
  bool isConsecutive(Value *value);

private:
  ScalarEvolution *SE;
  NDRange *NDR;
  unsigned int Dir;
  typedef std::map<const SCEV*, const SCEV*> SCEVMap;

private:
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
};

#endif
