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

#include <functional>
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
  llvm::errs() << "BranchExtraction::runOnFunction\n";
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
    extractBranches(region);
    region->fillRegion(dt, pdt);
    isolateRegion(region);
    region->fillRegion(dt, pdt);
  }

  return regions.size() != 0;
}

//------------------------------------------------------------------------------
// Isolate the exiting block from the rest of the graph.
// If it has incoming edges coming from outside the current region
// create a new exiting block for the region.
void BranchExtraction::extractBranches(DivergentRegion *region) {
  BasicBlock *header = region->getHeader();
  BasicBlock *exiting = region->getExiting();

  // No loops.
  if(loopInfo->getLoopFor(header) == NULL && loopInfo->getLoopFor(exiting) == NULL) {
    // Split Header.
    BasicBlock *newHeader = SplitBlock(header, header->getTerminator(), this);
    region->setHeader(newHeader);

    // Split Exiting.
    Instruction *firstNonPHI = exiting->getFirstNonPHI();
    SplitBlock(exiting, firstNonPHI, this);
  }

  // Loops.
  Loop *loop = loopInfo->getLoopFor(header);
  if(loop != NULL) {
    if (loop == loopInfo->getLoopFor(exiting)) {
      exiting = loop->getExitBlock();
      region->setExiting(exiting);
    }

    // Split Exiting.
    Instruction *firstNonPHI = exiting->getFirstNonPHI();
    SplitBlock(exiting, firstNonPHI, this);
  }
}

// Remember to update the DT / PDT.
void BranchExtraction::isolateRegion(DivergentRegion *region) {
  BasicBlock *exiting = region->getExiting();

  // The header does not dominate the exiting.
  if (dt->dominates(region->getHeader(), region->getExiting()))
    return;

  // Create a new exiting block.
  BasicBlock *newExiting = BasicBlock::Create(
      exiting->getContext(), exiting->getName() + Twine(".be_split"),
      exiting->getParent(), exiting);
  BranchInst::Create(exiting, newExiting);

  // All the blocks in the region pointing to the exiting are redirected to the new exiting.
  for (DivergentRegion::iterator iter = region->begin(), iterEnd = region->end();
       iter != iterEnd; ++iter) {
    TerminatorInst *terminator = (*iter)->getTerminator();
    for (unsigned int index = 0; index < terminator->getNumSuccessors(); ++index) {
      if (terminator->getSuccessor(index) == exiting) {
        terminator->setSuccessor(index, newExiting);
      }
    }
  }

  // 'newExiting' will contain the phi working on the values from the blocks
  // in the region.
  // 'Exiting' will contain the phi working on the values from the blocks
  // outside and in the region.
  PHIVector oldPhis;
  GetPHIs(exiting, oldPhis);

  PHIVector newPhis;
  PHIVector exitPhis;

  for (PHIVector::iterator I = oldPhis.begin(), E = oldPhis.end(); I != E;
       ++I) {
    PHINode *phi = *I;
    PHINode *newPhi = PHINode::Create(phi->getType(), 0,
                                      phi->getName() + Twine(".new_exiting"),
                                      newExiting->begin());
    PHINode *exitPhi = PHINode::Create(phi->getType(), 0,
                                       phi->getName() + Twine(".old_exiting"),
                                       exiting->begin());
    for (unsigned int index = 0; index < phi->getNumIncomingValues(); ++index) {
      BasicBlock *BB = phi->getIncomingBlock(index);
      if (contains(*region, BB))
        newPhi->addIncoming(phi->getIncomingValue(index), BB);
      else
        exitPhi->addIncoming(phi->getIncomingValue(index), BB);
    }
    newPhis.push_back(newPhi);
    exitPhis.push_back(exitPhi);
  }

  unsigned int phiNumber = newPhis.size();
  for (unsigned int phiIndex = 0; phiIndex < phiNumber; ++phiIndex) {
    // Add the edge coming from the 'newExiting' block to the phi nodes in
    // Exiting.
    PHINode *exitPhi = exitPhis[phiIndex];
    PHINode *newPhi = newPhis[phiIndex];
    exitPhi->addIncoming(newPhi, newExiting);

    // Update all the references to the old Phis to the new ones.
    oldPhis[phiIndex]->replaceAllUsesWith(exitPhi);
  }

  // Delete the old phi nodes.
  for (PHIVector::iterator I = oldPhis.begin(), E = oldPhis.end(); I != E;
       ++I) {
    PHINode *ToDelete = *I;
    ToDelete->eraseFromParent();
  }

  region->setExiting(newExiting);
}

//------------------------------------------------------------------------------
char BranchExtraction::ID = 0;
static RegisterPass<BranchExtraction> X("be", "Extract divergent regions");
