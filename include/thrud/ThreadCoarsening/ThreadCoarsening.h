#ifndef THREAD_COARSENING_H
#define THREAD_COARSENING_H

#include "thrud/DivergenceAnalysis/DivergenceAnalysis.h"

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/DivergentRegion.h"

#include "llvm/Pass.h"

#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/RegionInfo.h"

using namespace llvm;

namespace llvm {
class BasicBlock;
}

class ThreadCoarsening : public FunctionPass {
public:
  enum DivRegionOption {
    FullReplication,
    TrueBranchMerging,
    FalseBranchMerging,
    FullMerging
  };

public:
  static char ID;
  ThreadCoarsening();

  virtual bool runOnFunction(Function &F);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;

private:
  void init();

  // NDRange scaling.
  void scaleNDRange();
  void scaleSizes();
  void scaleIds();

  // Coarsening.
  void coarsenFunction();
  void replicateInst(Instruction *Inst);
  void replicateRegion(DivergentRegion *R);
  void replicateRegionClassic(DivergentRegion *R);
  void replicateRegionFalseMerging(DivergentRegion *R);
  void replicateRegionTrueMerging(DivergentRegion *R);
  void replicateRegionMerging(DivergentRegion *R, unsigned int branch);
  void replicateRegionFullMerging(DivergentRegion *R);
  void applyCoarseningMap(DivergentRegion &region, unsigned int index);
  void applyCoarseningMap(BasicBlock *block, unsigned int index);
  void applyCoarseningMap(Instruction *inst, unsigned int index);
  Instruction *getCoarsenedInstruction(Instruction *inst,
                                       unsigned int coarseningIndex);
  Instruction *insertBooleanReduction(Instruction *base, InstVector &insts,
                                      llvm::Instruction::BinaryOps binOp);

  // Manage placeholders.
  void replacePlaceholders();

  //  void InsertReplicatedInst(InstPairs &IP, Map &map);
  //  void PerformDuplication();
  //  void BuildPhiNodeMap(BasicBlock *OldBlock, BasicBlock *NewBlock, Map
  // &map);

private:
  unsigned int direction;
  unsigned int factor;
  unsigned int stride;
  DivRegionOption divRegionOption;

  PostDominatorTree *pdt;
  DominatorTree *dt;
  RegionInfo *regionInfo;
  SingleDimDivAnalysis *sdda;
  LoopInfo *loopInfo;
  NDRange *ndr;

  CoarseningMap cMap;
  CoarseningMap phMap;
  Map phReplacementMap;
};

#endif
