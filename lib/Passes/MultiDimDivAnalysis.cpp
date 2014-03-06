//===- MultiDimDivAnalysis.cpp - Multi Dimension Divergence Analysis ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "multi_dim_div_analysis"

#include "thrud/DivergenceAnalysis/MultiDimDivAnalysis.h"

#include "thrud/Support/DivergentRegion.h"
#include "thrud/Support/Utils.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"

#include "llvm/Pass.h"

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

MultiDimDivAnalysis::MultiDimDivAnalysis() : FunctionPass(ID) {}

//------------------------------------------------------------------------------
void MultiDimDivAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LoopInfo>();
  AU.addRequired<PostDominatorTree>();
  AU.addRequired<DominatorTree>();
  AU.addRequired<ScalarEvolution>();
  AU.addRequired<NDRange>();
  AU.setPreservesAll();
}

//------------------------------------------------------------------------------
bool MultiDimDivAnalysis::runOnFunction(Function &F) {
  Function *Func = (Function *)&F;

  // Apply the pass to kernels only.
  if (!IsKernel(Func))
    return false;

  PDT = &getAnalysis<PostDominatorTree>();
  DT = &getAnalysis<DominatorTree>();
  SE = &getAnalysis<ScalarEvolution>();
  LI = &getAnalysis<LoopInfo>();
  NDR = &getAnalysis<NDRange>();

  AllTIds = NDR->getTids();
  Sizes = NDR->getSizes();
//  AllTIds = FindThreadIds(Func);
//  Sizes = FindSpaceSizes(Func);
//  GroupIds = FindGroupIds(Func);
  Inputs = GetMemoryValues(Func);

  TIdInsts = ForwardCodeSlicing(AllTIds);

  // Find divergent regions.
  Branches = FindBranches(F);
  ValueVector AllTIdsV = ToValueVector(AllTIds);
  TIdBranches = GetThreadDepBranches(Branches, AllTIdsV);

  Regions = GetDivergentRegions(TIdBranches, DT, PDT, LI);

  // Get instructions to replicate.
  InstVector DoNotReplicate;
//  DoNotReplicate.reserve(AllTIds.size() + Sizes.size() + GroupIds.size());
  DoNotReplicate.reserve(AllTIds.size() + Sizes.size());
  DoNotReplicate.insert(DoNotReplicate.end(), Sizes.begin(), Sizes.end());
  DoNotReplicate.insert(DoNotReplicate.end(), AllTIds.begin(), AllTIds.end());
//  DoNotReplicate.insert(DoNotReplicate.end(), GroupIds.begin(), GroupIds.end());
  ToRep = GetInstToReplicateOutsideRegionCores(TIdInsts, AllTIds, Regions,
                                               DoNotReplicate);

  return false;
}

//------------------------------------------------------------------------------
RegionVector MultiDimDivAnalysis::getDivergentRegions() const {
  return Regions;
}

//------------------------------------------------------------------------------
InstVector MultiDimDivAnalysis::getThreadIds() const { return AllTIds; }

//------------------------------------------------------------------------------
InstVector MultiDimDivAnalysis::getSizes() const { return Sizes; }

//------------------------------------------------------------------------------
bool MultiDimDivAnalysis::IsThreadIdDependent(Instruction *I) const {
  return IsPresent<Instruction>(I, TIdInsts);
}

//------------------------------------------------------------------------------
InstVector MultiDimDivAnalysis::getToRep() const { return ToRep; }

//------------------------------------------------------------------------------
char MultiDimDivAnalysis::ID = 0;
static RegisterPass<MultiDimDivAnalysis>
    X("mdda", "OpenCL multi dimension divergence analysis");
