#include "thrud/DivergenceAnalysis/DivergenceAnalysis.h"

#include "thrud/Support/DivergentRegion.h"
#include "thrud/Support/Utils.h"

#include "llvm/Pass.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"

#include "llvm/ADT/Statistic.h"

#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/raw_ostream.h"

#include <utility>

using namespace llvm;

// Support functions.
// -----------------------------------------------------------------------------
void findUsesOf(Instruction *inst, InstSet &result);
Instruction *findOutermostBranch(InstVector &insts, const DominatorTree *dt);
InstVector findOutermostBranches(InstVector &branches, const DominatorTree *dt,
                                 const PostDominatorTree *pdt);

// DivergenceAnalysis.
// -----------------------------------------------------------------------------
DivergenceAnalysis::DivergenceAnalysis() : FunctionPass(ID) {}
DivergenceAnalysis::~DivergenceAnalysis() {}

void DivergenceAnalysis::getAnalysisUsage(AnalysisUsage &au) const {
  au.addRequired<LoopInfo>();
  au.addRequired<PostDominatorTree>();
  au.addRequired<DominatorTree>();
  au.addRequired<NDRange>();
  au.setPreservesAll();
}

bool DivergenceAnalysis::runOnFunction(Function &functionRef) {
  Function *function = (Function *)&functionRef;
  // Apply the pass to kernels only.
  if (!IsKernel(function))
    return false;

  pdt = &getAnalysis<PostDominatorTree>();
  dt = &getAnalysis<DominatorTree>();
  loopInfo = &getAnalysis<LoopInfo>();
  ndr = &getAnalysis<NDRange>();

  performAnalysis();
  findBranches();
  findRegions();
  findExternalInsts();

  return false;
}

InstVector DivergenceAnalysis::getTids() {
  // This must be overriden by all subclasses.
  return InstVector();
}

void DivergenceAnalysis::performAnalysis() {
  InstDeque workQueue;
  InstVector seeds = getTids();

  workQueue.assign(seeds.begin(), seeds.end());

  while (!workQueue.empty()) {
    Instruction *inst = workQueue.front();
    workQueue.pop_front();

    divInsts.push_back(inst);

    InstSet users;

    // Manage branches.
    if (isa<BranchInst>(inst)) {
      BasicBlock *block = findImmediatePostDom(inst->getParent(), pdt);
      for (BasicBlock::iterator inst = block->begin(); isa<PHINode>(inst);
           ++inst) {
        users.insert(inst);
      }
    }

    // Add users of the current instruction to the work list.
    findUsesOf(inst, users);
    workQueue.insert(workQueue.end(), users.begin(), users.end());
  }
}

void DivergenceAnalysis::findBranches() {
  // Find all branches.
  for (InstVector::iterator iter = divInsts.begin(), iterEnd = divInsts.end();
       iter != iterEnd; ++iter) {
    if (isa<BranchInst>(*iter)) {
      divBranches.push_back(*iter);
    }
  }

//  while (BranchInst *Top = findOutermostBranch(Branches, dt)) {
//    Result.push_back(Top);
//    Remove(Branches, Top);
//    BasicBlock *TopBlock = Top->getParent();
//    BasicBlock *Exiting = findImmediatePostDom(TopBlock, PDT);
//    BranchSet ToRemove;
//    for (BranchSet::iterator I = Branches.begin(), E = Branches.end(); I != E;
//         ++I) {
//      BranchInst *Branch = *I;
//      BasicBlock *BB = Branch->getParent();
//      if (BB != TopBlock && BB != Exiting &&
//          IsInRegion(TopBlock, Exiting, BB, DT, PDT))
//        ToRemove.insert(Branch);
//    }
//    Remove(Branches, ToRemove);
//  }
//  return Result;
  
  

}

void DivergenceAnalysis::findRegions() {
//  BranchSet allBranches(branches.begin(), branches.end());
//  BranchVector Bs = findOutermostBranches(allBranches, dt, pdt);
//
//  for (BranchVector::iterator iter = Bs.begin(), iterEnd = Bs.end();
//       iter != iterEnd; ++iter) {
//    BasicBlock *block = (*iter)->getParent();
//    BasicBlock *postDom = findImmediatePostDom(block, pdt);
//
//    if (loopInfo->isLoopHeader(block)) {
//      Loop *loop = loopInfo->getLoopFor(block);
//      if (loop == loopInfo->getLoopFor(postDom))
//        postDom = loop->getExitBlock();
//    }
//
//    regions.push_back(new DivergentRegion(block, postDom));
//  }
//
//  // FIXME: remove this adding fill region to the region construction.
//  for (RegionVector::iterator iter = regions.begin(), iterEnd = regions.end();
//       iter != iterEnd; ++iter) {
//    (*iter)->fillRegion(dt, pdt);
//  }
}

void DivergenceAnalysis::findExternalInsts() {
}

// Support functions.
//------------------------------------------------------------------------------
void findUsesOf(Instruction *inst, InstSet &result) {
  for (Instruction::use_iterator useIter = inst->use_begin(),
                                 useEnd = inst->use_end();
       useIter != useEnd; ++useIter) {
    if (Instruction *useInst = dyn_cast<Instruction>(*useIter)) {
      result.insert(useInst);
    }
  }
}

//------------------------------------------------------------------------------
Instruction *findOutermostBranch(InstSet &insts, const DominatorTree *dt) {
//  for (InstSet::iterator I = Bs.begin(), E = Bs.end(); I != E; ++I) {
//    BranchInst *B = *I;
//    if (!IsDominated(B, Bs, DT))
//      return B;
//  }
//  return NULL;
}

////------------------------------------------------------------------------------
//BranchVector FindOutermostBranches(BranchSet &Branches, const DominatorTree *dt,
//                                   const PostDominatorTree *pdt) {
//  BranchVector Result;
//  while (BranchInst *Top = FindOutermostBranch(Branches, DT)) {
//    Result.push_back(Top);
//    Remove(Branches, Top);
//    BasicBlock *TopBlock = Top->getParent();
//    BasicBlock *Exiting = findImmediatePostDom(TopBlock, PDT);
//    BranchSet ToRemove;
//    for (BranchSet::iterator I = Branches.begin(), E = Branches.end(); I != E;
//         ++I) {
//      BranchInst *Branch = *I;
//      BasicBlock *BB = Branch->getParent();
//      if (BB != TopBlock && BB != Exiting &&
//          IsInRegion(TopBlock, Exiting, BB, DT, PDT))
//        ToRemove.insert(Branch);
//    }
//    Remove(Branches, ToRemove);
//  }
//  return Result;
//}

char DivergenceAnalysis::ID = 0;
static RegisterPass<DivergenceAnalysis>
    X("divergence-analysis", "OpenCL divergence analysis");
