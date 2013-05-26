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

STATISTIC(NumExtracted, "Number of extracted branches");

extern cl::opt<std::string> KernelName;

BorderVector GetBorders(RegionVector &Rs);
Border GetBorder(DivergentRegion *R);

//------------------------------------------------------------------------------
BranchExtraction::BranchExtraction() : FunctionPass(ID) {}

//------------------------------------------------------------------------------
bool BranchExtraction::runOnFunction(Function &F) {
  errs() << "BranchExtraction\n";

  // Apply the pass to kernels only.
  if (!IsKernel((const Function *)&F))
    return false;

  std::string FunctionName = F.getName();
  if(KernelName != "" && FunctionName != KernelName)
    return false;


  // Perform analysis.
  LoopInfo *LI = &getAnalysis<LoopInfo>();
  DivergentRegionAnalysis *DRA = &getAnalysis<DivergentRegionAnalysis>();
  RegionVector Rs = DRA->getRegions();
 
  BorderVector Bs = GetBorders(Rs);

  unsigned int Size = Bs.size();

  for (unsigned int index = 0; index < Size; ++index) {
    DivergentRegion *R = Rs[index];
    Border B = Bs[index];

    // The header of the region is the block parent of the branch instruction.
    BasicBlock *Header = B.first->getParent();
    BasicBlock *Exiting = B.second;
    BasicBlock *NewHeader = NULL;
   
    if (!LI->isLoopHeader(Header))
      NewHeader = SplitBlock(Header, Header->getTerminator(), this);
    else {
      NewHeader = Header;
      Loop* L = LI->getLoopFor(Header);
      if(L == LI->getLoopFor(Exiting)) {
        Exiting = L->getExitBlock();
        R->setExiting(Exiting);
      }
    }
    Instruction *FirstNonPHI = Exiting->getFirstNonPHI();
    SplitBlock(Exiting, FirstNonPHI, this);
    R->setHeader(NewHeader);
    R->UpdateRegion();
    IsolateRegion(R);

  }

  NumExtracted = Rs.size();

  //dumpVector(Rs);

  return NumExtracted != 0;
}

//------------------------------------------------------------------------------
// Isolate the exiting block from the rest of the graph.
// If it has incoming edges coming from outside the current region
// create a new exiting block for the region.
void BranchExtraction::IsolateRegion(DivergentRegion *Region) {
  BlockVector RegionBlocks;
  // FIXME: this can be substituted
  // If the header does not dominates the exiting it means that
  // there are other incoming edges. 
  bool HasExtBlock = FindRegionBlocks(Region, RegionBlocks);
  BasicBlock *Exiting = Region->getExiting(); 

  // Split 
  if(HasExtBlock) {
    BasicBlock *New = BasicBlock::Create(Exiting->getContext(), 
                                         Exiting->getName() + Twine(".be_split"),
                                         Exiting->getParent(), 
                                         Exiting);
    BranchInst::Create(Exiting, New);
    for (BlockVector::iterator I = RegionBlocks.begin(), E = RegionBlocks.end();
         I != E; ++I) {
      TerminatorInst *Term = (*I)->getTerminator();
      for (unsigned int index = 0; index < Term->getNumSuccessors(); ++index) {
        if(Term->getSuccessor(index) == Exiting)
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
    
    for (PHIVector::iterator I = OldPhis.begin(), E = OldPhis.end(); 
         I != E; ++I) {
      PHINode *Phi = *I;
      PHINode *NewPhi = PHINode::Create(Phi->getType(), 0, 
                                        Phi->getName() + Twine(".new_exiting"), 
                                        New->begin());
      PHINode *ExitPhi = PHINode::Create(Phi->getType(), 0, 
                                         Phi->getName() + Twine(".old_exiting"), 
                                         Exiting->begin());
      for (unsigned int index = 0; 
           index < Phi->getNumIncomingValues(); 
           ++index) {
        BasicBlock *BB = Phi->getIncomingBlock(index);
        if(IsPresent(BB, RegionBlocks))
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
    for (PHIVector::iterator I = OldPhis.begin(), E = OldPhis.end(); 
         I != E; ++I) {
      PHINode *ToDelete = *I;
      ToDelete->eraseFromParent();
    }

  }

}

//------------------------------------------------------------------------------
bool BranchExtraction::FindRegionBlocks(DivergentRegion *Region,
                                        BlockVector &RegionBlocks) {
  RegionBlocks.clear();
  BasicBlock *Exiting = Region->getExiting();
  bool HasExtBlock = false;
  for (pred_iterator PI = pred_begin(Exiting), E = pred_end(Exiting);
       PI != E; ++PI) {
    BasicBlock *Pred = *PI;
    if(Region->Contains(Pred))
      RegionBlocks.push_back(Pred);
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
  //AU.addRequiredID(LoopSimplifyID);
}

//------------------------------------------------------------------------------
char BranchExtraction::ID = 0;
static RegisterPass<BranchExtraction> X(
       "be", 
       "Extract divergent branches from their blocks");

//------------------------------------------------------------------------------
BorderVector GetBorders(RegionVector &Rs) {
  BorderVector Result;
  for (RegionVector::iterator I = Rs.begin(), E = Rs.end(); I != E; ++I) {
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
