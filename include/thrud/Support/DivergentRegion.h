#ifndef DIVERGENT_REGION_H
#define DIVERGENT_REGION_H

#include "thrud/Support/DataTypes.h"

#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"

namespace llvm {
  class CmpInst;
  class LoopInfo;
  class ScalarEvolution;
}

class RegionBounds;

class DivergentRegion {
public:
  enum BoundCheck {LB, UB, EQ, ND, DATA};

public:
  DivergentRegion(BasicBlock *Header, BasicBlock *Exiting);

  // Getter and Setter. 
  BasicBlock *getHeader();
  BasicBlock *getExiting();

  RegionBounds *getBounds();
  BlockVector *getBlocks();

  void setHeader(BasicBlock *Header);
  void setExiting(BasicBlock *Exiting);

  void setCondition(BoundCheck condition);
  BoundCheck getCondition();

  bool IsStrict();

  // Region filling.
  void FillRegion(DominatorTree *DT, PostDominatorTree *PDT);
  void UpdateRegion();

  // Region analysis.
  bool Contains(const Instruction *I);
  bool Contains(const BasicBlock *BB);

  void Analyze(ScalarEvolution *SE, LoopInfo *LI,
               ValueVector &TIds, ValueVector &Inputs);
  DivergentRegion::BoundCheck AnalyzeCmp(ScalarEvolution *SE, LoopInfo *LI,
                                         CmpInst *Cmp, ValueVector &TIds);

  bool PerformHeaderCheck(DominatorTree *DT);

  // Dump.
  void dump();

private:
  RegionBounds *Bounds;
  BlockVector *Blocks;
  BoundCheck Condition; 

};

typedef std::vector<DivergentRegion*> RegionVector;

#endif
