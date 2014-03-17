#include "thrud/Support/RegionBounds.h"

#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"

#include "thrud/Support/Utils.h"

RegionBounds::RegionBounds(BasicBlock *header, BasicBlock *exiting)
    : header(header), exiting(exiting) {}

RegionBounds::RegionBounds() {}

BasicBlock *RegionBounds::getHeader() { return header; }
BasicBlock *RegionBounds::getExiting() { return exiting; }
const BasicBlock *RegionBounds::getHeader() const { return header; }
const BasicBlock *RegionBounds::getExiting() const { return exiting; }

void RegionBounds::setHeader(BasicBlock *header) { this->header = header; }
void RegionBounds::setExiting(BasicBlock *exiting) { this->exiting = exiting; }

void RegionBounds::listBlocks(BlockVector &result) const {
  // Perform traversal of the tree starting from header and stopping at exiting.
  BlockDeque worklist;
  worklist.push_back(header);

  while (!worklist.empty()) {
    BasicBlock *block = worklist.front();
    worklist.pop_front();

    result.push_back(block);

    // Iterate over block's children.
    for (succ_iterator iter = succ_begin(block), iterEnd = succ_end(block);
         iter != iterEnd; ++iter) {
      BasicBlock *child = *iter;
      if (!isPresent(child, result) && !isPresent(child, worklist) &&
          child != exiting) {
        worklist.push_back(child);
      }
    }
  }

  // Insert the exiting block.
  result.push_back(exiting);
}
