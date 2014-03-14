//===- ThreadCoarsening.cpp - Merge many OpenCL threads into one ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See loopInfoCENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Required passes: -mem2reg and -instnamer
// At the end perform CSE / DCE.

#define DEBUG_TYPE "thread_coarsening"

#include "thrud/ThreadCoarsening/ThreadCoarsening.h"

#include "thrud/DivergenceAnalysis/SingleDimDivAnalysis.h"

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/Utils.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

#include "llvm/Pass.h"

#include "llvm/ADT/ValueMap.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Twine.h"

#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/RegionInfo.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Transforms/Scalar.h"

#include <utility>

using namespace llvm;

// Command line options.
extern cl::opt<int> CoarseningDirectionCL;
cl::opt<unsigned int> CoarseningFactorCL("coarsening-factor", cl::init(1), cl::Hidden,
                              cl::desc("The coarsening factor"));
cl::opt<unsigned int> CoarseningStrideCL("coarsening-stride", cl::init(1), cl::Hidden,
                    cl::desc("The coarsening stride"));
cl::opt<std::string> KernelNameCL("kernel-name", cl::init(""), cl::Hidden,
                                cl::desc("Name of the kernel to coarsen"));
cl::opt<ThreadCoarsening::DivRegionOption> DivRegionOptionCL(
    "div-region-mgt", cl::init(ThreadCoarsening::FullReplication), cl::Hidden,
    cl::desc("Divergent region management"),
    cl::values(
        clEnumValN(ThreadCoarsening::FullReplication, "classic", "Replicate full region"),
        clEnumValN(ThreadCoarsening::TrueBranchMerging, "merge-true", "Merge true branch"),
        clEnumValN(ThreadCoarsening::FalseBranchMerging, "merge-false", "Merge false branch"),
        clEnumValN(ThreadCoarsening::FullMerging, "merge", "Merge both true and false branches"),
        clEnumValEnd));

//------------------------------------------------------------------------------
ThreadCoarsening::ThreadCoarsening() : FunctionPass(ID) {}

//------------------------------------------------------------------------------
void ThreadCoarsening::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LoopInfo>();
  AU.addRequired<SingleDimDivAnalysis>();
  AU.addRequired<PostDominatorTree>();
  AU.addRequired<DominatorTree>();
}

//------------------------------------------------------------------------------
bool ThreadCoarsening::runOnFunction(Function &F) {
  // Apply the pass to kernels only.
  if (!IsKernel((const Function *)&F))
    return false;

  // Apply the pass to the selected kernel only.
  std::string FunctionName = F.getName();
  if (KernelNameCL != "" && FunctionName != KernelNameCL)
    return false;

  // Get command line options.
  direction = CoarseningDirectionCL;
  factor = CoarseningFactorCL;
  stride = CoarseningStrideCL;
  divRegionOption = DivRegionOptionCL; 

  // Perform analysis.
  loopInfo = &getAnalysis<LoopInfo>();
  pdt = &getAnalysis<PostDominatorTree>();
  dt = &getAnalysis<DominatorTree>();
  sdda = &getAnalysis<SingleDimDivAnalysis>();

  // Trasnform the kernel.
  scaleNDRange();
  coarsenFunction();
  replacePlaceholders();

  return true;
}

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

  RegionVector Regions = sdda->getDivergentRegions();
  // Replicate regions.
  for (RegionVector::iterator regionIter = Regions.begin(),
                              regionEnd = Regions.end();
       regionIter != regionEnd; ++regionIter) {
    DivergentRegion *region = *regionIter;
    region->dump();
    replicateRegion(region);
  }

