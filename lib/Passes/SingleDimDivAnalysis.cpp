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

  llvm::errs() << "CD: " << CoarseningDirection << "\n";

  TIds = FindThreadIds(Func, CoarseningDirection);

  AllTIds = FindThreadIds(Func);
  Sizes = FindSpaceSizes(Func, CoarseningDirection);
  GroupIds = FindGroupIds(Func);
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
  DoNotReplicate.reserve(AllTIds.size() + Sizes.size() + GroupIds.size());
  DoNotReplicate.insert(DoNotReplicate.end(), Sizes.begin(), Sizes.end());
  DoNotReplicate.insert(DoNotReplicate.end(), AllTIds.begin(), AllTIds.end());
  DoNotReplicate.insert(DoNotReplicate.end(), GroupIds.begin(), GroupIds.end());
  ToRep =
      GetInstToReplicateOutsideRegions(TIdInsts, TIds, Regions, DoNotReplicate);

  return false;
}

// TO REMOVE
//------------------------------------------------------------------------------
//void SingleDimDivAnalysis::AnalyzeRegion(DivergentRegion *Region) {
//  BasicBlock *UB = Region->getHeader();
//  if(UB->size() == 1) {
//    Instruction *Inst = UB->begin();
//    if(BranchInst *Branch = dyn_cast<BranchInst>(Inst)) {
//      if(DependsOn(Branch, Inputs)) {
//        Region->setCondition(DivergentRegion::ND);
//        return;
//      }
//      Value *Cond = Branch->getCondition();
//      if(CmpInst* Cmp = dyn_cast<CmpInst>(Cond)) {
//        ////errs() << AnalyzeCmp(Cmp) << "\n";
//        AnalyzeCmp(Cmp);
//        //Region->setCondition(AnalyzeCmp(Cmp));
//      }
//      else {
//        // FIXME: I am not considering compound conditions.
//        Region->setCondition(DivergentRegion::ND);
//      }
//    }
//  }
//}
//
////------------------------------------------------------------------------------
//DivergentRegion::BoundCheck SingleDimDivAnalysis::AnalyzeCmp(CmpInst *Cmp) {
//  if(Cmp->isEquality())
//    return DivergentRegion::EQ;
//
//  Value *TIdOp = GetTIdOperand(Cmp);
//  // Get the operand position.
//  unsigned int position = GetOperandPosition(Cmp, TIdOp);
//  bool isFirst = (position == 0);
//  // Get the comparison sign.
//  bool GT = IsGreaterThan(Cmp->getPredicate());
//  // Get the TID subscript sign.
//  ValueVector VV = ToValueVector(TIds);
//
//  SmallPtrSet<const SCEV*, 8> Processed;
//  unsigned int result = AnalyzeSubscript(SE->getSCEV(TIdOp), VV, Processed);
//  ////errs() << "Result: " << result << "\n";
//  if (result == 0)
//    return DivergentRegion::ND;
//  //bool IsTIdPositive = (result == 1);
//
//  // Compare all of the previous.
//  unsigned int sum = isFirst + GT; // + IsTIdPositive;
//
//  if(sum % 2 == 0)
//    return DivergentRegion::UB;
//  else
//    return DivergentRegion::LB;
//
//}
//
////------------------------------------------------------------------------------
//Value *SingleDimDivAnalysis::GetTIdOperand(CmpInst* Cmp) {
//  // ASSUMPTION: only one operand of the comparison depends on the TId.
//  ValueVector TIdsV = ToValueVector(TIds);
//  for (CmpInst::op_iterator I = Cmp->op_begin(), E = Cmp->op_end();
//       I != E; ++I) {
//    Value *V = I->get();
//    if(DependsOn(V, TIdsV)) {
//      return *I;
//    }
//  }
//  return NULL;
//}

//------------------------------------------------------------------------------
InstVector SingleDimDivAnalysis::getInstToRepOutsideRegions() const {
  return ToRep;
}

//------------------------------------------------------------------------------
RegionVector SingleDimDivAnalysis::getDivergentRegions() const {
  return Regions;
}

//------------------------------------------------------------------------------
InstVector SingleDimDivAnalysis::getThreadIds() const { return TIds; }

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
