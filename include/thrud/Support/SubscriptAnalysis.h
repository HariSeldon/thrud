#include "thrud/Support/Utils.h"

#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

int GetThreadStride(Value *value, ScalarEvolution *SE, ValueVector &TIds);

int AnalyzeSubscript(ScalarEvolution *SE, const SCEV *Scev, ValueVector &TIds);

const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEV *Expr,
                          ValueVector &TIds, const APInt &value,
                          SmallPtrSet<const SCEV *, 8> &Processed);

const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEVAddRecExpr *Expr,
                          ValueVector &TIds, const APInt &value,
                          SmallPtrSet<const SCEV *, 8> &Processed);
const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEVCommutativeExpr *Expr,
                          ValueVector &TIds, const APInt &value,
                          SmallPtrSet<const SCEV *, 8> &Processed);
const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEVConstant *Expr,
                          ValueVector &TIds, const APInt &value,
                          SmallPtrSet<const SCEV *, 8> &Processed);
const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEVUnknown *Expr,
                          ValueVector &TIds, const APInt &value,
                          SmallPtrSet<const SCEV *, 8> &Processed);
const SCEV *ReplaceInExpr(ScalarEvolution *SE, const SCEVUDivExpr *Expr,
                          ValueVector &TIds, const APInt &value,
                          SmallPtrSet<const SCEV *, 8> &Processed);
const SCEV *ReplaceInPhi(ScalarEvolution *SE, PHINode *Phi, ValueVector &TIds,
                         const APInt &value,
                         SmallPtrSet<const SCEV *, 8> &Processed);