//  for (unsigned int CI = 1; CI < CF; ++CI) {
//    // Mapping between the old instruction in the old region and the
//    // new instructions in the new region. These new values have to be
//    // applied to the instructions duplicated using the current
//    // coarsening index.
//    Map CIMap;
//    // Initialize the map with the TId -> newTId mapping.
//    InitializeMap(CIMap, TIds, newTIds, CI, CF);
//
//    InstPairs InstMapping;
//    DuplicateInsts(Insts, InstMapping, CIMap, CI);
//    InsertReplicatedInst(InstMapping, CIMap);
//
//    // Duplicate divergent regions.
//    Map RegionsMap;
//    ReplicateRegions(Regions, RegionsMap, CI, CIMap);
//
//    // Apply the RegionsMap to the replicated instructions.
//    for (InstPairs::iterator I = InstMapping.begin(), E = InstMapping.end();
//         I != E; ++I) {
//      Instruction *inst = I->second;
//      ApplyMap(inst, RegionsMap);
//    }
//  }
//
//  // Apply the map to all the instrucions.
//  // This replaces tid with 2 * tid.
//  Map map;
//  InitializeMap(map, TIds, newTIds, 0, CF);
//  for (RegionVector::iterator regionInfo = Regions.begin(), RE = Regions.end();
//       regionInfo != RE; ++regionInfo) {
//    DivergentRegion *R = *regionInfo;
//    BlockVector *Blocks = R->getBlocks();
//    for (BlockVector::iterator BI = Blocks->begin(), BE = Blocks->end();
//         BI != BE; ++BI) {
//      BasicBlock *BB = *BI;
//      for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I) {
//        if (!IsPresent<Instruction>(I, InstTIds))
//          ApplyMap(I, map);
//      }
//    }
//  }
//  // Apply the map to all the original divergent instructions.
//  for (InstVector::iterator I = Insts.begin(), E = Insts.end(); I != E; ++I) {
//    ApplyMap(*I, map);
//  }


}

//------------------------------------------------------------------------------
void ThreadCoarsening::replicateInst(Instruction *inst) {
  llvm::errs() << " ThreadCoarsening::replicateInst\n";
  inst->dump();

  cMap.insert(std::pair<Instruction*, InstVector>(inst, InstVector()));
  InstVector &current = cMap[inst];

  Instruction *bookmark = inst; 

  for (unsigned int CI = 0; CI < factor; ++CI) {
    // Clone.
    Instruction *newInst = inst->clone();
    renameInst(newInst, inst->getName(), CI);
    ApplyMap(newInst, cMap, CI);
    // Insert the new instruction.
    newInst->insertAfter(bookmark);
    bookmark = newInst;
    // Add the new instruction.
    current.push_back(newInst);
  }

  // Update placeholder replacement map.
  CoarseningMap::iterator cIter = cMap.find(inst);
  if(cIter != cMap.end()) {
    InstVector &V = cIter->second;
    for(unsigned int index = 0; index < V.size(); ++index) {
      PHReplacementMap[V[index]] = current[index];
    }
  }
}

