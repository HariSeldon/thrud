//===- SingleDimDivAnalysis.cpp - Single Dimension Divergence Analysis ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "single_dim_div_analysis"

#include "thrud/DivergenceAnalysis/SingleDimDivAnalysis.h"

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

extern cl::opt<int> CoarseningDirection;

SingleDimDivAnalysis::SingleDimDivAnalysis() : FunctionPass(ID) {}

//------------------------------------------------------------------------------
void SingleDimDivAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LoopInfo>();
  AU.addRequired<PostDominatorTree>();
  AU.addRequired<DominatorTree>();
  AU.addRequired<ScalarEvolution>();
  AU.addRequired<NDRange>();
  AU.setPreservesAll();
}

//------------------------------------------------------------------------------
bool SingleDimDivAnalysis::runOnFunction(Function &F) {
  Function *Func = (Function *)&F;
  // Apply the pass to kernels only.
  if (!IsKernel(Func))
    return false;

  PDT = &getAnalysis<PostDominatorTree>();
  DT = &getAnalysis<DominatorTree>();
  SE = &getAnalysis<ScalarEvolution>();
  LI = &getAnalysis<LoopInfo>();
  NDR = &getAnalysis<NDRange>();

//  TIds = FindThreadIds(Func, CoarseningDirection);
  TIds = NDR->getTids();

  AllTIds = NDR->getTids(CoarseningDirection);
  Sizes = NDR->getSizes(CoarseningDirection);
//  AllTIds = FindThreadIds(Func);
//  Sizes = FindSpaceSizes(Func, CoarseningDirection);
//  GroupIds = FindGroupIds(Func);
  Inputs = GetMemoryValues(Func);

  TIdInsts = ForwardCodeSlicing(TIds);

  //TIdInsts = FindThreadDepInst(Func, TIdsV);

  // Find divergent regions.
  Branches = FindBranches(F);
  ValueVector TIdsV = ToValueVector(TIds);
  TIdBranches = GetThreadDepBranches(Branches, TIdsV);
  Regions = GetDivergentRegions(TIdBranches, DT, PDT, LI);
  for (RegionVector::iterator I = Regions.begin(), E = Regions.end(); I != E;
       ++I) {
    DivergentRegion *R = *I;
    R->Analyze(SE, LI, TIdsV, Inputs);
  }

  // Get instructions to replicate.
  InstVector DoNotReplicate;
  DoNotReplicate.reserve(AllTIds.size() + Sizes.size());
  DoNotReplicate.insert(DoNotReplicate.end(), Sizes.begin(), Sizes.end());
  DoNotReplicate.insert(DoNotReplicate.end(), AllTIds.begin(), AllTIds.end());
//  DoNotReplicate.insert(DoNotReplicate.end(), GroupIds.begin(), GroupIds.end());
  ToRep =
      GetInstToReplicateOutsideRegions(TIdInsts, TIds, Regions, DoNotReplicate);

  return false;
}

//------------------------------------------------------------------------------
InstVector SingleDimDivAnalysis::getInstToRepOutsideRegions() const {
  return ToRep;
}

//------------------------------------------------------------------------------
RegionVector SingleDimDivAnalysis::getDivergentRegions() const {
  return Regions;
}

//------------------------------------------------------------------------------
InstVector SingleDimDivAnalysis::getThreadIds() const { 
  llvm::errs() << "SDDA - getThreadIds: \n";
  TIds.size();
  dumpVector(TIds);
  return TIds; }

//------------------------------------------------------------------------------
InstVector SingleDimDivAnalysis::getSizes() const { return Sizes; }

//------------------------------------------------------------------------------
bool SingleDimDivAnalysis::IsThreadIdDependent(Instruction *I) const {
  return IsPresent<Instruction>(I, TIdInsts);
}

//------------------------------------------------------------------------------
InstVector SingleDimDivAnalysis::FindInstToReplicate() {
  return GetInstToReplicate(TIdInsts, TIds, AllTIds);
}

//------------------------------------------------------------------------------
InstVector SingleDimDivAnalysis::getTIdInsts() const { return TIdInsts; }

//------------------------------------------------------------------------------
char SingleDimDivAnalysis::ID = 0;
static RegisterPass<SingleDimDivAnalysis>
    X("sdda", "OpenCL single dimension divergence analysis");
