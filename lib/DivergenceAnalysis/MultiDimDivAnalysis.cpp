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

STATISTIC(NumRegions, "Number of divergent regions");
STATISTIC(NumDataRegions, "Number of data dependent regions");
STATISTIC(NumLBRegions, "Number of regions checking for LB");
STATISTIC(NumUBRegions, "Number of regions checking for UB");
STATISTIC(NumEQRegions, "Number of regions checking for EQ");
STATISTIC(UnknownRegions, "Number of unknown regions");

MultiDimDivAnalysis::MultiDimDivAnalysis() : FunctionPass(ID) {}

//------------------------------------------------------------------------------
void MultiDimDivAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LoopInfo>();
  AU.addRequired<PostDominatorTree>();
  AU.addRequired<DominatorTree>();
  AU.addRequired<ScalarEvolution>();
  AU.setPreservesAll();
}

//------------------------------------------------------------------------------
bool MultiDimDivAnalysis::runOnFunction(Function &F) {
  Function *Func = (Function *) &F;

  // Apply the pass to kernels only.
  if (!IsKernel(Func))
    return false;

  PostDominatorTree *PDT = &getAnalysis<PostDominatorTree>();
  DominatorTree *DT = &getAnalysis<DominatorTree>();
  ScalarEvolution *SE = &getAnalysis<ScalarEvolution>();
  LoopInfo *LI = &getAnalysis<LoopInfo>();

  AllTIds = FindThreadIds(Func);
  Sizes = FindSpaceSizes(Func);
  GroupIds = FindGroupIds(Func);
  Inputs = GetMemoryValues(Func);

  TIdInsts = ForwardCodeSlicing(AllTIds);

  // Find divergent regions.
  Branches = FindBranches(F);
  ValueVector AllTIdsV = ToValueVector(AllTIds);
  TIdBranches = GetThreadDepBranches(Branches, AllTIdsV);
  Regions = GetDivergentRegions(TIdBranches, DT, PDT, LI);

  NumRegions = Regions.size();
  NumDataRegions = 0;
  NumLBRegions = 0;
  NumUBRegions = 0;
  NumEQRegions = 0;
  UnknownRegions = 0;

  for (RegionVector::iterator I = Regions.begin(), E = Regions.end(); 
       I != E; ++I) {
    DivergentRegion *R = *I;
    R->Analyze(SE, LI, AllTIdsV, Inputs);
    DivergentRegion::BoundCheck BC = R->getCondition();
    switch(BC) {
      case DivergentRegion::DATA: {
        NumDataRegions++;
        break;
      }
      case DivergentRegion::LB: {
        NumLBRegions++;
        break;
      }
      case DivergentRegion::UB: {
        NumUBRegions++;
        break;
      }
      case DivergentRegion::EQ: {
        NumEQRegions++;
        break;
      }
      default:
        UnknownRegions++;
    }
  }

  errs() << NumRegions << " " << NumDataRegions << " " << UnknownRegions
         << " " << NumUBRegions << " " << NumEQRegions << " " 
         << NumLBRegions << "\n";

  return false;
}

//------------------------------------------------------------------------------
RegionVector MultiDimDivAnalysis::getDivergentRegions() const {
  return Regions;
}

//------------------------------------------------------------------------------
InstVector MultiDimDivAnalysis::getThreadIds() const {
  return AllTIds;
}

//------------------------------------------------------------------------------
InstVector MultiDimDivAnalysis::getSizes() const {
  return Sizes;
}

//------------------------------------------------------------------------------
bool MultiDimDivAnalysis::IsThreadIdDependent(Instruction *I) const {
  return IsPresent<Instruction>(I, TIdInsts);
}

//------------------------------------------------------------------------------
char MultiDimDivAnalysis::ID = 0;
static RegisterPass<MultiDimDivAnalysis> X(
       "mdda", 
       "OpenCL multi dimension divergence analysis");
