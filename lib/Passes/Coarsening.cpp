#include "thrud/ThreadCoarsening/ThreadCoarsening.h"

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/Utils.h"

//void dumpCoarseningMap(CoarseningMap &cMap) {
//  llvm::errs() << "------------------------------\n";
//  for (CoarseningMap::iterator iter = cMap.begin(), end = cMap.end(); iter != end; ++iter) {
//    InstVector &entry = iter->second;
//    Instruction *inst = iter->first;
//    llvm::errs() << "Key: ";
//    inst->dump();
//    llvm::errs() << " ";
//    entry[0]->dump();
//    llvm::errs() << "\n";
//  }
//  llvm::errs() << "------------------------------\n";
//}

//------------------------------------------------------------------------------
void ThreadCoarsening::coarsenFunction() {
  InstVector TIds = sdda->getThreadIds();
  InstVector Insts = sdda->getInstToRepOutsideRegions();

  // Replicate instructions.
  for (InstVector::iterator instIter = Insts.begin(), instEnd = Insts.end();
       instIter != instEnd; ++instIter) {
    Instruction *inst = *instIter;
    replicateInst(inst);
  }

  RegionVector regions = sdda->getDivergentRegions();
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
  CoarseningMap::iterator cIter = cMap.find(inst);
  if (cIter != cMap.end()) {
    InstVector &V = cIter->second;
    for (unsigned int index = 0; index < V.size(); ++index) {
      PHReplacementMap[V[index]] = current[index];
    }
  }
  
//  dumpCoarseningMap(cMap);
}

//------------------------------------------------------------------------------
void ThreadCoarsening::applyCoarseningMap(DivergentRegion *region, 
                                          unsigned int index) {
    
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
    Instruction *Op = dyn_cast<Instruction>(inst->getOperand(opIndex));
    Instruction *newOp = getCoarsenedInstruction(Op, index);
    if(newOp == NULL)
      continue;
    inst->setOperand(opIndex, newOp);
  }
}


//------------------------------------------------------------------------------
Instruction *
ThreadCoarsening::getCoarsenedInstruction(Instruction *inst,
                                          unsigned int coarseningIndex) {
  CoarseningMap::iterator It = cMap.find(inst);
  if (It != cMap.end()) {
    // The instruction is in the map.
    InstVector &entry = It->second;
    Instruction *result = entry[coarseningIndex];
    return result;
  } else {
    // The instruction is not in the map.
    if (sdda->IsThreadIdDependent(inst)) {
      // The instruction is divergent.
      // Look in placeholder map.
      CoarseningMap::iterator PHIt = PHMap.find(inst);
      Instruction *result = NULL;
      if (PHIt != PHMap.end()) {
        // The instruction is in the placeholder map.
        InstVector &entry = PHIt->second;
        result = entry[coarseningIndex];
      } else {
        // The instruction is not in the placeholder map.
        // Make an entry in the placeholder map.
        InstVector newEntry;
        for (unsigned int counter = 0; counter < factor - 1; ++counter) {
          Instruction *ph = inst->clone();
          renameValueWithFactor(ph, (inst->getName() + Twine("ph")).str(),
                                coarseningIndex);
          newEntry.push_back(ph);
        }
        PHMap.insert(std::pair<Instruction *, InstVector>(inst, newEntry));
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
  // Iterate over placeholder map.
  for (CoarseningMap::iterator mapIter = PHMap.begin(), mapEnd = PHMap.end();
       mapIter != mapEnd; ++mapIter) {
    InstVector &phs = mapIter->second;
    // Iteate over placeholder vector.
    for (InstVector::iterator instIter = phs.begin(), instEnd = phs.end();
         instIter != instEnd; ++instIter) {
      Instruction *ph = *instIter;
      Value *replacement = PHReplacementMap[ph];
      assert(replacement != NULL && "Missing replacement value");
      ph->replaceAllUsesWith(replacement);
    }
  }
}

//------------------------------------------------------------------------------
void ThreadCoarsening::replicateRegion(DivergentRegion * region) {
  // Do not replicate if the region is strict.
  if (region->IsStrict())
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
  region->dump();

  RegionBounds *bounds = getExtingExit(region);
  BasicBlock *pred = getPredecessor(region, loopInfo);

  for (unsigned int index = 0; index < factor - 1; ++index) {
    // Clone the region and apply the new map.
    // CIMap is applied to all the blocks in the region.
    RegionBounds newPair =
        cloneRegion(region->getBounds(), "..cf" + Twine(index + 2), dt);

//    // Build the mapping for the phi nodes in the exiting block.
//    BuildExitingPhiMap(R->getExiting(), NewPair.getExiting(), RegionsMap);
//  
    // Exiting -> NewPair.first
    BasicBlock *exiting = bounds->getHeader();
    llvm::errs() << "Exiting: " << exiting->getName() << "\n";
    changeBlockTarget(exiting, newPair.getHeader());
    // NewPair.second -> IP.second
    llvm::errs() << "New pair: " << newPair.getExiting()->getName() << "\n";
    changeBlockTarget(newPair.getExiting(), bounds->getExiting());
    // IP.first -> NewPair.second
    bounds->setHeader(newPair.getExiting());
  
    // Update the phi nodes of the newly inserted header.
    remapBlocksInPHIs(newPair.getHeader(), pred, exiting);
    // Update the phi nodes in the exit block.
    remapBlocksInPHIs(bounds->getExiting(), region->getExiting(), newPair.getExiting());
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
                                              unsigned int branch) {
//  RegionBounds *bounds = getExtingExit(region);
//  BasicBlock *pred = getPredecessor(region, loopInfo);
//
//  // Get branch.
//  BranchInst *Branch = dyn_cast<BranchInst>(R->getHeader()->getTerminator());
//  Branch->dump();
//
//  // Get Block.
//  BasicBlock *Head = Branch->getSuccessor(branch);
}
