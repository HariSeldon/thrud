#ifndef DIVERGENCE_ANALYSIS_H
#define DIVERGENCE_ANALYSIS_H

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/DivergentRegion.h"
#include "thrud/Support/NDRange.h"

#include "thrud/Support/ControlDependenceAnalysis.h"

#include "llvm/Pass.h"

#include "llvm/Analysis/PostDominators.h"

using namespace llvm;

class DivergenceAnalysis {
public:
  InstVector &getDivInsts();
  InstVector &getDivInstsOutsideRegions();
  RegionVector &getDivRegions();
  bool isDivergent(Instruction *inst);

protected:
  virtual InstVector getTids();
  void performAnalysis();

  void findBranches();
  void findRegions();
  void findExternalInsts();
  void findOutermostBranches(InstVector &result);

protected:
  InstVector divInsts;
  InstVector externalDivInsts;
  InstVector divBranches;
  RegionVector regions;

  NDRange *ndr;
  PostDominatorTree *pdt;
  DominatorTree *dt;
  LoopInfo *loopInfo;
  ControlDependenceAnalysis *cda;
};

class SingleDimDivAnalysis : public FunctionPass, public DivergenceAnalysis {
public:
  static char ID;
  SingleDimDivAnalysis();

  virtual bool runOnFunction(Function &F);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;

public:
  virtual InstVector getTids();
};

class MultiDimDivAnalysis : public FunctionPass, public DivergenceAnalysis {
public:
  static char ID;
  MultiDimDivAnalysis();

  virtual bool runOnFunction(Function &F);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;

public:
  virtual InstVector getTids();
};

#endif
