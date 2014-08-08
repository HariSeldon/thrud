#include "thrud/ThreadVectorizing/ThreadVectorizing.h"

#include "llvm/Analysis/LoopInfo.h"

#include "llvm/Transforms/Utils/BasicBlockUtils.h"

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
  CoarseningMap aliveMap;
  initAliveMap(region, aliveMap);
  replicateRegionImpl(region, aliveMap);
//  updatePlaceholdersWithAlive(aliveMap);
}

//------------------------------------------------------------------------------
void ThreadVectorizing::initAliveMap(DivergentRegion *region,
                                    CoarseningMap &aliveMap) {
  InstVector &aliveInsts = region->getAlive();
  for (InstVector::iterator iter = aliveInsts.begin(),
                            iterEnd = aliveInsts.end();
       iter != iterEnd; ++iter) {
    aliveMap.insert(std::pair<Instruction *, InstVector>(*iter, InstVector()));
  }
}

//------------------------------------------------------------------------------
void ThreadVectorizing::replicateRegionImpl(DivergentRegion *region,
                                            CoarseningMap &aliveMap) {
  BasicBlock *pred = getPredecessor(region, loopInfo);
  BasicBlock *topInsertionPoint = region->getExiting();
  BasicBlock *bottomInsertionPoint = getExit(*region);

//  ValueVector result;
//  InstVector &aliveInsts = region->getAlive();
//  result.reserve(aliveInsts.size());
//  for (InstVector::iterator iter = aliveInsts.begin(),
//                            iterEnd = aliveInsts.end();
//       iter != iterEnd; ++iter) {
//    result.push_back(
//        UndefValue::get(VectorType::get((*iter)->getType(), width)));
//  }

  region->findIncomingValues();
  InstVector &incomingInst = region->getIncoming();

  BasicBlock *firstRegionHeader = NULL;
  BasicBlock *lastRegionExiting = NULL;

  // Replicate the region.
  for (unsigned int index = 0; index < width; ++index) {
    Map valueMap;
    DivergentRegion *newRegion =
        region->clone(".cf" + Twine(index + 1), dt, pdt, valueMap);
    applyVectorMapToRegion(*newRegion, incomingInst, index);

    if(index == 0) {
      firstRegionHeader = newRegion->getHeader();
    }
    if(index == width - 1) {
      lastRegionExiting = newRegion->getExiting();
    }

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

//    // Set the ir just before the branch out of the region
//    irBuilder->SetInsertPoint(newRegion->getExiting()->getTerminator());
//
//    for (unsigned int aliveIndex = 0; aliveIndex < aliveInsts.size();
//         ++aliveIndex) {
//      Value *newValue = valueMap[aliveInsts[aliveIndex]];
//      result[aliveIndex] = irBuilder->CreateInsertElement(
//          result[aliveIndex], newValue, irBuilder->getInt32(index), "inserted");
//    }

    delete newRegion;
    updateAliveMap(aliveMap, valueMap);
  }

  createAliveVectors(lastRegionExiting, aliveMap);

  // Remove the original region from the function.
  changeBlockTarget(pred, firstRegionHeader);

  removeOldRegion(region);

//  for (unsigned int aliveIndex = 0; aliveIndex < aliveInsts.size();
//       ++aliveIndex) {
//    vectorMap[aliveInsts[aliveIndex]] = result[aliveIndex];
//  }
}

//------------------------------------------------------------------------------
void ThreadVectorizing::createAliveVectors(BasicBlock *block,
                                           CoarseningMap &aliveMap) {
  irBuilder->SetInsertPoint(block->getTerminator());

  for (CoarseningMap::iterator mapIter = aliveMap.begin(),
                               mapEnd = aliveMap.end();
       mapIter != mapEnd; ++mapIter) {
    Instruction *alive = mapIter->first;
    InstVector &aliveInsts = mapIter->second; 

    Value *result = UndefValue::get(VectorType::get(alive->getType(), width));
    for(unsigned int index = 0; index < aliveInsts.size(); ++index) {
      Instruction *alive = aliveInsts[index]; 
      result = irBuilder->CreateInsertElement(
          result, alive, irBuilder->getInt32(index), "region.alive");
    }
    vectorMap[alive] = result;
  }
}

//------------------------------------------------------------------------------
void ThreadVectorizing::updateAliveMap(CoarseningMap &aliveMap, Map &regionMap) {
  for (CoarseningMap::iterator mapIter = aliveMap.begin(),
                               mapEnd = aliveMap.end();
       mapIter != mapEnd; ++mapIter) {
    InstVector &aliveInsts = mapIter->second;
    Value *value = regionMap[mapIter->first];
    assert(value != NULL && "Missing alive value in region map");
    aliveInsts.push_back(dyn_cast<Instruction>(value));
  }
}

//------------------------------------------------------------------------------
void ThreadVectorizing::removeOldRegion(DivergentRegion *region) {

  // This does not work. The blocks are still in used before the removal.
  for (DivergentRegion::iterator blockIter = region->begin(),
                                 blockIterEnd = region->end();
       blockIter != blockIterEnd; ++blockIter) {
    BasicBlock *block = *blockIter;
    TerminatorInst *blockTerm = block->getTerminator();

    // Loop through all of our successors and make sure they know that one
    // of their predecessors is going away.
    for (unsigned i = 0, e = blockTerm->getNumSuccessors(); i != e; ++i)
      blockTerm->getSuccessor(i)->removePredecessor(block);

    // Zap all the instructions in the block.
    while (!block->empty()) {
      Instruction &I = block->back();
      // If this instruction is used, replace uses with an arbitrary value.
      // Because control flow can't get here, we don't care what we replace the
      // value with.  Note that since this block is unreachable, and all values
      // contained within it must dominate their uses, that all uses will
      // eventually be removed (they are themselves dead).
      if (!I.use_empty())
        I.replaceAllUsesWith(UndefValue::get(I.getType()));
      block->getInstList().pop_back();
    }
  }

  for (DivergentRegion::iterator blockIter = region->begin(),
                                 blockIterEnd = region->end();
       blockIter != blockIterEnd; ++blockIter) {
    BasicBlock *block = *blockIter;
    block->eraseFromParent();
  }
}

//------------------------------------------------------------------------------
void ThreadVectorizing::applyVectorMapToRegion(DivergentRegion &region,
                                               InstVector &incoming,
                                               unsigned int index) {

  // Perform the remapping on the incoming values only.

  for (DivergentRegion::iterator blockIter = region.begin(),
                                 blockIterEnd = region.end();
       blockIter != blockIterEnd; ++blockIter) {
    BasicBlock *block = *blockIter;
    for (BasicBlock::iterator instIter = block->begin(),
                              instIterEnd = block->end();
         instIter != instIterEnd; ++instIter) {
      Instruction *inst = instIter;
      irBuilder->SetInsertPoint(inst);

      for (unsigned int opIndex = 0, opEnd = inst->getNumOperands();
           opIndex != opEnd; ++opIndex) {
        Instruction *operand = dyn_cast<Instruction>(inst->getOperand(opIndex));
        if (operand == NULL)
          continue;

        if (!isPresent(operand, incoming)) {
          continue;
        }

        Value *vectorValue = getVectorValue(operand);
        Value *newOperand = irBuilder->CreateExtractElement(
            vectorValue, irBuilder->getInt32(index), "extracted");

        inst->setOperand(opIndex, newOperand);
      }
    }
  }
}

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
