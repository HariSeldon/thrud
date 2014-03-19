#include "thrud/ThreadCoarsening/ThreadCoarsening.h"

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/Utils.h"

#include "llvm/IR/Module.h"

//void dumpCoarseningMap(CoarseningMap &cMap) {
//  llvm::errs() << "------------------------------\n";
//  for (CoarseningMap::iterator iter = cMap.begin(), end = cMap.end();
//       iter != end; ++iter) {
//    InstVector &entry = iter->second;
//    Instruction *inst = iter->first;
//    llvm::errs() << "Key: ";
//    inst->dump();
//    llvm::errs() << " ";
//    dumpVector(entry);
//    llvm::errs() << "\n";
//  }
//  llvm::errs() << "------------------------------\n";
//}

//------------------------------------------------------------------------------
void ThreadCoarsening::coarsenFunction() {
  RegionVector &regions = sdda->getDivRegions();
  InstVector &insts = sdda->getDivInstsOutsideRegions();

  // Replicate instructions.
  for (InstVector::iterator instIter = insts.begin(), instEnd = insts.end();
       instIter != instEnd; ++instIter) {
    Instruction *inst = *instIter;
    replicateInst(inst);
  }

  // Replicate regions.
  for (RegionVector::iterator regionIter = regions.begin(),
                              regionEnd = regions.end();
       regionIter != regionEnd; ++regionIter) {
    DivergentRegion *region = *regionIter;
    replicateRegion(region);
  }
}

//------------------------------------------------------------------------------
void ThreadCoarsening::replicateInst(Instruction *inst) {
//  errs() << "ThreadCoarsening::replicateInst\n";
//  inst->dump();

  InstVector current;
  current.reserve(factor - 1);
  Instruction *bookmark = inst;

  for (unsigned int index = 0; index < factor - 1; ++index) {
    // Clone.
    Instruction *newInst = inst->clone();
    renameValueWithFactor(newInst, inst->getName(), index);
    applyCoarseningMap(newInst, index);
    // Insert the new instruction.
    newInst->insertAfter(bookmark);
    bookmark = newInst;
    // Add the new instruction to the coarsening map.
    current.push_back(newInst);
  }
  cMap.insert(std::pair<Instruction *, InstVector>(inst, current));

  // Update placeholder replacement map.
  CoarseningMap::iterator phIter = phMap.find(inst);
  if (phIter != phMap.end()) {
    InstVector &V = phIter->second;
    for (unsigned int index = 0; index < V.size(); ++index) {
      phReplacementMap[V[index]] = current[index];
    }
  }
}

//------------------------------------------------------------------------------
void ThreadCoarsening::applyCoarseningMap(DivergentRegion &region,
                                          unsigned int index) {
  for (DivergentRegion::iterator iter = region.begin(), iterEnd = region.end();
       iter != iterEnd; ++iter) {
    BasicBlock *block = *iter;
    applyCoarseningMap(block, index);
  }
}

//------------------------------------------------------------------------------
void ThreadCoarsening::applyCoarseningMap(BasicBlock *block,
                                          unsigned int index) {
  for (BasicBlock::iterator iter = block->begin(), iterEnd = block->end();
       iter != iterEnd; ++iter) {
    applyCoarseningMap(iter, index);
  }
}

//------------------------------------------------------------------------------
void ThreadCoarsening::applyCoarseningMap(Instruction *inst,
                                          unsigned int index) {
  for (unsigned int opIndex = 0, opEnd = inst->getNumOperands();
       opIndex != opEnd; ++opIndex) {
    Instruction *operand = dyn_cast<Instruction>(inst->getOperand(opIndex));
    if (operand == NULL)
      continue;
    Instruction *newOp = getCoarsenedInstruction(operand, index);
    if (newOp == NULL)
      continue;
    inst->setOperand(opIndex, newOp);
  }
}

