//===- BranchExtraction.cpp - Extract divergent branches from blocks ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "branch_extraction"

#include "thrud/ThreadCoarsening/BranchExtraction.h"

#include "thrud/DivergenceAnalysis/DivergentRegionAnalysis.h"

#include "thrud/Support/DivergentRegion.h"
#include "thrud/Support/Utils.h"

#include "llvm/Pass.h"

#include "llvm/ADT/ValueMap.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Twine.h"

#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/RegionInfo.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <utility>

using namespace llvm;

extern cl::opt<std::string> KernelNameCL;

BorderVector GetBorders(RegionVector &regions);
Border GetBorder(DivergentRegion *R);

//------------------------------------------------------------------------------
BranchExtraction::BranchExtraction() : FunctionPass(ID) {}

//------------------------------------------------------------------------------
bool BranchExtraction::runOnFunction(Function &F) {
  // Apply the pass to kernels only.
  if (!IsKernel((const Function *)&F))
    return false;

  std::string FunctionName = F.getName();
  if (KernelNameCL != "" && FunctionName != KernelNameCL)
    return false;

  // Perform analysis.
  LoopInfo *LI = &getAnalysis<LoopInfo>();
  DivergentRegionAnalysis *DRA = &getAnalysis<DivergentRegionAnalysis>();
  RegionVector regions = DRA->getRegions();

  BorderVector Bs = GetBorders(regions);

  unsigned int Size = Bs.size();

  for (unsigned int index = 0; index < Size; ++index) {
    DivergentRegion *R = regions[index];
    Border B = Bs[index];

    // The header of the region is the block parent of the branch instruction.
    BasicBlock *Header = B.first->getParent();
    BasicBlock *Exiting = B.second;
    BasicBlock *NewHeader = NULL;

    if (!LI->isLoopHeader(Header))
      NewHeader = SplitBlock(Header, Header->getTerminator(), this);
    else {
      NewHeader = Header;
      Loop *L = LI->getLoopFor(Header);
      if (L == LI->getLoopFor(Exiting)) {
        Exiting = L->getExitBlock();
        R->setExiting(Exiting);
      }
    }
    Instruction *FirstNonPHI = Exiting->getFirstNonPHI();
    SplitBlock(Exiting, FirstNonPHI, this);
    R->setHeader(NewHeader);
    R->updateRegion();
    isolateRegion(R);
  }

  return regions.size() != 0;
}

//------------------------------------------------------------------------------
// Isolate the exiting block from the rest of the graph.
// If it has incoming edges coming from outside the current region
// create a new exiting block for the region.
void BranchExtraction::isolateRegion(DivergentRegion *Region) {
  BlockVector RegionBlocks;
  // FIXME: this can be substituted
  // If the header does not dominates the exiting it means that
  // there are other incoming edges.
  bool HasExtBlock = findRegionBlocks(Region, RegionBlocks);
  BasicBlock *Exiting = Region->getExiting();

  // Split
  if (HasExtBlock) {
    BasicBlock *New = BasicBlock::Create(
        Exiting->getContext(), Exiting->getName() + Twine(".be_split"),
        Exiting->getParent(), Exiting);
    BranchInst::Create(Exiting, New);
    for (BlockVector::iterator I = RegionBlocks.begin(), E = RegionBlocks.end();
         I != E; ++I) {
      TerminatorInst *Term = (*I)->getTerminator();
      for (unsigned int index = 0; index < Term->getNumSuccessors(); ++index) {
        if (Term->getSuccessor(index) == Exiting)
          Term->setSuccessor(index, New);
      }
    }

    // 'New' will contain the phi working on the values from the blocks
    // in the region.
    // 'Exiting' will contain the phi working on the values from the blocks
    // outside and in the region.
    PHIVector OldPhis;
    GetPHIs(Exiting, OldPhis);

    PHIVector NewPhis;
    PHIVector ExitPhis;

    for (PHIVector::iterator I = OldPhis.begin(), E = OldPhis.end(); I != E;
         ++I) {
      PHINode *Phi = *I;
      PHINode *NewPhi =
          PHINode::Create(Phi->getType(), 0,
                          Phi->getName() + Twine(".new_exiting"), New->begin());
      PHINode *ExitPhi = PHINode::Create(Phi->getType(), 0,
                                         Phi->getName() + Twine(".old_exiting"),
                                         Exiting->begin());
      for (unsigned int index = 0; index < Phi->getNumIncomingValues();
           ++index) {
        BasicBlock *BB = Phi->getIncomingBlock(index);
        if (isPresent(BB, RegionBlocks))
          NewPhi->addIncoming(Phi->getIncomingValue(index), BB);
        else
          ExitPhi->addIncoming(Phi->getIncomingValue(index), BB);
      }
      NewPhis.push_back(NewPhi);
      ExitPhis.push_back(ExitPhi);
    }

    unsigned int PhiNumber = NewPhis.size();
    for (unsigned int PhiIndex = 0; PhiIndex < PhiNumber; ++PhiIndex) {
      // Add the edge coming from the 'New' block to the phi nodes in Exiting.
      PHINode *ExitPhi = ExitPhis[PhiIndex];
      PHINode *NewPhi = NewPhis[PhiIndex];
      ExitPhi->addIncoming(NewPhi, New);

      // Update all the references to the old Phis to the new ones.
      OldPhis[PhiIndex]->replaceAllUsesWith(ExitPhi);
    }

    // Delete the old phi nodes.
    for (PHIVector::iterator I = OldPhis.begin(), E = OldPhis.end(); I != E;
         ++I) {
      PHINode *ToDelete = *I;
      ToDelete->eraseFromParent();
    }

  }

}

//------------------------------------------------------------------------------
bool BranchExtraction::findRegionBlocks(DivergentRegion *region,
                                        BlockVector &regionBlocks) {
  regionBlocks.clear();
  BasicBlock *Exiting = region->getExiting();
  bool HasExtBlock = false;
  for (pred_iterator predIter = pred_begin(Exiting), predEnd = pred_end(Exiting); predIter != predEnd;
       ++predIter) {
    BasicBlock *pred = *predIter;
    if(contains(*region, pred))
      regionBlocks.push_back(pred);
    else
      HasExtBlock = true;
  }
  return HasExtBlock;
}

//------------------------------------------------------------------------------
void BranchExtraction::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LoopInfo>();
  AU.addPreserved<LoopInfo>();
  AU.addRequired<DivergentRegionAnalysis>();
  AU.addPreserved<DivergentRegionAnalysis>();
}

//------------------------------------------------------------------------------
char BranchExtraction::ID = 0;
static RegisterPass<BranchExtraction>
    X("be", "Extract divergent branches from their blocks");

//------------------------------------------------------------------------------
BorderVector GetBorders(RegionVector &regions) {
  BorderVector Result;
  for (RegionVector::iterator I = regions.begin(), E = regions.end(); I != E; ++I) {
    Result.push_back(GetBorder(*I));
  }
  return Result;
}

//------------------------------------------------------------------------------
Border GetBorder(DivergentRegion *R) {
  Border Result;
  Result.first = dyn_cast<BranchInst>(R->getHeader()->getTerminator());
  Result.second = R->getExiting();
  return Result;
}
