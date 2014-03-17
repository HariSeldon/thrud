#define DEBUG_TYPE "branch_extraction"

#include "thrud/ThreadCoarsening/BranchExtraction.h"

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/DivergentRegion.h"
#include "thrud/Support/Utils.h"

#include "llvm/Pass.h"

#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/PostDominators.h"

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
cl::opt<int> CoarseningDirectionCL("coarsening-direction", cl::init(0),
                                   cl::Hidden,
                                   cl::desc("The coarsening direction"));

//------------------------------------------------------------------------------
BranchExtraction::BranchExtraction() : FunctionPass(ID) {}

//------------------------------------------------------------------------------
void BranchExtraction::getAnalysisUsage(AnalysisUsage &au) const {
  au.addRequired<LoopInfo>();
  au.addRequired<SingleDimDivAnalysis>();
  au.addRequired<PostDominatorTree>();
  au.addRequired<DominatorTree>();
  au.addPreserved<SingleDimDivAnalysis>();
}

//------------------------------------------------------------------------------
bool BranchExtraction::runOnFunction(Function &F) {
  // Apply the pass to kernels only.
  if (!IsKernel((const Function *)&F))
    return false;

  std::string FunctionName = F.getName();
  if (KernelNameCL != "" && FunctionName != KernelNameCL)
    return false;

  // Perform analyses.
  loopInfo = &getAnalysis<LoopInfo>();
  dt = &getAnalysis<DominatorTree>();
  pdt = &getAnalysis<PostDominatorTree>();

  SingleDimDivAnalysis *sdda = &getAnalysis<SingleDimDivAnalysis>();

  // Isolate regions.
  RegionVector &regions = sdda->getDivRegions();
  for (RegionVector::iterator iter = regions.begin(), iterEnd = regions.end();
       iter != iterEnd; ++iter) {
    DivergentRegion *region = *iter;
    isolateRegion(region);
    // Update region.
    region->fillRegion(dt, pdt);
  }

  return regions.size() != 0;
}

//------------------------------------------------------------------------------
// Isolate the exiting block from the rest of the graph.
// If it has incoming edges coming from outside the current region
// create a new exiting block for the region.
void BranchExtraction::isolateRegion(DivergentRegion *region) {
  BasicBlock *header = region->getHeader();
  BasicBlock *exiting = region->getExiting();
  BasicBlock *newHeader = NULL;

  if(loopInfo->getLoopFor(header) == NULL && loopInfo->getLoopFor(exiting) == NULL) {
    // Split Header.
    BasicBlock *newHeader = SplitBlock(header, header->getTerminator(), this);
    region->setHeader(newHeader);

    // Split Exiting.
    Instruction *firstNonPHI = exiting->getFirstNonPHI();
    BasicBlock *newExit = SplitBlock(exiting, firstNonPHI, this);
  }

//  if (!loopInfo->isLoopHeader(header)) {
//    newHeader = SplitBlock(header, header->getTerminator(), this);
//  }
//  else {
//    newHeader = header;
//    Loop *loop = loopInfo->getLoopFor(header);
//    if (loop == loopInfo->getLoopFor(exiting)) {
//      exiting = loop->getExitBlock();
//      region->setExiting(exiting);
//    }
//  }
//  Instruction *firstNonPHI = exiting->getFirstNonPHI();
//  SplitBlock(exiting, firstNonPHI, this);
//  region->setHeader(newHeader);

  // Try to do without this.

//  BlockVector RegionBlocks;
//  // FIXME: this can be substituted
//  // If the header does not dominates the exiting it means that
//  // there are other incoming edges.
//  bool HasExtBlock = findRegionBlocks(Region, RegionBlocks);
//  BasicBlock *Exiting = Region->getExiting();
//
//  // Split
//  if (HasExtBlock) {
//    BasicBlock *New = BasicBlock::Create(
//        Exiting->getContext(), Exiting->getName() + Twine(".be_split"),
//        Exiting->getParent(), Exiting);
//    BranchInst::Create(Exiting, New);
//    for (BlockVector::iterator I = RegionBlocks.begin(), E = RegionBlocks.end();
//         I != E; ++I) {
//      TerminatorInst *Term = (*I)->getTerminator();
//      for (unsigned int index = 0; index < Term->getNumSuccessors(); ++index) {
//        if (Term->getSuccessor(index) == Exiting)
//          Term->setSuccessor(index, New);
//      }
//    }
//
//    // 'New' will contain the phi working on the values from the blocks
//    // in the region.
//    // 'Exiting' will contain the phi working on the values from the blocks
//    // outside and in the region.
//    PHIVector OldPhis;
//    GetPHIs(Exiting, OldPhis);
//
//    PHIVector NewPhis;
//    PHIVector ExitPhis;
//
//    for (PHIVector::iterator I = OldPhis.begin(), E = OldPhis.end(); I != E;
//         ++I) {
//      PHINode *Phi = *I;
//      PHINode *NewPhi =
//          PHINode::Create(Phi->getType(), 0,
//                          Phi->getName() + Twine(".new_exiting"), New->begin());
//      PHINode *ExitPhi = PHINode::Create(Phi->getType(), 0,
//                                         Phi->getName() + Twine(".old_exiting"),
//                                         Exiting->begin());
//      for (unsigned int index = 0; index < Phi->getNumIncomingValues();
//           ++index) {
//        BasicBlock *BB = Phi->getIncomingBlock(index);
//        if (isPresent(BB, RegionBlocks))
//          NewPhi->addIncoming(Phi->getIncomingValue(index), BB);
//        else
//          ExitPhi->addIncoming(Phi->getIncomingValue(index), BB);
//      }
//      NewPhis.push_back(NewPhi);
//      ExitPhis.push_back(ExitPhi);
//    }
//
//    unsigned int PhiNumber = NewPhis.size();
//    for (unsigned int PhiIndex = 0; PhiIndex < PhiNumber; ++PhiIndex) {
//      // Add the edge coming from the 'New' block to the phi nodes in Exiting.
//      PHINode *ExitPhi = ExitPhis[PhiIndex];
//      PHINode *NewPhi = NewPhis[PhiIndex];
//      ExitPhi->addIncoming(NewPhi, New);
//
//      // Update all the references to the old Phis to the new ones.
//      OldPhis[PhiIndex]->replaceAllUsesWith(ExitPhi);
//    }
//
//    // Delete the old phi nodes.
//    for (PHIVector::iterator I = OldPhis.begin(), E = OldPhis.end(); I != E;
//         ++I) {
//      PHINode *ToDelete = *I;
//      ToDelete->eraseFromParent();
//    }
//  }
}

//------------------------------------------------------------------------------
char BranchExtraction::ID = 0;
static RegisterPass<BranchExtraction> X("be", "Extract divergent regions");
