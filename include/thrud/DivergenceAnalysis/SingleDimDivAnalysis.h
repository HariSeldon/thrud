#ifndef SINGLE_DIM_DIV_ANALYSIS_H
#define SINGLE_DIM_DIV_ANALYSIS_H

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/DivergentRegion.h"
#include "thrud/Support/NDRange.h"

#include "llvm/Pass.h"

#include "llvm/Analysis/PostDominators.h"

using namespace llvm;

namespace llvm {
class CmpInst;
class ScalarEvolution;
class LoopInfo;
}

class SingleDimDivAnalysis : public FunctionPass {
  void operator=(const SingleDimDivAnalysis &);       // Do not implement.
  SingleDimDivAnalysis(const SingleDimDivAnalysis &); // Do not implement.

public:
  static char ID;
  SingleDimDivAnalysis();

  virtual bool runOnFunction(Function &F);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;

  bool IsThreadIdDependent(Instruction *I) const;
  InstVector FindInstToReplicate();
  InstVector getTIdInsts() const;

  InstVector getThreadIds() const;
  InstVector getSizes() const;
  RegionVector getDivergentRegions() const;
  InstVector getInstToRepOutsideRegions() const;

  void AnalyzeRegion(DivergentRegion *Region);
  Value *GetTIdOperand(CmpInst *Cmp);

private:
  DivergentRegion::BoundCheck AnalyzeCmp(CmpInst *Cmp);

private:
  InstVector TIds;
  InstVector AllTIds;
  InstVector TIdInsts;
  InstVector Sizes;
  InstVector GroupIds;

  BranchVector Branches;
  BranchVector TIdBranches;
  RegionVector Regions;
  InstVector ToRep;
  ValueVector Inputs;

  PostDominatorTree *PDT;
  DominatorTree *DT;
  ScalarEvolution *SE;
  LoopInfo *LI;
  NDRange *NDR;
};

#endif