//------------------------------------------------------------------------------
Instruction *
ThreadCoarsening::getCoarsenedInstruction(Instruction *inst,
                                          unsigned int coarseningIndex) {
  CoarseningMap::iterator It = cMap.find(inst);
  if (It != cMap.end()) {
    // The instruction is in the map.
    InstVector &V = It->second;
    Instruction *Result = V[coarseningIndex];
    return Result;
  } else {
    // The instruction is not in the map.
    if (sdda->IsThreadIdDependent(inst)) {
      // The instruction is divergent.
      // Look in placeholder map.
      CoarseningMap::iterator PHIt = PHMap.find(inst);
      if (PHIt != PHMap.end()) {
        // The instruction is in the placeholder map.
        InstVector &V = PHIt->second;
        Instruction *Result = V[coarseningIndex];
        return Result;
      } else {
        // The instruction is not in the placeholder map.
        // Make an entry in the placeholder map.
        InstVector newEntry;
        for (unsigned int counter = 0; counter < factor; ++counter) {
          Instruction *ph = inst->clone();
          renameInst(ph, (inst->getName() + Twine("ph")).str(),
                     coarseningIndex);
          newEntry.push_back(ph);
        }
        PHMap.insert(std::pair<Instruction *, InstVector>(inst, newEntry));
      }
    }
  }
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
void ThreadCoarsening::renameInst(Instruction *I, StringRef oldName,
                                  unsigned int index) {
  if (!oldName.empty())
    I->setName(oldName + ".." + Twine(index) + "..");
}

//------------------------------------------------------------------------------
void ThreadCoarsening::replicateRegion(DivergentRegion * R) {
  // Do not replicate if the region is strict.
  if (R->IsStrict())
    return;

  switch (divRegionOption) {
  case FullReplication: {
    replicateRegionClassic(R);
    break;
  }
//  case TrueBranchMerging: {
//    replicateRegionTrueMerging(R, RegionsMap, CI, CIMap);
//    break;
//  }
//  case FalseBranchMerging: {
//    replicateRegionFalseMerging(R, RegionsMap, CI, CIMap);
//    break;
//  }
//  case FullMerging: {
//    replicateRegionFullMerging(R, RegionsMap, CI, CIMap);
//    break;
//  }
  }
}

//------------------------------------------------------------------------------
void ThreadCoarsening::replicateRegionClassic(DivergentRegion *R) {
  RegionBounds *Bounds = GetInsertionPoints(R);
  BasicBlock *Pred = GetPred(R, loopInfo);

  // Clone the region and apply the new map.
  // CIMap is applied to all the blocks in the region.
  RegionBounds NewPair = CloneRegion(R->getBounds(), ".." + Twine(CI) + "..",
                                     dt, RegionsMap, CIMap);
  // Build the mapping for the phi nodes in the exiting block.
  BuildExitingPhiMap(R->getExiting(), NewPair.getExiting(), RegionsMap);

  // Exiting -> NewPair.first
  BasicBlock *Exiting = Bounds->getHeader();
  ChangeBlockTarget(Exiting, NewPair.getHeader());
  // NewPair.second -> IP.second
  ChangeBlockTarget(NewPair.getExiting(), Bounds->getExiting());
  // IP.first -> NewPair.second
  Bounds->setHeader(NewPair.getExiting());

  // Update the phi nodes of the newly inserted header.
  RemapBlocksInPHIs(NewPair.getHeader(), Pred, Exiting);
  // Update the phi nodes in the exit block.
  RemapBlocksInPHIs(Bounds->getExiting(), R->getExiting(), NewPair.getExiting());
}

////------------------------------------------------------------------------------
//void ThreadCoarsening::replicateRegionFullMerging(DivergentRegion *R,
//                                                  Map &RegionsMap,
//                                                  unsigned int CI, Map &CIMap) {
//}
//
////------------------------------------------------------------------------------
//void ThreadCoarsening::replicateRegionFalseMerging(DivergentRegion *R,
//                                                   Map &RegionsMap,
//                                                   unsigned int CI,
//                                                   Map &CIMap) {
//  replicateRegionMerging(R, RegionsMap, CI, CIMap, 1);
//}
//
////------------------------------------------------------------------------------
//void ThreadCoarsening::replicateRegionTrueMerging(DivergentRegion *R,
//                                                  Map &RegionsMap,
//                                                  unsigned int CI, Map &CIMap) {
//  replicateRegionMerging(R, RegionsMap, CI, CIMap, 0);
//}
//
////------------------------------------------------------------------------------
//void ThreadCoarsening::replicateRegionMerging(DivergentRegion *R,
//                                              Map &RegionsMap, unsigned int CI,
//                                              Map &CIMap, unsigned int branch) {
//  llvm::errs() << "ThreadCoarsening::replicateRegionMerging\n";
//  R->dump();
//
//  RegionBounds *Bounds = GetInsertionPoints(R);
//  BasicBlock *Pred = GetPred(R, loopInfo);
//
//  // Get branch.
//  BranchInst *Branch = dyn_cast<BranchInst>(R->getHeader()->getTerminator());
//  Branch->dump();
//
//  // Get Block.
//  BasicBlock *Head = Branch->getSuccessor(branch);
//  
//  
//
//}

//------------------------------------------------------------------------------
char ThreadCoarsening::ID = 0;
static RegisterPass<ThreadCoarsening>
    X("tc", "OpenCL Thread Coarsening Transformation Pass");
