#include "thrud/Support/RegionBounds.h"

#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"

RegionBounds::RegionBounds(BasicBlock *Header, BasicBlock *Exiting)
    : Header(Header), Exiting(Exiting) {}

RegionBounds::RegionBounds() {}

BasicBlock *RegionBounds::getHeader() { return Header; }

BasicBlock *RegionBounds::getExiting() { return Exiting; }

void RegionBounds::setHeader(BasicBlock *Header) { this->Header = Header; }

void RegionBounds::setExiting(BasicBlock *Exiting) { this->Exiting = Exiting; }

bool RegionBounds::Contains(const BasicBlock *BB, const DominatorTree *DT,
                            const PostDominatorTree *PDT) {
  return DT->dominates(Header, BB) && PDT->dominates(Exiting, BB);
}
bool RegionBounds::Contains(const Instruction *Inst, const DominatorTree *DT,
                            const PostDominatorTree *PDT) {
  const BasicBlock *BB = Inst->getParent();
  return Contains(BB, DT, PDT);
}
