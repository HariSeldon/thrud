#include "thrud/ThreadCoarsening/ThreadCoarsening.h"

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/Utils.h"

#include "llvm/IR/Module.h"

#include "llvm/Transforms/Utils/Cloning.h"

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
  errs() << "ThreadCoarsening::replicateInst\n";
  inst->dump();

  InstVector current;
  current.reserve(factor - 1);
  Instruction *bookmark = inst;

  errs() << "Factor:" << factor << "\n";

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
//  dump(phReplacementMap);

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
