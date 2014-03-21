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
#include <functional>
#include <algorithm>

DivergentRegion::DivergentRegion(BasicBlock *header, BasicBlock *exiting,
                                 DominatorTree *dt, PostDominatorTree *pdt)
    : condition(DivergentRegion::ND) {
  bounds.setHeader(header);
  bounds.setExiting(exiting);

  fillRegion(dt, pdt);
}

DivergentRegion::DivergentRegion(BasicBlock *header, BasicBlock *exiting,
                                 DominatorTree *dt, PostDominatorTree *pdt, 
                                 InstVector &alive)
    : condition(DivergentRegion::ND), alive(alive) {
  bounds.setHeader(header);
  bounds.setExiting(exiting);

  fillRegion(dt, pdt);
}

DivergentRegion::DivergentRegion(RegionBounds &bounds, DominatorTree *dt,
                                 PostDominatorTree *pdt)
    : bounds(bounds), condition(DivergentRegion::ND) {
  fillRegion(dt, pdt);
}

BasicBlock *DivergentRegion::getHeader() { return bounds.getHeader(); }
BasicBlock *DivergentRegion::getExiting() { return bounds.getExiting(); }
const BasicBlock *DivergentRegion::getHeader() const {
  return bounds.getHeader();
}
const BasicBlock *DivergentRegion::getExiting() const {
  return bounds.getExiting();
}

void DivergentRegion::setHeader(BasicBlock *Header) {
  bounds.setHeader(Header);
}

void DivergentRegion::setExiting(BasicBlock *Exiting) {
  bounds.setExiting(Exiting);
}

RegionBounds &DivergentRegion::getBounds() { return bounds; }

BlockVector &DivergentRegion::getBlocks() { return blocks; }

InstVector &DivergentRegion::getAlive() { return alive; }

void DivergentRegion::fillRegion(DominatorTree *dt, PostDominatorTree *pdt) {
  blocks.clear();
  bounds.listBlocks(blocks);

  // Try to get rid of this.
  //  // If H is dominated by a block in the region then recompute the bounds.
  //  if (isDominated(bounds.getHeader(), blocks, dt)) {
  //    updateBounds(dt, pdt);
  //  }
}

//------------------------------------------------------------------------------
void DivergentRegion::findAliveValues() {
//  errs() << "DivergentRegion::findAliveValues\n";
  for (BlockVector::iterator iterBlock = blocks.begin(),
                             blockEnd = blocks.end();
       iterBlock != blockEnd; ++iterBlock) {
    BasicBlock *block = *iterBlock;
    for (BasicBlock::iterator iterInst = block->begin(), instEnd = block->end();
         iterInst != instEnd; ++iterInst) {
      Instruction *inst = iterInst;
//      inst->dump();

      // Iterate over the uses of the instruction.
      for (Instruction::use_iterator iterUse = inst->use_begin(),
                                     useEnd = inst->use_end();
           iterUse != useEnd; ++iterUse) {
        if (Instruction *useInst = dyn_cast<Instruction>(*iterUse)) {
//          errs() << "  ";
//          useInst->dump();
          BasicBlock *blockUser = useInst->getParent();
//          errs() << "  ";
//          errs() << blockUser->getName() << "\n";
          // If the user of the instruction is not in the region -> the value is
          // alive.
          if (!contains(*this, blockUser)) {
            alive.push_back(inst);
            break;
          } 
        }
      }
    }
  }
}

//------------------------------------------------------------------------------
void DivergentRegion::updateBounds(DominatorTree *dt, PostDominatorTree *pdt) {
  for (BlockVector::iterator iter = blocks.begin(), iterEnd = blocks.end();
       iter != iterEnd; ++iter) {
    BasicBlock *block = *iter;
    if (dominatesAll(block, blocks, dt))
      bounds.setHeader(block);
    else if (postdominatesAll(block, blocks, pdt))
      bounds.setExiting(block);
  }
}

bool DivergentRegion::isStrict() { return condition == DivergentRegion::EQ; }

void DivergentRegion::setCondition(DivergentRegion::BoundCheck condition) {
  this->condition = condition;
}

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