//------------------------------------------------------------------------------
Instruction *
ThreadCoarsening::getCoarsenedInstruction(Instruction *inst,
                                          unsigned int coarseningIndex) {
//  errs() << "ThreadCoarsening::getCoarsenedInstruction\n";
//  inst->dump();
  CoarseningMap::iterator It = cMap.find(inst);
  // The instruction is in the map.
  if (It != cMap.end()) {
    InstVector &entry = It->second;
    Instruction *result = entry[coarseningIndex];
    return result;
  } else {
    // The instruction is divergent.
    if (sdda->isDivergent(inst)) {
      // Look in placeholder map.
      CoarseningMap::iterator PHIt = phMap.find(inst);
      Instruction *result = NULL;
      if (PHIt != phMap.end()) {
        // The instruction is in the placeholder map.
        InstVector &entry = PHIt->second;
        result = entry[coarseningIndex];
      }
          // The instruction is not in the placeholder map.
          else {
        // Make an entry in the placeholder map.
        InstVector newEntry;
        for (unsigned int counter = 0; counter < factor - 1; ++counter) {
          Instruction *ph = inst->clone();
          ph->insertAfter(inst);
          renameValueWithFactor(ph,
                                (inst->getName() + Twine("place.holder")).str(),
                                coarseningIndex);
          newEntry.push_back(ph);
        }
        phMap.insert(std::pair<Instruction *, InstVector>(inst, newEntry));
        // Return the appropriate placeholder.
        result = newEntry[coarseningIndex];
      }
      return result;
    }
  }
  return NULL;
}

