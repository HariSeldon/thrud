#include "thrud/ThreadCoarsening/ThreadCoarsening.h"

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/Utils.h"

#include "llvm/IR/Module.h"

#include "llvm/Transforms/Utils/Cloning.h"

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
    delete newRegion;
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
    return replicateRegionClassic(region);
  }

  BasicBlock *topBlock = createTopBranch(region);
  DivergentRegion *firstRegion =
      createCascadingFirstRegion(region, topBlock, branchIndex);

  // This manages the case, all True or all False.
  InstVector insts = sdda->getDivInsts(region, branchIndex);
  for (InstVector::iterator iter = insts.begin(), iterEnd = insts.end();
       iter != iterEnd; ++iter) {
    Instruction *inst = *iter;
    replicateInst(inst);
  }
  //  ?????
  //  newBranch->setSuccessor(branchIndex, branch->getSuccessor(branchIndex));
  //  changeBlockTarget(topBlock, newRegion->getHeader());

  BasicBlock *pred = firstRegion->getExiting();
  RegionBounds *bookmark =
      new RegionBounds(firstRegion->getExiting(), region->getExiting());

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
    // FIXME manage alive values.
  }

  // Remove old region header.
  region->getHeader()->getTerminator()->eraseFromParent();
  region->getHeader()->eraseFromParent();
}

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
      CloneBasicBlock(header, headerMap, "newHeader", header->getParent(), 0);
  changeBlockTarget(pred, newHeader);
  BranchInst *newBranch = dyn_cast<BranchInst>(newHeader->getTerminator());
  newBranch->setCondition(reduction);
  return newHeader;
}

DivergentRegion *ThreadCoarsening::createCascadingFirstRegion(
    DivergentRegion *region, BasicBlock *pred, unsigned int branchIndex) {

  // Clone and insert the first region in the else branch.
  Map valueMap;
  DivergentRegion *firstRegion = region->clone(".clone", dt, pdt, valueMap);

  // Attach the top link.
  changeBlockTarget(pred, firstRegion->getHeader(), (1 - branchIndex));
  // Attach the bottom link.
  changeBlockTarget(firstRegion->getExiting(), region->getExiting());

// FIXME.
//  // Update the phi nodes of the newly inserted header.
//  remapBlocksInPHIs(firstRegion->getHeader(), pred, exiting);
//  // Update the phi nodes in the exit block.
//  remapBlocksInPHIs(region->getExiting(), region->getExiting(),
//                    firstRegion->getExiting());

  // FIXME manage alive values.
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
