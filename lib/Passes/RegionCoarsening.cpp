#include "thrud/ThreadCoarsening/ThreadCoarsening.h"

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/Utils.h"

#include "llvm/IR/Module.h"

#include "llvm/Transforms/Utils/Cloning.h"

#include "thrud/Support/Utils.h"

//------------------------------------------------------------------------------
void ThreadCoarsening::replicateRegion(DivergentRegion *region) {
  assert(dt->dominates(region->getHeader(), region->getExiting()) &&
         "Header does not dominates Exiting");
  assert(pdt->dominates(region->getExiting(), region->getHeader()) &&
         "Exiting does not post dominate Header");

  // Do not replicate if the region is strict.
  if (region->isStrict())
    return;

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
  //  errs() << "ThreadCoarsening::replicateRegionClassic\n";

  BasicBlock *pred = getPredecessor(region, loopInfo);
  RegionBounds *bookmark = getExitingAndExit(*region);

  CoarseningMap aliveMap;
  // Initialize aliveMap.
  InstVector &aliveInsts = region->getAlive();
  for (InstVector::iterator iter = aliveInsts.begin(),
                            iterEnd = aliveInsts.end();
       iter != iterEnd; ++iter) {
    aliveMap.insert(std::pair<Instruction *, InstVector>(*iter, InstVector()));
  }

  // Replicate the region.
  for (unsigned int index = 0; index < factor - 1; ++index) {
    Map valueMap;
    DivergentRegion *newRegion =
        region->clone(".cf" + Twine(index + 2), dt, pdt, valueMap);
    applyCoarseningMap(*newRegion, index);
    // Connect the region to the CFG.
    BasicBlock *exiting = bookmark->getHeader();
    changeBlockTarget(exiting, newRegion->getHeader());
    changeBlockTarget(newRegion->getExiting(), bookmark->getExiting());

    // Update the phi nodes of the newly inserted header.
    remapBlocksInPHIs(newRegion->getHeader(), pred, exiting);
    // Update the phi nodes in the exit block.
    remapBlocksInPHIs(bookmark->getExiting(), region->getExiting(),
                      newRegion->getExiting());

    bookmark = getExitingAndExit(*newRegion);
    delete newRegion;

    // Fill the aliveMap.
    for (CoarseningMap::iterator mapIter = aliveMap.begin(),
                                 mapEnd = aliveMap.end();
         mapIter != mapEnd; ++mapIter) {
      InstVector &coarsenedInsts = mapIter->second;
      Value *value = valueMap[mapIter->first];
      assert(value != NULL && "Missing alive value in region map");
      coarsenedInsts.push_back(dyn_cast<Instruction>(value));
    }
  }

  // Go through region alive values.
  for (CoarseningMap::iterator mapIter = aliveMap.begin(),
                               mapEnd = aliveMap.end();
       mapIter != mapEnd; ++mapIter) {
    Instruction *alive = mapIter->first;
    InstVector &coarsenedValues = mapIter->second;

    // Update placeholder replacement map with alive values.
    CoarseningMap::iterator phIter = phMap.find(alive);
    if (phIter != phMap.end()) {
      InstVector &V = phIter->second;
      for (unsigned int index = 0; index < factor - 1; ++index) {
        phReplacementMap[V[index]] = coarsenedValues[index];
      }
    }
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
  errs() << "ThreadCoarsening::replicateRegionMerging\n";

  if (!region->areSubregionsDisjoint()) {
    assert(false && "Region merging is not possible");
    return replicateRegionClassic(region);
  }

  InstVector &alive = region->getAlive();
  // Identify exiting block of merged subregion.
  BasicBlock *mergedSubregionExiting =
      getSubregionExiting(region, branchIndex);
  // Identify the alive values defined in the merged subregion.
  InstVector aliveFromMerged;
  getSubregionAlive(region, mergedSubregionExiting, aliveFromMerged);

  errs() << "mergedSubregionExiting: " << mergedSubregionExiting->getName()
         << "\n";
  errs() << "aliveFromMerged:\n";
  dumpVector(aliveFromMerged);

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

  BasicBlock *pred = firstRegion->getExiting();
  RegionBounds *bookmark =
      new RegionBounds(firstRegion->getExiting(), region->getExiting());
  BasicBlock *replicatedExiting = NULL;
  firstRegion->dump();

  // Initialize aliveMap.
  // FIXME: Hoist this in an another function.
  CoarseningMap aliveMap;
  InstVector &aliveInsts = firstRegion->getAlive();
  for (InstVector::iterator iter = aliveInsts.begin(),
                            iterEnd = aliveInsts.end();
       iter != iterEnd; ++iter) {
    aliveMap.insert(std::pair<Instruction *, InstVector>(*iter, InstVector()));
  }

  dumpCoarseningMap(aliveMap);

  for (unsigned int index = 0; index < factor - 1; ++index) {
    Map valueMap;
    DivergentRegion *newRegion =
        firstRegion->clone(".cf" + Twine(index + 2), dt, pdt, valueMap);
    applyCoarseningMap(*newRegion, index);

    // Connect the region to the CFG.
    BasicBlock *exiting = bookmark->getHeader();
    changeBlockTarget(exiting, newRegion->getHeader());
    changeBlockTarget(newRegion->getExiting(), bookmark->getExiting());

    // Update the phi nodes of the newly inserted header.
    remapBlocksInPHIs(newRegion->getHeader(), pred, exiting);
    // Update the phi nodes in the exit block.
    remapBlocksInPHIs(bookmark->getExiting(), region->getExiting(),
                      newRegion->getExiting());

    delete bookmark;
    bookmark = new RegionBounds(newRegion->getExiting(), region->getExiting());
    replicatedExiting = newRegion->getExiting();

    // Fill the aliveMap.
    for (CoarseningMap::iterator mapIter = aliveMap.begin(),
                                 mapEnd = aliveMap.end();
         mapIter != mapEnd; ++mapIter) {
      InstVector &coarsenedInsts = mapIter->second;
      Value *value = valueMap[mapIter->first];
      assert(value != NULL && "Missing alive value in region map");
      coarsenedInsts.push_back(dyn_cast<Instruction>(value));
    }
  }

  // Remove old region header.
  region->getHeader()->getTerminator()->eraseFromParent();
  region->getHeader()->eraseFromParent();

//  region->getExiting()->getParent()->getParent()->dump();
//  exit(1);

  // Add new phi nodes to the exit block.
  updateExitPhiNodes(region->getExiting(), mergedSubregionExiting,
                     aliveFromMerged, firstRegionMap, replicatedExiting,
                     aliveMap);
}

// -----------------------------------------------------------------------------
void ThreadCoarsening::removeRedundantBlocks(DivergentRegion *region,
                                             unsigned int branchIndex) {
  BranchInst *branch = dyn_cast<BranchInst>(region->getHeader()->getTerminator());
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
  errs() << "getAlivePastPhi\n";
  inst->dump();
  dumpCoarseningMap(aliveMap);

  CoarseningMap::iterator aliveIter = aliveMap.find(inst);
  if(aliveIter != aliveMap.end()) {
    return aliveIter->first;
  } else {
    for (CoarseningMap::iterator iter = aliveMap.begin(),
                                 iterEnd = aliveMap.end();
         iter != iterEnd; ++iter) {
      Instruction *mapInst = iter->first;
      if(PHINode *phi = dyn_cast<PHINode>(mapInst)) {
        phi->dump();
        unsigned int argNumber = phi->getNumIncomingValues();
        for (unsigned int index = 0; index < argNumber; ++index) {
          Value *phiValue = phi->getIncomingValue(index);
          phiValue->dump();
          if(phiValue == inst) {
            return phi;
          }
        }
      }
    }
  }
  return NULL;
}

void ThreadCoarsening::updateExitPhiNodes(BasicBlock *block,
                                          BasicBlock *mergedSubregionExiting,
                                          InstVector &aliveFromMerged,
                                          Map &cloningMap,
                                          BasicBlock *replicatedExiting,
                                          CoarseningMap &aliveMap) {

  errs() << "ThreadCoarsening::updateExitPhiNodes\n";

  dumpCoarseningMap(aliveMap);
  dumpCoarseningMap(cMap);
  dumpCoarseningMap(phMap);
  
  PhiVector phis;

  for (InstVector::iterator aliveIter = aliveFromMerged.begin(),
                            aliveIterEnd = aliveFromMerged.end();
       aliveIter != aliveIterEnd; ++aliveIter) {
    Instruction *inst = *aliveIter;
    Instruction *clonedInst = dyn_cast<Instruction>(cloningMap[inst]);
    Instruction *clonedAliveInst = getAlivePastPhi(clonedInst, aliveMap);
    clonedAliveInst->dump();
    errs() << "inst:";
    inst->dump();
    errs() << "clonedInst:";
    clonedInst->dump();

    PHINode *phi = NULL;
    // Find the phi node for the for the current alive instruction.
    for (BasicBlock::iterator phiIter = block->begin(); isa<PHINode>(phiIter); ++phiIter) {
      PHINode *tmpPhi = dyn_cast<PHINode>(phiIter);
      if(inst == tmpPhi->getIncomingValueForBlock(mergedSubregionExiting) &&
          phMap.find(tmpPhi) != phMap.end()) {
        phi = tmpPhi;
      }
    }
    assert(phi != NULL && "Missing phis");
  
    phi->dump();
    unsigned int toKeep = phi->getBasicBlockIndex(mergedSubregionExiting);
    phi->removeIncomingValue(1 - toKeep);
    phi->addIncoming(clonedAliveInst, replicatedExiting); 

    errs() << "cphis:\n";

    // Go through the values matching the phi node in the phMap. 
    InstVector &coarsenedPhis = phMap.find(phi)->second;
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
      assert(aliveMapIter != aliveMap.end() && "Cannot find inst in alive map");
      cPhi->addIncoming(aliveMapIter->second[counter], replicatedExiting);

      ++counter;
    }
  }

//    for (unsigned int index = 0; index < factor; ++index) {
//
//      // Add the incoming value from mergedSubregionExiting.
//      Instruction *toAdd = NULL;
//      if (index == 0) {
//        toAdd = inst;
//      } else {
//        CoarseningMap::iterator cMapIter = cMap.find(inst);
//        assert(cMapIter != cMap.end() && "Cannot find inst in coarsening map");
//        InstVector &coarsenedInsts = cMapIter->second;
//        toAdd = coarsenedInsts[index - 1];
//      }
//
//      newPhi->addIncoming(toAdd, mergedSubregionExiting);
//
//      // Add the incoming value from replicatedExiting.
//      if (index == 0) {
//        // Find clonedInst in the aliveMap.
//        // Alive map might contain phi nodes, in this case look in the arguments
//        // and return the result of the phi.
//        toAdd = clonedAliveInst;
//      } else {
//        CoarseningMap::iterator aliveMapIter = aliveMap.find(clonedAliveInst);
//        assert(aliveMapIter != aliveMap.end() && "Cannot find inst in alive map");
//        InstVector &coarsenedInsts = aliveMapIter->second;
//        toAdd = coarsenedInsts[index - 1];
//      }
//
//      newPhi->addIncoming(toAdd, replicatedExiting);
//    }
//  }
//
//  block->dump();
}

