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

extern cl::opt<unsigned int> CoarseningDirectionCL;

// Support functions.
// -----------------------------------------------------------------------------
void findUsesOf(Instruction *inst, InstSet &result);
bool isOutermost(Instruction *inst, RegionVector &regions);
bool isOutermost(DivergentRegion *region, RegionVector &regions);

// DivergenceAnalysis.
// -----------------------------------------------------------------------------
void DivergenceAnalysis::init() {
  divInsts.clear();
  outermostDivInsts.clear();
  divBranches.clear();
  regions.clear();
  outermostRegions.clear();
}

InstVector DivergenceAnalysis::getTids() {
  // This must be overriden by all subclasses.
  return InstVector();
}

void DivergenceAnalysis::performAnalysis() {
  InstVector seeds = getTids();
  InstSet worklist(seeds.begin(), seeds.end());

  while (!worklist.empty()) {
    InstSet::iterator iter = worklist.begin();
    Instruction *inst = *iter;
    worklist.erase(iter);
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

    findUsesOf(inst, users);
    // Add users of the current instruction to the work list.
    for (InstSet::iterator iter = users.begin(), iterEnd = users.end();
         iter != iterEnd; ++iter) {
      if (!isPresent(*iter, divInsts))
        worklist.insert(*iter);
    }
  }

  errs() << "DivergenceAnalysis::performAnalysis\n";
  dumpVector(divInsts);

}

void DivergenceAnalysis::findBranches() {
  // Find all branches.
  for (InstVector::iterator iter = divInsts.begin(), iterEnd = divInsts.end();
       iter != iterEnd; ++iter) {
    if (isa<BranchInst>(*iter)) {
      divBranches.push_back(*iter);
    }
  }
}

void DivergenceAnalysis::findRegions() {
  for (InstVector::iterator iter = divBranches.begin(),
                            iterEnd = divBranches.end();
       iter != iterEnd; ++iter) {
    BasicBlock *header = (*iter)->getParent();
    BasicBlock *exiting = findImmediatePostDom(header, pdt);

    if (loopInfo->isLoopHeader(header)) {
      Loop *loop = loopInfo->getLoopFor(header);
      if (loop == loopInfo->getLoopFor(exiting))
        exiting = loop->getExitBlock();
    }

    regions.push_back(new DivergentRegion(header, exiting, dt, pdt));
  }
}

// This is called only when the outermost instructions are acutally requrested,
// ie. during coarsening. This is done to be sure that this instructions are
// computed after the extraction of divergent regions from the CFG.
void DivergenceAnalysis::findOutermostInsts() {
  errs() << "DivergenceAnalysis::findOutermostInsts\n";
  outermostDivInsts.clear();
//  dumpVector(divInsts);
  for (InstVector::iterator iter = divInsts.begin(), iterEnd = divInsts.end();
       iter != iterEnd; ++iter) {
    Instruction *inst = *iter;
    inst->dump();
    errs() << isOutermost(inst, regions) << "\n";
    if (isOutermost(inst, regions)) {
      inst->dump();
      outermostDivInsts.push_back(inst);
    }
  }

  // Remove from outermostDivInsts all the calls to builtin functions.
  InstVector oclIds = ndr->getTids();
  InstVector result;

  size_t oldSize = outermostDivInsts.size();

  std::sort(outermostDivInsts.begin(), outermostDivInsts.end());
  std::sort(oclIds.begin(), oclIds.end());
  std::set_difference(outermostDivInsts.begin(), outermostDivInsts.end(),
                      oclIds.begin(), oclIds.end(), std::back_inserter(result));
  outermostDivInsts.swap(result);

  assert(outermostDivInsts.size() <= oldSize && "Wrong set difference");
}

void DivergenceAnalysis::findOutermostRegions() {
  outermostRegions.clear();
  for (RegionVector::iterator iter = regions.begin(), iterEnd = regions.end();
       iter != iterEnd; ++iter) {
    if (isOutermost(*iter, regions)) {
      outermostRegions.push_back(*iter);
    }
  }
}

// Public functions.
//------------------------------------------------------------------------------
InstVector &DivergenceAnalysis::getDivInsts() { return divInsts; }

InstVector &DivergenceAnalysis::getOutermostDivInsts() {
  // Use memoization.
  if (outermostDivInsts.empty())
    findOutermostInsts();
  return outermostDivInsts;
}

InstVector DivergenceAnalysis::getDivInsts(DivergentRegion *region,
                                           unsigned int branchIndex) {
  InstVector result;
  DivergentRegion &r = *region;

  for (InstVector::iterator iter = divInsts.begin(), iterEnd = divInsts.end();
       iter != iterEnd; ++iter) {
    Instruction *inst = *iter;
    if (containsInternally(r, inst)) {
      result.push_back(inst);
    }
  }

  return result;
}

