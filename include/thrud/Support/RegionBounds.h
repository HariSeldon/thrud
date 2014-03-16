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

class RegionBounds {
public:
  RegionBounds(BasicBlock *header, BasicBlock *exiting);
  RegionBounds();

public:
  BasicBlock *getHeader();
  BasicBlock *getExiting();
  const BasicBlock *getHeader() const;
  const BasicBlock *getExiting() const;

  void setHeader(BasicBlock *Header);
  void setExiting(BasicBlock *Exiting);

private:
  BasicBlock *header;
  BasicBlock *exiting;

};

// Non-member functions.
//------------------------------------------------------------------------------
void listBlocksImpl(const BasicBlock *end, BasicBlock *bb, BlockSet &blockSet);
void listBlocks(RegionBounds &bounds, BlockVector &result);

bool contains(const RegionBounds &bounds, const BasicBlock *bb,
              const DominatorTree *dt, const PostDominatorTree *pdt);
bool contains(const RegionBounds &bounds, const Instruction *inst,
              const DominatorTree *dt, const PostDominatorTree *pdt);

#endif