//void ThreadCoarsening::updateExitPhiNodes(BasicBlock *block,
//                                          BasicBlock *mergedSubregionExiting,
//                                          InstVector &aliveFromMerged,
//                                          Map &cloningMap,
//                                          BasicBlock *replicatedExiting,
//                                          CoarseningMap &aliveMap) {
//
//  errs() << "ThreadCoarsening::updateExitPhiNodes\n";
//
//  dumpCoarseningMap(aliveMap);
//
//  for (InstVector::iterator iter = aliveFromMerged.begin(),
//                            iterEnd = aliveFromMerged.end();
//       iter != iterEnd; ++iter) {
//    Instruction *inst = *iter;
//    Instruction *clonedInst = dyn_cast<Instruction>(cloningMap[inst]);
//    Instruction *clonedAliveInst = getAlivePastPhi(clonedInst, aliveMap); 
//    clonedAliveInst->dump(); 
//
//    errs() << "inst:";
//    inst->dump();
//    errs() << "clonedInst:";
//    clonedInst->dump();
//
//    for (unsigned int index = 0; index < factor; ++index) {
//
//      // Create phinode.
//      PHINode *newPhi = PHINode::Create(
//          inst->getType(), 2, inst->getName() + ".phXXX", block->begin());
//
//      // Add the incoming value from mergedSubregionExiting.
//      Instruction *toAdd = NULL;
//      if (index == 0) {
//        toAdd = inst;
//      } else {
//        CoarseningMap::iterator cMapIter = cMap.find(inst);
//        assert(cMapIter != cMap.end() && "Cannot find inst in coarsening map");
//        InstVector &coarsenedInsts = cMapIter->second;
//        toAdd = coarsenedInsts[index - 1];
//      }
//
//      newPhi->addIncoming(toAdd, mergedSubregionExiting);
//
//      // Add the incoming value from replicatedExiting.
//      if (index == 0) {
//        // Find clonedInst in the aliveMap.
//        // Alive map might contain phi nodes, in this case look in the arguments
//        // and return the result of the phi.
//        toAdd = clonedAliveInst;
//      } else {
//        CoarseningMap::iterator aliveMapIter = aliveMap.find(clonedAliveInst);
//        assert(aliveMapIter != aliveMap.end() && "Cannot find inst in alive map");
//        InstVector &coarsenedInsts = aliveMapIter->second;
//        toAdd = coarsenedInsts[index - 1];
//      }
//
//      newPhi->addIncoming(toAdd, replicatedExiting);
//    }
//  }
//
//  block->dump();
//}

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

  // FIXME.
  // Update the phi nodes of the newly inserted header.
  //  remapBlocksInPHIs(firstRegion->getHeader(), pred, exiting);
  // Update the phi nodes in the exit block.
  //  remapBlocksInPHIs(region->getExiting(), region->getExiting(),
  //                    firstRegion->getExiting());

  return firstRegion;
}

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
