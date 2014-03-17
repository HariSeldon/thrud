#include "thrud/DivergenceAnalysis/DivergenceAnalysis.h"

#include "llvm/Pass.h"
using namespace llvm;

class DivergentRegion;

class BranchExtraction : public FunctionPass {
public:
  static char ID;
  BranchExtraction();

  virtual bool runOnFunction(Function &function);
  virtual void getAnalysisUsage(AnalysisUsage &au) const;

private:
  void extractBranches(DivergentRegion *region);
  void isolateRegion(DivergentRegion *region);

private:
  LoopInfo *loopInfo;
  DominatorTree *dt;
  PostDominatorTree *pdt;
};