RegionVector &DivergenceAnalysis::getDivRegions() { return regions; }

RegionVector &DivergenceAnalysis::getOutermostDivRegions() {
  // Use memoization.
  if (outermostRegions.empty()) {
    findOutermostRegions();
  }
  return outermostRegions;
}

RegionVector DivergenceAnalysis::getDivRegions(DivergentRegion *region,
                                               unsigned int branchIndex) {
  RegionVector result;
  DivergentRegion &r = *region;
  for (RegionVector::iterator iter = regions.begin(), iterEnd = regions.end();
       iter != iterEnd; ++iter) {
    DivergentRegion *currentRegion = *iter;
    if (containsInternally(r, currentRegion)) {
      result.push_back(currentRegion);
    }
  }
  return result;
}

bool DivergenceAnalysis::isDivergent(Instruction *inst) {
  return isPresent(inst, divInsts);
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

bool isOutermost(Instruction *inst, RegionVector &regions) {
  bool result = false;
  for (RegionVector::const_iterator iter = regions.begin(),
                                    iterEnd = regions.end();
       iter != iterEnd; ++iter) {
    DivergentRegion *region = *iter;
    result |= contains(*region, inst);
  }
  return !result;
}

bool isOutermost(DivergentRegion *region, RegionVector &regions) {
  Instruction *inst = region->getHeader()->getTerminator();
  bool result = false;
  for (RegionVector::const_iterator iter = regions.begin(),
                                    iterEnd = regions.end();
       iter != iterEnd; ++iter) {
    DivergentRegion *region = *iter;
    result |= containsInternally(*region, inst);
  }
  return !result;
}

// SingleDimDivAnalysis
//------------------------------------------------------------------------------
SingleDimDivAnalysis::SingleDimDivAnalysis() : FunctionPass(ID) {}

void SingleDimDivAnalysis::getAnalysisUsage(AnalysisUsage &au) const {
  au.addRequired<LoopInfo>();
  au.addPreserved<LoopInfo>();
  au.addRequired<PostDominatorTree>();
  au.addRequired<DominatorTree>();
  au.addRequired<NDRange>();
  au.addRequired<ControlDependenceAnalysis>();
  //  au.setPreservesAll();
}

bool SingleDimDivAnalysis::runOnFunction(Function &functionRef) {
  Function *function = (Function *)&functionRef;
  // Apply the pass to kernels only.
  if (!IsKernel(function))
    return false;

  init();
  pdt = &getAnalysis<PostDominatorTree>();
  dt = &getAnalysis<DominatorTree>();
  loopInfo = &getAnalysis<LoopInfo>();
  ndr = &getAnalysis<NDRange>();
  cda = &getAnalysis<ControlDependenceAnalysis>();

  performAnalysis();
  findBranches();
  findRegions();

  return false;
}

InstVector SingleDimDivAnalysis::getTids() {
  return ndr->getTids(CoarseningDirectionCL);
}

char SingleDimDivAnalysis::ID = 0;
static RegisterPass<SingleDimDivAnalysis> X("sdda",
                                            "Single divergence analysis");

// MultiDimDivAnalysis
//------------------------------------------------------------------------------
MultiDimDivAnalysis::MultiDimDivAnalysis() : FunctionPass(ID) {}

void MultiDimDivAnalysis::getAnalysisUsage(AnalysisUsage &au) const {
  au.addRequired<LoopInfo>();
  au.addRequired<PostDominatorTree>();
  au.addRequired<DominatorTree>();
  au.addRequired<NDRange>();
  au.addRequired<ControlDependenceAnalysis>();
  //  au.setPreservesAll();
}

bool MultiDimDivAnalysis::runOnFunction(Function &functionRef) {
  Function *function = (Function *)&functionRef;
  // Apply the pass to kernels only.
  if (!IsKernel(function))
    return false;

  init();
  pdt = &getAnalysis<PostDominatorTree>();
  dt = &getAnalysis<DominatorTree>();
  loopInfo = &getAnalysis<LoopInfo>();
  ndr = &getAnalysis<NDRange>();
  cda = &getAnalysis<ControlDependenceAnalysis>();

  performAnalysis();
  findBranches();
  findRegions();

  return false;
}

InstVector MultiDimDivAnalysis::getTids() { return ndr->getTids(); }

char MultiDimDivAnalysis::ID = 0;
static RegisterPass<MultiDimDivAnalysis>
    Y("mdda", "Multidimensional divergence analysis");
