#include "thrud/DivergenceAnalysis/SingleDimDivAnalysis.h"

#include "llvm/Pass.h"
using namespace llvm;

class DivergentRegion;
class DivergenceInfo;

class BranchExtraction : public FunctionPass {
  void operator=(const BranchExtraction &);   // Do not implement.
  BranchExtraction(const BranchExtraction &); // Do not implement.

public:
  static char ID;
  BranchExtraction();

  virtual bool runOnFunction(Function &F);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;

private:
  void isolateRegion(DivergentRegion *Region);
  bool findRegionBlocks(DivergentRegion *Region, BlockVector &RegionBlocks);

private:
  SingleDimDivAnalysis *SDDA;

};