//------------------------------------------------------------------------------
void ThreadCoarsening::replacePlaceholders() {
//  errs() << "ThreadCoarsening::replacePlaceholders\n";
//  dumpCoarseningMap(phMap);
//  printMap(phReplacementMap);

  // Iterate over placeholder map.
  for (CoarseningMap::iterator mapIter = phMap.begin(), mapEnd = phMap.end();
       mapIter != mapEnd; ++mapIter) {
    InstVector &phs = mapIter->second;
    // Iteate over placeholder vector.
    for (InstVector::iterator instIter = phs.begin(), instEnd = phs.end();
         instIter != instEnd; ++instIter) {
      Instruction *ph = *instIter;
      Value *replacement = phReplacementMap[ph];
      //if(replacement == NULL) {
      //  ph->eraseFromParent();
      //}
      assert(replacement != NULL && "Missing replacement value");
      ph->replaceAllUsesWith(replacement);
    }
  }
}

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

  for (unsigned int index = 0; index < factor - 1; ++index) {
    Map valueMap;
    DivergentRegion newRegion =
        region->clone("..cf" + Twine(index + 2), dt, pdt, valueMap);
    applyCoarseningMap(newRegion, index);
    // Connect the region to the CFG.
    BasicBlock *exiting = bookmark->getHeader();
    changeBlockTarget(exiting, newRegion.getHeader());
    changeBlockTarget(newRegion.getExiting(), bookmark->getExiting());

    // Update the phi nodes of the newly inserted header.
    remapBlocksInPHIs(newRegion.getHeader(), pred, exiting);
    // Update the phi nodes in the exit block.
    remapBlocksInPHIs(bookmark->getExiting(), region->getExiting(),
                      newRegion.getExiting());

    bookmark = getExitingAndExit(newRegion);

    // Manage alive values.
    // Go through region alive values.
    InstVector &aliveInsts = region->getAlive();
    for (InstVector::iterator iter = aliveInsts.begin(),
                              iterEnd = aliveInsts.end();
         iter != iterEnd; ++iter) {
      Instruction *alive = *iter;
      Value *newValue = valueMap[alive];
      Instruction *newAlive = dyn_cast<Instruction>(newValue);
      assert(newAlive != NULL && "Wrong value in valueMap");

      // Look for 'alive' in the coarsening map.
      CoarseningMap::iterator mapIter = cMap.find(alive);
      // The 'alive' is already in the map.
      if (mapIter != cMap.end()) {
        InstVector &cInsts = mapIter->second;
        cInsts.push_back(newAlive);
      }
          // The 'alive' is not in the map -> add a new entry.
          else {
        InstVector newCInsts(1, dyn_cast<Instruction>(newAlive));
        cMap.insert(std::pair<Instruction *, InstVector>(alive, newCInsts));
      }

      InstVector &current = cMap.find(alive)->second;

      // Update placeholder replacement map with alive values.
      CoarseningMap::iterator phIter = phMap.find(alive);
      if (phIter != phMap.end()) {
        InstVector &V = phIter->second;
        for (unsigned int index = 0; index < V.size(); ++index) {
          phReplacementMap[V[index]] = current[index];
        }
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
  if(!region->areSubregionsDisjoint()) {
    errs() << "FALLING BACK TO replicateRegionClassic!";
    return replicateRegionClassic(region);
  }

  // Identify the comparisons.
  BranchInst *branch = dyn_cast<BranchInst>(region->getHeader()->getTerminator());
  Instruction *condition = dyn_cast<Instruction>(branch->getCondition());
  assert(condition != NULL && "The condition is not an instruction");
  CoarseningMap::iterator conditionIter = cMap.find(condition);
  assert(conditionIter != cMap.end() && "condition not in coarsening map");
  InstVector &cConditions = conditionIter->second;

  Instruction *reduction =
      insertBooleanReduction(condition, cConditions, llvm::Instruction::And);

  branch->setCondition(reduction);

//  // This manages the case, all True or all False.
//  InstVector insts = sdda->getDivInsts(region, branchIndex);
//  for (InstVector::iterator iter = insts.begin(), iterEnd = insts.end(); iter != iterEnd; ++iter) {
//    Instruction *inst = *iter;
//    replicateInst(inst);
//  }

  // Manage the other case.
  BasicBlock *pred = getPredecessor(region, loopInfo);
  RegionBounds *bookmark = new RegionBounds(region->getHeader(), region->getExiting());

  // This does not work for CF higher that one.
  // I think I am not inserting the region in the right place.
  for (unsigned int index = 0; index < factor; ++index) {
    Map valueMap;
    DivergentRegion newRegion =
        region->clone("..cf" + Twine(index + 2), dt, pdt, valueMap);

    errs() << "Index:" << index << "\n";
    if(index != 0) {
      applyCoarseningMap(newRegion, index - 1);
    }


    // Connect the region to the CFG.
    BasicBlock *exiting = bookmark->getHeader();

    if(index == 0)
      changeBlockTarget(exiting, newRegion.getHeader(), 1 - branchIndex);
    else 
      changeBlockTarget(exiting, newRegion.getHeader());

    changeBlockTarget(newRegion.getExiting(), bookmark->getExiting());

    // Update the phi nodes of the newly inserted header.
    remapBlocksInPHIs(newRegion.getHeader(), pred, exiting);
    // Update the phi nodes in the exit block.
    remapBlocksInPHIs(bookmark->getExiting(), region->getExiting(),
                      newRegion.getExiting());

    delete bookmark;
    bookmark = new RegionBounds(newRegion.getExiting(), region->getExiting()); 
  }
  
  // This manages the case, all True or all False.
  InstVector insts = sdda->getDivInsts(region, branchIndex);
  for (InstVector::iterator iter = insts.begin(), iterEnd = insts.end(); iter != iterEnd; ++iter) {
    Instruction *inst = *iter;
    replicateInst(inst);
  }
  

//  region->getHeader()->getParent()->getParent()->dump();
}

//------------------------------------------------------------------------------
Instruction *
ThreadCoarsening::insertBooleanReduction(Instruction *base, InstVector &insts,
                                         llvm::Instruction::BinaryOps binOp) {

  llvm::Instruction *reduction = base;
  for (unsigned int index = 0; index < insts.size(); ++index) {
    reduction = BinaryOperator::Create(binOp, reduction, insts[index],
                                       Twine("cmp.cf") + Twine(index + 1),
                                       base->getParent()->getTerminator());
  }

  return reduction;
}
