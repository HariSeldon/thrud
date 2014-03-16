#include "thrud/Support/RegionBounds.h"

#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"

RegionBounds::RegionBounds(BasicBlock *header, BasicBlock *exiting)
    : header(header), exiting(exiting) {}

RegionBounds::RegionBounds() {}

BasicBlock *RegionBounds::getHeader() { return header; }
BasicBlock *RegionBounds::getExiting() { return exiting; }
const BasicBlock *RegionBounds::getHeader() const { return header; }
const BasicBlock *RegionBounds::getExiting() const { return exiting; }

void RegionBounds::setHeader(BasicBlock *header) { this->header = header; }
void RegionBounds::setExiting(BasicBlock *exiting) { this->exiting = exiting; }

// Non-member functions.
//------------------------------------------------------------------------------
void listBlocksImpl(const BasicBlock *end, BasicBlock *bb, BlockSet &blockSet) {
  if (bb == end)
    return;

  BlockVector added;
  for (succ_iterator iter = succ_begin(bb), iterEnd = succ_end(bb);
       iter != iterEnd; iter++) {
    BasicBlock *child = *iter;
    if (blockSet.insert(child).second)
      added.push_back(child);
  }

  for (BlockVector::iterator iter = added.begin(), iterEnd = added.end();
       iter != iterEnd; ++iter)
    listBlocksImpl(*iterEnd, *iter, blockSet);
}

// -----------------------------------------------------------------------------
void listBlocks(RegionBounds &bounds, BlockVector &result) {
  BlockSet blockSet;
  blockSet.insert(bounds.getHeader());
  listBlocksImpl(bounds.getExiting(), bounds.getHeader(), blockSet);
  result.assign(blockSet.begin(), blockSet.end());
}

// -----------------------------------------------------------------------------
bool contains(const RegionBounds &bounds, const BasicBlock *block,
              const DominatorTree *dt, const PostDominatorTree *pdt) {
  return dt->dominates(bounds.getHeader(), block) &&
         pdt->dominates(bounds.getExiting(), block);
}
bool contains(const RegionBounds &bounds, const Instruction *inst,
              const DominatorTree *dt, const PostDominatorTree *pdt) {
  return contains(bounds, inst->getParent(), dt, pdt);
}
