#ifndef REGION_BOUNDARY_H
#define REGION_BOUNDARY_H

#include "thrud/Support/DataTypes.h"

#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"

namespace llvm {
class BasicBlock;
class DominatorTree;
class Instruction;
}

// Lightweigth couple of blocks enclosing a region.
class RegionBounds {
public:
  RegionBounds(BasicBlock *Header, BasicBlock *Exiting);
  RegionBounds();

public:
  BasicBlock *getHeader();
  BasicBlock *getExiting();

  void setHeader(BasicBlock *Header);
  void setExiting(BasicBlock *Exiting);

  bool Contains(const BasicBlock *BB, const DominatorTree *DT,
                const PostDominatorTree *PDT);
  bool Contains(const Instruction *Inst, const DominatorTree *DT,
                const PostDominatorTree *PDT);

private:
  BasicBlock *Header;
  BasicBlock *Exiting;

};

#endif
