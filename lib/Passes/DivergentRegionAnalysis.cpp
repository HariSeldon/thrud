//===- DivergentRegionAnalysis.cpp - Count branches depending on TID ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "branch_analysis"

#include "thrud/DivergenceAnalysis/DivergentRegionAnalysis.h"

#include "thrud/Support/Utils.h"

#include "llvm/Pass.h"

#include "llvm/ADT/Statistic.h"

#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/raw_ostream.h"

#include <utility>

using namespace llvm;

cl::opt<int>
CoarseningDirection("coarsening-direction", cl::init(-1), cl::Hidden,
                    cl::desc("The coarsening direction"));

//------------------------------------------------------------------------------
DivergentRegionAnalysis::DivergentRegionAnalysis() : FunctionPass(ID) { }

//------------------------------------------------------------------------------
void DivergentRegionAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LoopInfo>();
  AU.addRequired<PostDominatorTree>();
  AU.addRequired<DominatorTree>();
  AU.setPreservesAll();
}

//------------------------------------------------------------------------------
bool DivergentRegionAnalysis::runOnFunction(Function &F) {
  errs() << "DivergentRegionAnalysis.\n";

  // Apply the pass to kernels only.
  if (!IsKernel((const Function *)&F))
    return false;

  PostDominatorTree *PDT = &getAnalysis<PostDominatorTree>();
  DominatorTree *DT = &getAnalysis<DominatorTree>();
  LoopInfo *LI = &getAnalysis<LoopInfo>();
  unsigned int CD = CoarseningDirection;

  // Find all branches.
  BranchVector Branches = FindBranches(F);
  // Find TIds.
  InstVector TIds = FindThreadIds((Function *)&F, CD);

  // Find the branches that depend on the thread Id.
  ValueVector VTIds = ToValueVector(TIds);
  BranchVector TIdBs = GetThreadDepBranches(Branches, VTIds);

  Regions = GetDivergentRegions(TIdBs, DT, PDT, LI);

  return false;
}

//------------------------------------------------------------------------------
std::vector<DivergentRegion*> DivergentRegionAnalysis::getRegions() {
  return Regions;
}

//------------------------------------------------------------------------------
char DivergentRegionAnalysis::ID = 0;
static RegisterPass<DivergentRegionAnalysis> X(
       "dra",
       "OpenCL divergent region analysis");
