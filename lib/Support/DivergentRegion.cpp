#include "thrud/Support/DivergentRegion.h"

#include "thrud/Support/RegionBounds.h"
#include "thrud/Support/Utils.h"

#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <numeric>

DivergentRegion::DivergentRegion(BasicBlock *header, BasicBlock *exiting)
    : condition(DivergentRegion::ND) {
  bounds.setHeader(header);
  bounds.setExiting(exiting);
  updateRegion();
}

DivergentRegion::DivergentRegion(RegionBounds &bounds)
    : bounds(bounds), condition(DivergentRegion::ND) {
  updateRegion();
}

BasicBlock *DivergentRegion::getHeader() { return bounds.getHeader(); }
BasicBlock *DivergentRegion::getExiting() { return bounds.getExiting(); }
const BasicBlock *DivergentRegion::getHeader() const { return bounds.getHeader(); }
const BasicBlock *DivergentRegion::getExiting() const { return bounds.getExiting(); }

void DivergentRegion::setHeader(BasicBlock *Header) {
  bounds.setHeader(Header);
}

void DivergentRegion::setExiting(BasicBlock *Exiting) {
  bounds.setExiting(Exiting);
}

RegionBounds &DivergentRegion::getBounds() { return bounds; }

BlockVector &DivergentRegion::getBlocks() { return blocks; }

void DivergentRegion::fillRegion(DominatorTree *dt, PostDominatorTree *pdt) {
//  blocks = listBlocks(bounds);
//
//  // If H is dominated by a block in the region than recompute the bounds.
//  if (IsDominated(bounds.getHeader(), *blocks, DT)) {
//    bounds = FindBounds(*blocks, DT, PDT);
//  }
}

void DivergentRegion::updateRegion() { listBlocks(bounds, blocks); }

//------------------------------------------------------------------------------
// Checks if the header of the region dominates all the blocks in the region.
bool DivergentRegion::PerformHeaderCheck(DominatorTree *DT) {
  return DominatesAll(bounds.getHeader(), blocks, DT);
}

//------------------------------------------------------------------------------
bool DivergentRegion::IsStrict() { return condition == DivergentRegion::EQ; }

//------------------------------------------------------------------------------
void DivergentRegion::setCondition(DivergentRegion::BoundCheck condition) {
  this->condition = condition;
}

//------------------------------------------------------------------------------
DivergentRegion::BoundCheck DivergentRegion::getCondition() const {
  return condition;
}

//------------------------------------------------------------------------------
void DivergentRegion::analyze() {
  BasicBlock *header = getHeader();
  if (BranchInst *Branch = dyn_cast<BranchInst>(header->getTerminator())) {
    Value *Cond = Branch->getCondition();
    if (CmpInst *Cmp = dyn_cast<CmpInst>(Cond)) {
      setCondition(IsEquals(Cmp->getPredicate()) ? EQ : ND);
      return;
    }
  }
  setCondition(ND);
}

//------------------------------------------------------------------------------
void DivergentRegion::dump() {
  errs() << "Bounds: " << getHeader()->getName() << " -- "
         << getExiting()->getName() << "\n";
  errs() << "Blocks: ";
  for (DivergentRegion::iterator iter = begin(), iterEnd = end();
       iter != iterEnd; ++iter) {
    errs() << (*iter)->getName() << " -- ";
  }
  errs() << "\nCondition: ";
  switch (getCondition()) {
  case DivergentRegion::EQ:
    errs() << "EQUALS\n";
    break;
  case DivergentRegion::ND:
    errs() << "ND\n";
    break;
  }
}

//------------------------------------------------------------------------------
unsigned int addSizes(unsigned int partialSum, BasicBlock *block) {
//  if (block == bounds.getHeader() || block == bounds.getExiting())
//    return;
  return partialSum + block->size();
}

unsigned int DivergentRegion::size() {
  return std::accumulate(blocks.begin(), blocks.end(), 0, addSizes);
}

//------------------------------------------------------------------------------
DivergentRegion::iterator DivergentRegion::begin() {
  return iterator(*this);  
}

DivergentRegion::iterator DivergentRegion::end() {
  return DivergentRegion::iterator::end();
}

DivergentRegion::const_iterator DivergentRegion::begin() const {
  return const_iterator(*this);  
}

DivergentRegion::const_iterator DivergentRegion::end() const {
  return DivergentRegion::const_iterator::end();
}

// Iterator class.
//------------------------------------------------------------------------------
DivergentRegion::iterator::iterator() { currentBlock = 0; }
DivergentRegion::iterator::iterator(const DivergentRegion &region) {
  blocks = region.blocks;
  currentBlock = (blocks.size() == 0) ? -1 : 0;
}
DivergentRegion::iterator::iterator(const iterator& original) {
  blocks = original.blocks;
  currentBlock = original.currentBlock;
}

// Pre-increment.
DivergentRegion::iterator& DivergentRegion::iterator::operator++() {
  toNext();
  return *this;
}
// Post-increment.
DivergentRegion::iterator DivergentRegion::iterator::operator++(int) {
  iterator old(*this);
  ++*this;
  return old;
}

