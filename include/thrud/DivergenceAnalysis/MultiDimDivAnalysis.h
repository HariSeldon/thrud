#ifndef MULTI_DIM_DIV_ANALYSIS_H
#define MULTI_DIM_DIV_ANALYSIS_H

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/DivergentRegion.h"

#include "llvm/Pass.h"

#include "llvm/Analysis/PostDominators.h"

using namespace llvm;

namespace llvm {
  class CmpInst;
  class ScalarEvolution;
}

class MultiDimDivAnalysis : public FunctionPass {
  void operator=(const MultiDimDivAnalysis &);        // Do not implement.
  MultiDimDivAnalysis(const MultiDimDivAnalysis &);   // Do not implement.

public:
  static char ID;
  MultiDimDivAnalysis();

  virtual bool runOnFunction(Function &F); 
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;

  bool IsThreadIdDependent(Instruction *I) const;

  InstVector getThreadIds() const;
  InstVector getSizes() const;
  RegionVector getDivergentRegions() const;
  InstVector getInstToRepOutsideRegions() const;

  void AnalyzeRegion(DivergentRegion *Region);
  Value *GetTIdOperand(CmpInst* Cmp);

private:
  DivergentRegion::BoundCheck AnalyzeCmp(CmpInst *Cmp);

private:
  InstVector AllTIds;
  InstVector TIdInsts;
  InstVector Sizes;
  InstVector GroupIds;

  BranchVector Branches;
  BranchVector TIdBranches;
  RegionVector Regions;
  ValueVector Inputs;
};

#endif
