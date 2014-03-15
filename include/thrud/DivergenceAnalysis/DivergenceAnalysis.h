#ifndef DIVERGENCE_ANALYSIS_H
#define DIVERGENCE_ANALYSIS_H

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/DivergentRegion.h"
#include "thrud/Support/NDRange.h"

#include "llvm/Pass.h"

#include "llvm/Analysis/PostDominators.h"

using namespace llvm;

class DivergenceAnalysis : public FunctionPass {
  void operator=(const DivergenceAnalysis &);    // Do not implement.
  DivergenceAnalysis(const DivergenceAnalysis &); // Do not implement.

public:
  static char ID;
  DivergenceAnalysis();
  ~DivergenceAnalysis() = 0;

  virtual bool runOnFunction(Function &F);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;

  // FIXME: add accessor methods for instructions and regions.

protected:
  virtual InstVector getTids();
  void performAnalysis();

  void findRegions();
  void findBranches();
  void findExternalInsts();

protected:
  InstVector divInsts;
  InstVector externalDivInsts;
  InstVector divBranches;
  RegionVector divRegions;

  NDRange *ndr;
  PostDominatorTree *pdt;
  DominatorTree *dt;
  LoopInfo *loopInfo;
};

#endif