bool DivergentRegion::areSubregionsDisjoint() {
  BranchInst *branch = dyn_cast<BranchInst>(getHeader()->getTerminator());
  assert(branch->getNumSuccessors() == 2 && "Wrong successor number");

  BasicBlock *first = branch->getSuccessor(0);
  BasicBlock *second = branch->getSuccessor(1);

  BlockVector firstList;
  BlockVector secondList;

  listBlocks(first, getExiting(), firstList);
  listBlocks(second, getExiting(), secondList);

  std::sort(firstList.begin(), firstList.end());
  std::sort(secondList.begin(), secondList.end());

  BlockVector intersection;
  std::set_intersection(firstList.begin(), firstList.end(), secondList.begin(),
                        secondList.end(), std::back_inserter(intersection));

  if (intersection.size() == 1) {
    return intersection[0] == getExiting();
  }
  return false;
}

DivergentRegion* DivergentRegion::clone(const Twine &suffix, DominatorTree *dt,
                                       PostDominatorTree *pdt, Map& valuesMap) {
  Function *function = getHeader()->getParent();
  BasicBlock *newHeader = NULL;
  BasicBlock *newExiting = NULL;

  Map regionBlockMap;
  valuesMap.clear();
  BlockVector newBlocks;
  newBlocks.reserve(blocks.size());
  for (BlockVector::iterator iter = blocks.begin(), iterEnd = blocks.end();
       iter != iterEnd; ++iter) {
    BasicBlock *block = *iter;
    BasicBlock *newBlock = CloneBasicBlock(block, valuesMap, suffix, function, 0);
    regionBlockMap[block] = newBlock;
    newBlocks.push_back(newBlock);

    if (block == getHeader())
      newHeader = newBlock;
    if (block == getExiting())
      newExiting = newBlock; 

    CloneDominatorInfo(block, regionBlockMap, dt);
  }

  // The remapping of the branches must be done at the end of the cloning
  // process.
  for (BlockVector::iterator iter = newBlocks.begin(),
                             iterEnd = newBlocks.end();
       iter != iterEnd; ++iter) {
    applyMap(*iter, regionBlockMap);
    applyMap(*iter, valuesMap);
  }

  return new DivergentRegion(newHeader, newExiting, dt, pdt);
}

//DivergentRegion DivergentRegion::cloneSubregion(const Twine &suffix,
//                                                DominatorTree *dt,
//                                                PostDominatorTree *pdt,
//                                                unsigned int branchIndex,
//                                                Map &valuesMap) {
//  Function *function = getHeader()->getParent();
//  BasicBlock *newHeader = NULL;
//  BranchInst *branch = dyn_cast<BranchInst>(getHeader()->getTerminator());
//  BasicBlock *top = branch->getSuccessor(branchIndex);
//  BlockVector blocks;
//  listBlocks(top, getExiting(), blocks);
//
//  Map regionBlockMap;
//  valuesMap.clear();
//  BlockVector newBlocks;
//  newBlocks.reserve(blocks.size());
//
//  for (BlockVector::iterator iter = blocks.begin(), iterEnd = blocks.end();
//    iter != iterEnd; ++iter) {
//    BasicBlock *block = *iter;
//    if (block == getExiting())
//      continue;
//
//    BasicBlock *newBlock = CloneBasicBlock(block, valuesMap, suffix, function, 0);
//    regionBlockMap[block] = newBlock;
//    newBlocks.push_back(newBlock);
//
//    if (block == top)
//      newHeader = newBlock;
//
//    CloneDominatorInfo(block, regionBlockMap, dt);
//  }
//
//  // The remapping of the branches must be done at the end of the cloning
//  // process.
//  for (BlockVector::iterator iter = newBlocks.begin(),
//                             iterEnd = newBlocks.end();
//       iter != iterEnd; ++iter) {
//    applyMap(*iter, regionBlockMap);
//    applyMap(*iter, valuesMap);
//  }
//
//  return DivergentRegion(newHeader, getExiting(), dt, pdt);
//}

