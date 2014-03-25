#include "thrud/ThreadCoarsening/ThreadCoarsening.h"

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/Utils.h"

#include "llvm/Analysis/LoopInfo.h"

#include "llvm/IR/Module.h"

#include "llvm/Transforms/Utils/Cloning.h"

#include "thrud/Support/Utils.h"

//------------------------------------------------------------------------------
void ThreadCoarsening::replicateRegion(DivergentRegion *region) {
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
void ThreadCoarsening::replicateRegionClassic(DivergentRegion *region) {
  CoarseningMap aliveMap;
  initAliveMap(region, aliveMap);
  replicateRegionImpl(region, aliveMap);
  updatePlaceholdersWithAlive(aliveMap);
}

//------------------------------------------------------------------------------
void ThreadCoarsening::initAliveMap(DivergentRegion *region,
                                    CoarseningMap &aliveMap) {
  InstVector &aliveInsts = region->getAlive();
  for (InstVector::iterator iter = aliveInsts.begin(),
                            iterEnd = aliveInsts.end();
       iter != iterEnd; ++iter) {
    aliveMap.insert(std::pair<Instruction *, InstVector>(*iter, InstVector()));
  }
}

//------------------------------------------------------------------------------
void ThreadCoarsening::replicateRegionImpl(DivergentRegion *region,
                                           CoarseningMap &aliveMap) {
  BasicBlock *pred = getPredecessor(region, loopInfo);
  BasicBlock *topInsertionPoint = region->getExiting();
  BasicBlock *bottomInsertionPoint = getExit(*region);

  // Replicate the region.
  for (unsigned int index = 0; index < factor - 1; ++index) {
    Map valueMap;
    DivergentRegion *newRegion =
        region->clone(".cf" + Twine(index + 2), dt, pdt, valueMap);
    applyCoarseningMap(*newRegion, index);

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

    delete newRegion;
    updateAliveMap(aliveMap, valueMap);
  }
}

//------------------------------------------------------------------------------
void ThreadCoarsening::updateAliveMap(CoarseningMap &aliveMap, Map &regionMap) {
  for (CoarseningMap::iterator mapIter = aliveMap.begin(),
                               mapEnd = aliveMap.end();
       mapIter != mapEnd; ++mapIter) {
    InstVector &coarsenedInsts = mapIter->second;
    Value *value = regionMap[mapIter->first];
    assert(value != NULL && "Missing alive value in region map");
    coarsenedInsts.push_back(dyn_cast<Instruction>(value));
  }
}

//------------------------------------------------------------------------------
void ThreadCoarsening::updatePlaceholdersWithAlive(CoarseningMap &aliveMap) {
  // Go through region alive values.
  for (CoarseningMap::iterator mapIter = aliveMap.begin(),
                               mapEnd = aliveMap.end();
       mapIter != mapEnd; ++mapIter) {
    Instruction *alive = mapIter->first;
    InstVector &coarsenedInsts = mapIter->second;

    updatePlaceholderMap(alive, coarsenedInsts);
  }
}

//------------------------------------------------------------------------------
void ThreadCoarsening::replicateRegionFullMerging(DivergentRegion *region) {}

//------------------------------------------------------------------------------
void ThreadCoarsening::replicateRegionFalseMerging(DivergentRegion *region) {
  replicateRegionMerging(region, 1);
}

//------------------------------------------------------------------------------
void ThreadCoarsening::replicateRegionTrueMerging(DivergentRegion *region) {
  replicateRegionMerging(region, 0);
}

//------------------------------------------------------------------------------
void ThreadCoarsening::replicateRegionMerging(DivergentRegion *region,
                                              unsigned int branchIndex) {
//  errs() << "ThreadCoarsening::replicateRegionMerging\n";

  if (!region->areSubregionsDisjoint()) {
    assert(false && "Region merging is not possible");
    return replicateRegionClassic(region);
  }

  if (loopInfo->isLoopHeader(region->getHeader())) {
//    assert(false && "Region merging is not possible, the region is a loop");
    return replicateRegionClassic(region);
  }

  // Identify exiting block of merged subregion.
  BasicBlock *mergedSubregionExiting = getSubregionExiting(region, branchIndex);
  // Identify the alive values defined in the merged subregion.
  InstVector aliveFromMerged;
  getSubregionAlive(region, mergedSubregionExiting, aliveFromMerged);

  // Create the block that contains the overall branch.
  BasicBlock *topBlock = createTopBranch(region);
  // Clone the whole region.
  Map firstRegionMap;
  DivergentRegion *firstRegion =
      createCascadingFirstRegion(region, topBlock, branchIndex, firstRegionMap);
  // Remove redundant blocks.
  removeRedundantBlocks(region, branchIndex);

  // Replicate instructions in branchIndex region.
  InstVector insts = sdda->getDivInsts(region, branchIndex);
  std::for_each(
      insts.begin(), insts.end(),
      std::bind1st(std::mem_fun(&ThreadCoarsening::replicateInst), this));

  RegionVector regions = sdda->getDivRegions(region, branchIndex);
  std::for_each(
      regions.begin(), regions.end(),
      std::bind1st(std::mem_fun(&ThreadCoarsening::replicateRegion), this));

  BasicBlock *topInsertionPoint = firstRegion->getExiting();
  BasicBlock *bottomInsertionPoint = region->getExiting();
  BasicBlock *replicatedExiting = firstRegion->getExiting();

  // Initialize aliveMap.
  CoarseningMap aliveMap;
  initAliveMap(firstRegion, aliveMap);

  // Replicate the region.
  for (unsigned int index = 0; index < factor - 1; ++index) {
    Map valueMap;
    DivergentRegion *newRegion =
        firstRegion->clone(".cf" + Twine(index + 2), dt, pdt, valueMap);
    applyCoarseningMap(*newRegion, index);

    // Connect the region to the CFG.
    changeBlockTarget(topInsertionPoint, newRegion->getHeader());
    changeBlockTarget(newRegion->getExiting(), bottomInsertionPoint);

    // Update the phi nodes in the exit block.
    remapBlocksInPHIs(bottomInsertionPoint, region->getExiting(),
                      newRegion->getExiting());

    topInsertionPoint = newRegion->getExiting();
    bottomInsertionPoint = region->getExiting();

    replicatedExiting = newRegion->getExiting();

    updateAliveMap(aliveMap, valueMap);
  }

  // Add new phi nodes to the exit block.
  updateExitPhiNodes(region->getExiting(), mergedSubregionExiting,
                     aliveFromMerged, firstRegionMap, replicatedExiting,
                     aliveMap);

  // Remove old region header.
  region->getHeader()->getTerminator()->eraseFromParent();
  region->getHeader()->eraseFromParent();
}

// -----------------------------------------------------------------------------
void ThreadCoarsening::removeRedundantBlocks(DivergentRegion *region,
                                             unsigned int branchIndex) {
  BranchInst *branch =
      dyn_cast<BranchInst>(region->getHeader()->getTerminator());
  // Indetify blocks to remove.
  BlockVector toRemove;
  listBlocks(branch->getSuccessor(1 - branchIndex), region->getExiting(),
             toRemove);
  toRemove.erase(find(toRemove.begin(), toRemove.end(), region->getExiting()));

  // Null all the values of the blocks.
  for (BlockVector::iterator iter = toRemove.begin(), iterEnd = toRemove.end();
       iter != iterEnd; ++iter) {
    BasicBlock *block = *iter;
    block->replaceAllUsesWith(region->getExiting());
    for (BasicBlock::iterator instIter = block->begin(), instEnd = block->end();
         instIter != instEnd; ++instIter) {
      instIter->replaceAllUsesWith(UndefValue::get(instIter->getType()));
    }
  }

  // Erase the blocks.
  for (BlockVector::iterator iter = toRemove.begin(), iterEnd = toRemove.end();
       iter != iterEnd; ++iter) {
    BasicBlock *block = *iter;
    block->eraseFromParent();
  }
}

// -----------------------------------------------------------------------------
Instruction *getAlivePastPhi(Instruction *inst, CoarseningMap &aliveMap) {
//  errs() << "getAlivePastPhi\n";
//  inst->dump();
//  dumpCoarseningMap(aliveMap);

  CoarseningMap::iterator aliveIter = aliveMap.find(inst);
  if (aliveIter != aliveMap.end()) {
    return aliveIter->first;
  } else {
    for (CoarseningMap::iterator iter = aliveMap.begin(),
                                 iterEnd = aliveMap.end();
         iter != iterEnd; ++iter) {
      Instruction *mapInst = iter->first;
      if (PHINode *phi = dyn_cast<PHINode>(mapInst)) {
//        phi->dump();
        unsigned int argNumber = phi->getNumIncomingValues();
        for (unsigned int index = 0; index < argNumber; ++index) {
          Value *phiValue = phi->getIncomingValue(index);
//          phiValue->dump();
          if (phiValue == inst) {
            return phi;
          }
        }
      }
    }
  }
  return NULL;
}

// -----------------------------------------------------------------------------
void ThreadCoarsening::updateExitPhiNodes(BasicBlock *block,
                                          BasicBlock *mergedSubregionExiting,
                                          InstVector &aliveFromMerged,
                                          Map &cloningMap,
                                          BasicBlock *replicatedExiting,
                                          CoarseningMap &aliveMap) {

//  errs() << "ThreadCoarsening::updateExitPhiNodes\n";
//  dumpCoarseningMap(aliveMap);
//  dumpCoarseningMap(cMap);
//  dumpCoarseningMap(phMap);

  PhiVector phis;

  for (InstVector::iterator aliveIter = aliveFromMerged.begin(),
                            aliveIterEnd = aliveFromMerged.end();
       aliveIter != aliveIterEnd; ++aliveIter) {
    Instruction *inst = *aliveIter;
    Instruction *clonedInst = dyn_cast<Instruction>(cloningMap[inst]);
    Instruction *clonedAliveInst = getAlivePastPhi(clonedInst, aliveMap);
//    clonedAliveInst->dump();
//    errs() << "inst:";
//    inst->dump();
//    errs() << "clonedInst:";
//    clonedInst->dump();

    PHINode *phi = NULL;
    // Find the phi node for the current alive instruction.
    for (BasicBlock::iterator phiIter = block->begin(); isa<PHINode>(phiIter);
         ++phiIter) {
      PHINode *tmpPhi = dyn_cast<PHINode>(phiIter);
      if (inst == tmpPhi->getIncomingValueForBlock(mergedSubregionExiting) &&
          (phMap.find(tmpPhi) != phMap.end() || phMap.empty())) {
        phi = tmpPhi;
      }
    }
    assert(phi != NULL && "Missing phi");

//    phi->dump();
    unsigned int toKeep = phi->getBasicBlockIndex(mergedSubregionExiting);
    phi->removeIncomingValue(1 - toKeep);
    phi->addIncoming(clonedAliveInst, replicatedExiting);

    // Go through the values matching the phi node in the phMap.
    CoarseningMap::iterator phIter = phMap.find(phi);

    if (phIter != phMap.end()) {
      InstVector &coarsenedPhis = phIter->second;

      unsigned int counter = 0;
      for (InstVector::iterator cphiIter = coarsenedPhis.begin(),
                                cphiEnd = coarsenedPhis.end();
           cphiIter != cphiEnd; ++cphiIter) {
        PHINode *cPhi = dyn_cast<PHINode>(*cphiIter);
        // Remove the wrong incoming block.
        unsigned int toKeep = phi->getBasicBlockIndex(mergedSubregionExiting);
        cPhi->removeIncomingValue(1 - toKeep);

        // Update the value incoming from the merged exiting block.
        CoarseningMap::iterator cMapIter = cMap.find(inst);
        assert(cMapIter != cMap.end() && "Cannot find inst in coarsening map");
        cPhi->setIncomingValue(toKeep, cMapIter->second[counter]);

        // Add the values incoming from the replicated region.
        CoarseningMap::iterator aliveMapIter = aliveMap.find(clonedAliveInst);
        assert(aliveMapIter != aliveMap.end() &&
               "Cannot find inst in alive map");
        cPhi->addIncoming(aliveMapIter->second[counter], replicatedExiting);

        ++counter;
      }

      updatePlaceholderMap(phi, coarsenedPhis);
    }
  }
}

// -----------------------------------------------------------------------------
BasicBlock *ThreadCoarsening::createTopBranch(DivergentRegion *region) {
  BasicBlock *pred = getPredecessor(region, loopInfo);
  BasicBlock *header = region->getHeader();
  BranchInst *branch = dyn_cast<BranchInst>(header->getTerminator());
  Instruction *condition = dyn_cast<Instruction>(branch->getCondition());
  assert(condition != NULL && "The condition is not an instruction");
  CoarseningMap::iterator conditionIter = cMap.find(condition);
  assert(conditionIter != cMap.end() && "condition not in coarsening map");
  InstVector &cConditions = conditionIter->second;
  Instruction *reduction =
      insertBooleanReduction(condition, cConditions, llvm::Instruction::And);
  Map headerMap;
  BasicBlock *newHeader =
      CloneBasicBlock(header, headerMap, ".newHeader", header->getParent(), 0);
  changeBlockTarget(pred, newHeader);
  BranchInst *newBranch = dyn_cast<BranchInst>(newHeader->getTerminator());
  newBranch->setCondition(reduction);
  return newHeader;
}

// -----------------------------------------------------------------------------
DivergentRegion *ThreadCoarsening::createCascadingFirstRegion(
    DivergentRegion *region, BasicBlock *pred, unsigned int branchIndex,
    Map &valueMap) {

  // Clone and insert the first region in the else branch.
  DivergentRegion *firstRegion = region->clone(".clone", dt, pdt, valueMap);

  // Attach the top link.
  changeBlockTarget(pred, firstRegion->getHeader(), (1 - branchIndex));
  // Attach the bottom link. The new exiting points to the old exiting.
  changeBlockTarget(firstRegion->getExiting(), region->getExiting());

  InstVector newAlive;
  applyMap(region->getAlive(), valueMap, newAlive);
  firstRegion->setAlive(newAlive);

  //  // FIXME.
  //  // Update the phi nodes of the newly inserted header.
  //  remapBlocksInPHIs(firstRegion->getHeader(), pred, exiting);
  //  // Update the phi nodes in the exit block.
  //  remapBlocksInPHIs(region->getExiting(), region->getExiting(),
  //                    firstRegion->getExiting());

  return firstRegion;
}

// -----------------------------------------------------------------------------
Instruction *
ThreadCoarsening::insertBooleanReduction(Instruction *base, InstVector &insts,
                                         llvm::Instruction::BinaryOps binOp) {

  llvm::Instruction *reduction = base;
  for (unsigned int index = 0; index < insts.size(); ++index) {
    reduction = BinaryOperator::Create(binOp, reduction, insts[index],
                                       Twine("agg.cmp") + Twine(index + 1),
                                       base->getParent()->getTerminator());
  }

  return reduction;
}
