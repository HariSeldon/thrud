#include "thrud/Support/Utils.h"

#include "llvm/Pass.h"

using namespace llvm;

namespace llvm { class ScalarEvolution; }

class DivergentRegionAnalysis : public FunctionPass {
  void operator=(const DivergentRegionAnalysis &);          // Do not implement.
  DivergentRegionAnalysis(const DivergentRegionAnalysis &); // Do not implement.

public:
  static char ID;
  DivergentRegionAnalysis();

  virtual bool runOnFunction(Function &F);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;

  RegionVector getRegions();

private:
  RegionVector Regions;
};