void DivergentRegion::dump() {
  errs() << "Bounds: " << getHeader()->getName() << " -- "
         << getExiting()->getName() << "\n";
  errs() << "Blocks: ";
  for (DivergentRegion::iterator iter = begin(), iterEnd = end();
       iter != iterEnd; ++iter) {
    errs() << (*iter)->getName() << ", ";
  }
  errs() << "\nAlive: ";
  dumpVector(alive);
  errs() << "Condition: ";
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
DivergentRegion::iterator DivergentRegion::begin() { return iterator(*this); }

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
DivergentRegion::iterator::iterator(const iterator &original) {
  blocks = original.blocks;
  currentBlock = original.currentBlock;
}

// Pre-increment.
DivergentRegion::iterator &DivergentRegion::iterator::operator++() {
  toNext();
  return *this;
}
// Post-increment.
DivergentRegion::iterator DivergentRegion::iterator::operator++(int) {
  iterator old(*this);
  ++*this;
  return old;
}

BasicBlock *DivergentRegion::iterator::operator*() const {
  return blocks.at(currentBlock);
}
bool DivergentRegion::iterator::operator!=(const iterator &iter) const {
  return iter.currentBlock != this->currentBlock;
}

void DivergentRegion::iterator::toNext() {
  ++currentBlock;
  if (currentBlock == blocks.size())
    currentBlock = -1;
}

DivergentRegion::iterator DivergentRegion::iterator::end() {
  iterator endIterator;
  endIterator.currentBlock = -1;
  return endIterator;
}

// Const Iterator class.
//------------------------------------------------------------------------------
DivergentRegion::const_iterator::const_iterator() { currentBlock = 0; }
DivergentRegion::const_iterator::const_iterator(const DivergentRegion &region) {
  blocks = region.blocks;
  currentBlock = (blocks.size() == 0) ? -1 : 0;
}
DivergentRegion::const_iterator::const_iterator(
    const const_iterator &original) {
  blocks = original.blocks;
  currentBlock = original.currentBlock;
}

// Pre-increment.
DivergentRegion::const_iterator &DivergentRegion::const_iterator::operator++() {
  toNext();
  return *this;
}
// Post-increment.
DivergentRegion::const_iterator
DivergentRegion::const_iterator::operator++(int) {
  const_iterator old(*this);
  ++*this;
  return old;
}

const BasicBlock *DivergentRegion::const_iterator::operator*() const {
  return blocks.at(currentBlock);
}
bool
DivergentRegion::const_iterator::operator!=(const const_iterator &iter) const {
  return iter.currentBlock != this->currentBlock;
}

void DivergentRegion::const_iterator::toNext() {
  ++currentBlock;
  if (currentBlock == blocks.size())
    currentBlock = -1;
}

DivergentRegion::const_iterator DivergentRegion::const_iterator::end() {
  const_iterator endIterator;
  endIterator.currentBlock = -1;
  return endIterator;
}

// Non member functions.
//------------------------------------------------------------------------------
RegionBounds *getExitingAndExit(DivergentRegion &region) {
  BasicBlock *exiting = region.getExiting();
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

// -----------------------------------------------------------------------------
bool contains(const DivergentRegion &region, const Instruction *inst) {
  return contains(region, inst->getParent());
}

bool containsInternally(const DivergentRegion &region,
                        const Instruction *inst) {
  return containsInternally(region, inst->getParent());
}

bool contains(const DivergentRegion &region, const BasicBlock *block) {
  DivergentRegion::const_iterator result =
      std::find(region.begin(), region.end(), block);
  return result != region.end();
}

bool containsInternally(const DivergentRegion &region,
                        const BasicBlock *block) {
  for (DivergentRegion::const_iterator iter = region.begin(),
                                       iterEnd = region.end();
       iter != iterEnd; ++iter) {
    const BasicBlock *currentBlock = (*iter);

    if (currentBlock == region.getHeader() ||
        currentBlock == region.getExiting())
      continue;

    if (block == currentBlock)
      return true;
  }
  return false;
}

bool containsInternally(const DivergentRegion &region,
                       const DivergentRegion *innerRegion) {
  return containsInternally(region, innerRegion->getHeader()) &&
         containsInternally(region, innerRegion->getExiting());
}
