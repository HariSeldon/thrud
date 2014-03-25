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
  InstVector &getOutermostDivInsts();
  InstVector getDivInsts(DivergentRegion *region, unsigned int branchIndex);
  bool isDivergent(Instruction *inst);

  RegionVector &getDivRegions();
  RegionVector &getOutermostDivRegions();
  RegionVector getDivRegions(DivergentRegion *region, unsigned int branchIndex);

protected:
  virtual InstVector getTids();
  void performAnalysis();

  void init();
  void findBranches();
  void findRegions();
  void findOutermostInsts(InstVector &insts, RegionVector &regions,
                          InstVector &result);
  void findOutermostRegions();

protected:
  InstVector divInsts;
  InstVector outermostDivInsts;
  InstVector divBranches;
  RegionVector regions;
  RegionVector outermostRegions;

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