BasicBlock* DivergentRegion::iterator::operator*() const {
  return blocks.at(currentBlock);
}
bool DivergentRegion::iterator::operator!=(const iterator& iter) const {
  return iter.currentBlock != this->currentBlock;
}

void DivergentRegion::iterator::toNext() {
  ++currentBlock;
  if (currentBlock == blocks.size()) currentBlock = -1;
}

DivergentRegion::iterator DivergentRegion::iterator::end() {
  iterator endIterator;
  endIterator.currentBlock = -1;
  return endIterator;
}

// Iterator class.
//------------------------------------------------------------------------------
DivergentRegion::const_iterator::const_iterator() { currentBlock = 0; }
DivergentRegion::const_iterator::const_iterator(const DivergentRegion &region) {
  blocks = region.blocks;
  currentBlock = (blocks.size() == 0) ? -1 : 0;
}
DivergentRegion::const_iterator::const_iterator(const const_iterator& original) {
  blocks = original.blocks;
  currentBlock = original.currentBlock;
}

// Pre-increment.
DivergentRegion::const_iterator& DivergentRegion::const_iterator::operator++() {
  toNext();
  return *this;
}
// Post-increment.
DivergentRegion::const_iterator DivergentRegion::const_iterator::operator++(int) {
  const_iterator old(*this);
  ++*this;
  return old;
}

const BasicBlock* DivergentRegion::const_iterator::operator*() const {
  return blocks.at(currentBlock);
}
bool DivergentRegion::const_iterator::operator!=(const const_iterator& iter) const {
  return iter.currentBlock != this->currentBlock;
}

void DivergentRegion::const_iterator::toNext() {
  ++currentBlock;
  if (currentBlock == blocks.size()) currentBlock = -1;
}

DivergentRegion::const_iterator DivergentRegion::const_iterator::end() {
  const_iterator endIterator;
  endIterator.currentBlock = -1;
  return endIterator;
}

// Non member functions.
//------------------------------------------------------------------------------
RegionBounds *getExtingExit(DivergentRegion *region) {
  BasicBlock *exiting = region->getExiting();
  TerminatorInst *terminator = exiting->getTerminator();
  assert(terminator->getNumSuccessors() == 1 &&
         "Divergent region must have one successor only");
  BasicBlock *exit = terminator->getSuccessor(0);
  return new RegionBounds(exiting, exit);
}

//------------------------------------------------------------------------------
BasicBlock *getPredecessor(DivergentRegion *region, LoopInfo *loopInfo) {
  BasicBlock *header = region->getHeader();
  BasicBlock *predecessor = header->getSinglePredecessor();
  if (predecessor == NULL) {
    Loop *loop = loopInfo->getLoopFor(header);
    predecessor = loop->getLoopPredecessor();
  }
  assert(predecessor != NULL &&
         "Region header does not have a single predecessor");
  return predecessor;
}

//------------------------------------------------------------------------------
// 'map' will contain the mapping between the old and the new instructions in
// the region.
// FIXME: iter might not need the whole map, but only the live values out of the
// region.
RegionBounds cloneRegion(RegionBounds &bounds, const Twine &suffix,
                         DominatorTree *dt) {
  RegionBounds newBounds;
  BlockVector newBlocks;
  BlockVector blocks;

  listBlocks(bounds, blocks);

  // Map used to update the branches inside the region.
  Map regionBlockMap;
  Map blocksMap;
  Function *function = bounds.getHeader()->getParent();
  for (BlockVector::iterator iter = blocks.begin(), iterEnd = blocks.end();
       iter != iterEnd; ++iter) {

    BasicBlock *bb = *iter;
    BasicBlock *newBB = CloneBasicBlock(bb, blocksMap, suffix, function, 0);
    regionBlockMap[bb] = newBB;
    newBlocks.push_back(newBB);

    // Save the head and the tail of the cloned block region.
    if (bb == bounds.getHeader())
      newBounds.setHeader(newBB);
    if (bb == bounds.getExiting())
      newBounds.setExiting(newBB);

    CloneDominatorInfo(bb, regionBlockMap, dt);
  }

  // The remapping of the branches must be done at the end of the cloning
  // process.
  for (BlockVector::iterator iter = newBlocks.begin(),
                             iterEnd = newBlocks.end();
       iter != iterEnd; ++iter) {
    applyMap(*iter, regionBlockMap);
    //    applyMap(*iter, map);
    //    applyMap(*iter, ToApply);
  }
  return newBounds;
}

// -----------------------------------------------------------------------------
bool contains(const DivergentRegion &region, const Instruction *inst) {
  return contains(region, inst->getParent());
}

bool containsInternally(const DivergentRegion &region, const Instruction *inst) {
  return containsInternally(region, inst->getParent());
}

bool contains(const DivergentRegion &region, const BasicBlock *block) {
  DivergentRegion::const_iterator result =
      std::find(region.begin(), region.end(), block);
  return result != region.end();
}

bool containsInternally(const DivergentRegion &region, const BasicBlock *block) {
  for (DivergentRegion::const_iterator iter = region.begin(), iterEnd = region.end();
       iter != iterEnd; ++iter) {
    const BasicBlock *currentBlock = (*iter);

    if (currentBlock == region.getHeader() || currentBlock == region.getExiting())
      continue;

    if (block == currentBlock)
      return true;
  }
  return false;
}
