#ifndef SUBSCRIPT_ANALYSIS_H
#define SUBSCRIPT_ANALYSIS_H

#include "thrud/Support/Utils.h"

#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

class NDRange;

class SubscriptAnalysis {
public:
  SubscriptAnalysis(ScalarEvolution *SE, NDRange *NDR, unsigned int Dir);

public:
  int AnalyzeSubscript(const SCEV *Scev);
  int GetThreadStride(Value *value);

private:
  ScalarEvolution *SE;
  NDRange *NDR;
  unsigned int Dir;

private:
  const SCEV *ReplaceInExpr(const SCEV *Expr, const APInt &globalValue,
                            const APInt &localValue, const APInt &groupValue,
                            SmallPtrSet<const SCEV *, 8> &Processed);
  const SCEV *ReplaceInExpr(const SCEVAddRecExpr *Expr,
                            const APInt &globalValue, const APInt &localValue,
                            const APInt &groupValue,
                            SmallPtrSet<const SCEV *, 8> &Processed);
  const SCEV *ReplaceInExpr(const SCEVCommutativeExpr *Expr,
                            const APInt &globalValue, const APInt &localValue,
                            const APInt &groupValue,
                            SmallPtrSet<const SCEV *, 8> &Processed);
  const SCEV *ReplaceInExpr(const SCEVConstant *Expr, const APInt &globalValue,
                            const APInt &localValue, const APInt &groupValue,
                            SmallPtrSet<const SCEV *, 8> &Processed);
  const SCEV *ReplaceInExpr(const SCEVUnknown *Expr, const APInt &globalValue,
                            const APInt &localValue, const APInt &groupValue,
                            SmallPtrSet<const SCEV *, 8> &Processed);
  const SCEV *ReplaceInExpr(const SCEVUDivExpr *Expr, const APInt &globalValue,
                            const APInt &localValue, const APInt &groupValue,
                            SmallPtrSet<const SCEV *, 8> &Processed);
  const SCEV *ReplaceInPhi(PHINode *Phi, const APInt &globalValue,
                           const APInt &localValue, const APInt &groupValue,
                           SmallPtrSet<const SCEV *, 8> &Processed);
};

#endif
