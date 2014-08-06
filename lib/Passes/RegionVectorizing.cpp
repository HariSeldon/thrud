#include "thrud/ThreadVectorizing/ThreadVectorizing.h"

// -----------------------------------------------------------------------------
void ThreadVectorizing::replicateRegion(DivergentRegion *region) {
  assert(dt->dominates(region->getHeader(), region->getExiting()) &&
         "Header does not dominates Exiting");
  assert(pdt->dominates(region->getExiting(), region->getHeader()) &&
         "Exiting does not post dominate Header");

  switch (divRegionOption) {
  case FullReplication: {
    replicateRegionClassic(region);
    break;
  }
  case TrueBranchMerging: {
    replicateRegionTrueMerging(region);
    break;
  }
  case FalseBranchMerging: {
    replicateRegionFalseMerging(region);
    break;
  }
  case FullMerging: {
    replicateRegionFullMerging(region);
    break;
  }
  }
}

//------------------------------------------------------------------------------
void ThreadVectorizing::replicateRegionClassic(DivergentRegion *region) {
//  V2VMap aliveMap;
//  initAliveMap(region, aliveMap);
  replicateRegionImpl(region);
//  updatePlaceholdersWithAlive(aliveMap);
}

//------------------------------------------------------------------------------
void ThreadVectorizing::replicateRegionImpl(DivergentRegion *region) {
  BasicBlock *pred = getPredecessor(region, loopInfo);
  BasicBlock *topInsertionPoint = region->getExiting();
  BasicBlock *bottomInsertionPoint = getExit(*region);

  ValueVector result;
  InstVector &aliveInsts = region->getAlive();
  result.reserve(aliveInsts.size());
  for (InstVector::iterator iter = aliveInsts.begin(),
                            iterEnd = aliveInsts.end();
       iter != iterEnd; ++iter) {
    result.push_back(
        UndefValue::get(VectorType::get((*iter)->getType(), width)));
  }

  region->dump();
  applyVectorMapToRegion(*region, 0);

  // Replicate the region.
  for (unsigned int index = 0; index < width - 1; ++index) {
    Map valueMap;
    DivergentRegion *newRegion =
        region->clone(".cf" + Twine(index + 2), dt, pdt, valueMap);
    applyVectorMapToRegion(*newRegion, index + 1);

    // Connect the region to the CFG.
    changeBlockTarget(topInsertionPoint, newRegion->getHeader());
    changeBlockTarget(newRegion->getExiting(), bottomInsertionPoint);

    // Update the phi nodes of the newly inserted header.
    remapBlocksInPHIs(newRegion->getHeader(), pred, topInsertionPoint);
    // Update the phi nodes in the exit block.
    remapBlocksInPHIs(bottomInsertionPoint, topInsertionPoint,
                      newRegion->getExiting());

    topInsertionPoint = newRegion->getExiting();
    bottomInsertionPoint = getExit(*newRegion);

    // Set the ir just before the branch out of the region
    irBuilder->SetInsertPoint(newRegion->getExiting()->getTerminator());

    for (unsigned int aliveIndex = 0; aliveIndex < aliveInsts.size();
         ++aliveIndex) {
      Value *newValue = valueMap[aliveInsts[aliveIndex]];
      result[aliveIndex] = irBuilder->CreateInsertElement(
          result[aliveIndex], newValue, irBuilder->getInt32(index), "inserted");
    }

    delete newRegion;
  }

  for (unsigned int aliveIndex = 0; aliveIndex < aliveInsts.size();
       ++aliveIndex) {
    vectorMap[aliveInsts[aliveIndex]] = result[aliveIndex];
  }
}

//------------------------------------------------------------------------------
void ThreadVectorizing::applyVectorMapToRegion(DivergentRegion &region,
                                                  unsigned int index) {
  for (DivergentRegion::iterator iter = region.begin(), iterEnd = region.end();
       iter != iterEnd; ++iter) {
    BasicBlock *block = *iter;
    applyVectorMapToBlock(block, index);
  }
}

//------------------------------------------------------------------------------
void ThreadVectorizing::applyVectorMapToBlock(BasicBlock *block, unsigned int index) {
  for (BasicBlock::iterator iter = block->begin(), iterEnd = block->end();
       iter != iterEnd; ++iter) {
    applyVectorMapToInst(iter, index);
  }
}

//------------------------------------------------------------------------------
void ThreadVectorizing::applyVectorMapToInst(Instruction *inst,
                                             unsigned int index) {

  errs() << "applyVectorMapToInst: ";
  inst->dump();
  irBuilder->SetInsertPoint(inst);
  for (unsigned int opIndex = 0, opEnd = inst->getNumOperands();
       opIndex != opEnd; ++opIndex) {
    Instruction *operand = dyn_cast<Instruction>(inst->getOperand(opIndex));
    if (operand == NULL)
      continue;
    Value *vectorValue = getVectorValue(operand);
    Value *newOperand = irBuilder->CreateExtractElement(
        vectorValue, irBuilder->getInt32(index), "extracted");

    inst->setOperand(opIndex, newOperand);
  }
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void ThreadVectorizing::replicateRegionFullMerging(DivergentRegion *region) {}
void ThreadVectorizing::replicateRegionFalseMerging(DivergentRegion *region) {
  replicateRegionMerging(region, 1);
}
void ThreadVectorizing::replicateRegionTrueMerging(DivergentRegion *region) {
  replicateRegionMerging(region, 0);
}
void ThreadVectorizing::replicateRegionMerging(DivergentRegion *region,
                                               unsigned int branchIndex) {}
