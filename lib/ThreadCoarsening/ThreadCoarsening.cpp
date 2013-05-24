//===- ThreadCoarsening.cpp - Merge many OpenCL threads into one ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Required passes: -mem2reg and -instnamer
// At the end perform CSE / DCE.

#define DEBUG_TYPE "thread_coarsening"

#include "thrud/ThreadCoarsening/ThreadCoarsening.h"

#include "thrud/DivergenceAnalysis/SingleDimDivAnalysis.h"

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/Utils.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

#include "llvm/Pass.h"

#include "llvm/ADT/ValueMap.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Twine.h"

#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/RegionInfo.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Transforms/Scalar.h"

#include <utility>

using namespace llvm;

STATISTIC(CoarsFactor, "Coarsening factor");
STATISTIC(CoarsDir, "Coarsening direction");
STATISTIC(NumThreadIds, "Number of getThreadId function calls");
STATISTIC(NumDivRegions, "Number of divergent_region");
STATISTIC(NumReplicated, "Number of replicated instructions");

extern cl::opt<int> CoarseningDirection;
extern cl::opt<int> CoarseningFactor;
extern cl::opt<int> Stride;
extern cl::opt<std::string> KernelName;

//------------------------------------------------------------------------------
ThreadCoarsening::ThreadCoarsening() : FunctionPass(ID) {}

//------------------------------------------------------------------------------
void ThreadCoarsening::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LoopInfo>();
  AU.addRequired<SingleDimDivAnalysis>();
  AU.addRequired<PostDominatorTree>();
  AU.addRequired<DominatorTree>();
}

//------------------------------------------------------------------------------
bool ThreadCoarsening::runOnFunction(Function &F) {
  //errs() << "===== ThreadCoarsening =====\n";
  // Apply the pass to kernels only.
  if (!IsKernel((const Function *)&F))
    return false;

  std::string FunctionName = F.getName();
  if(KernelName != "" && FunctionName != KernelName)
    return false;

  // Collect statistics.
  CoarsFactor = CoarseningFactor;
  CoarsDir = CoarseningDirection;
  unsigned int CF = CoarseningFactor;
  unsigned int CD = CoarseningDirection;
  unsigned int ST = Stride;

  // Perform analysis.
  LI = &getAnalysis<LoopInfo>();
  PDT = &getAnalysis<PostDominatorTree>();
  DT = &getAnalysis<DominatorTree>();
  SDDA = &getAnalysis<SingleDimDivAnalysis>();

  // Collect analysis results.
  InstVector TIds = SDDA->getThreadIds();
  InstVector Insts = SDDA->getInstToRepOutsideRegions();

  NumReplicated = Insts.size();

  // Scale sizes and TIds.
  // Instructions used to compute the new tid that contain a reference to
  // a tid.
  InstVector InstTIds; 
  InstVector NewTIds = ScaleSizeAndIds(CD, CF, ST, InstTIds);
  RegionVector Regions = SDDA->getDivergentRegions();
  NumDivRegions = Regions.size();
  BlockVector Preds = BuildPredList(Regions, LI);

  NumReplicated = Insts.size();
  for (RegionVector::iterator I = Regions.begin(), E = Regions.end(); I != E; ++I) {
    NumReplicated += getInstructionNumberInRegion(*I);
  }

  std::vector<RegionBounds*> InsertionPoints = BuildInsertionPoints(Regions);

  for (unsigned int CI = 1; CI < CF; ++CI) {
    // Mapping between the old instruction in the old region and the 
    // new instructions in the new region. These new values have to be
    // applied to the instructions duplicated using the current 
    // coarsening index.
    Map CIMap;
    // Initialize the map with the TId -> NewTId mapping.
    InitializeMap(CIMap, TIds, NewTIds, CI, CF);

    InstPairs InstMapping;
    DuplicateInsts(Insts, InstMapping, CIMap, CI);
    InsertReplicatedInst(InstMapping, CIMap);

    // Duplicate the divergent regions.
    Map RegionsMap;
    for (unsigned int Index = 0; Index < Regions.size(); ++Index) {
      if(Regions[Index]->IsStrict()) continue;

      // Select the current region.
      DivergentRegion *R = Regions[Index];
      // Dump the region.
      //R->dump();

      RegionBounds &IP = *(InsertionPoints[Index]);
        
      // Clone the region and apply the new map.
      // CIMap is applied to all the blocks in the region.
      RegionBounds NewPair = CloneRegion(R->getBounds(), 
                                         ".." + Twine(CI) + "..", 
                                         DT, RegionsMap, CIMap);
      // Build the mapping for the phi nodes in the exiting block.
      BuildExitingPhiMap(R->getExiting(), NewPair.getExiting(), RegionsMap);

      // Exiting -> NewPair.first
      BasicBlock *Exiting = IP.getHeader();
      ChangeBlockTarget(Exiting, NewPair.getHeader());
      // NewPair.second -> IP.second
      ChangeBlockTarget(NewPair.getExiting(), IP.getExiting());
      // IP.first -> NewPair.second
      IP.setHeader(NewPair.getExiting());

      // Update the phi nodes of the newly inserted header. 
      RemapBlocksInPHIs(NewPair.getHeader(), Preds[Index], Exiting);
      // Update the phi nodes in the exit block.
      RemapBlocksInPHIs(IP.getExiting(), R->getExiting(), NewPair.getExiting());
    }

    // Apply the RegionsMap to the replicated instructions.
    for (InstPairs::iterator I = InstMapping.begin(), E = InstMapping.end(); 
         I != E; ++I) {
      Instruction *Inst = I->second;
      ApplyMap(Inst, RegionsMap);             
    }

  }

  // Apply the map to all the instrucions.
  // This replaces tid with 2 * tid.
  Map map;
  InitializeMap(map, TIds, NewTIds, 0, CF);
  for (RegionVector::iterator RI = Regions.begin(), RE = Regions.end(); 
       RI != RE; ++RI) {
    DivergentRegion *R = *RI;
    BlockVector *Blocks = R->getBlocks();
    for (BlockVector::iterator BI = Blocks->begin(), BE = Blocks->end();
         BI != BE; ++BI) {
      BasicBlock *BB = *BI;
      for (BasicBlock::iterator I = BB->begin(), E = BB->end();
           I != E; ++I) {
        if(!IsPresent<Instruction>(I, InstTIds))
          ApplyMap(I, map);
      }
    }
  }
  // Apply the map to all the original divergent instructions.
  for (InstVector::iterator I = Insts.begin(), E = Insts.end(); 
       I != E; ++I) {
    ApplyMap(*I, map);
  }

  return false;
}

//------------------------------------------------------------------------------
void ThreadCoarsening::DuplicateInsts(InstVector &Insts, InstPairs &IP, 
                                      Map &map, unsigned int CI) {
  for (InstVector::iterator I = Insts.begin(), E = Insts.end(); I != E; ++I) {
    Instruction *Inst = *I;
    Instruction *New = Inst->clone();
    RenameInstructionWithIndex(New, Inst->getName(), CI);
    map[Inst] = New;
    IP.insert(std::make_pair(Inst, New));
  }
}

//------------------------------------------------------------------------------
void ThreadCoarsening::RenameInstructionWithIndex(Instruction *I, 
                                                  StringRef oldName,
                                                  unsigned int index) {
  if(!oldName.empty()) I->setName(oldName + ".." + Twine(index) + "..");
}

//------------------------------------------------------------------------------
InstVector ThreadCoarsening::createOffsetInsts(Value *tId,
                                               unsigned int CoarseningFactor,
                                               unsigned int index) {
  std::vector<Instruction*> Result;
  Instruction *Mul = getMulInst(tId, CoarseningFactor);
  Instruction *Add = getAddInst(Mul, index);
  Result.push_back(Mul);
  Result.push_back(Add);
  return Result;
}

//------------------------------------------------------------------------------
InstVector ThreadCoarsening::InsertIdOffset(unsigned int CD, unsigned int CF, 
                                            unsigned int ST, 
                                            InstVector &InstsTid) {
  unsigned int logST = log2(ST);
  unsigned int CFST = CF * ST;
  unsigned int ST1 = ST - 1;
 
  InstVector Result;
  InstVector TIds = SDDA->getThreadIds();
  NumThreadIds = TIds.size();
  for (InstVector::iterator I = TIds.begin(), E = TIds.end(); I != E; ++I) {
    Instruction *Shift = getShiftInst(*I, logST);
    Shift->insertAfter(*I);
    Instruction *Mul = getMulInst(Shift, CFST);
    Mul->insertAfter(Shift);
    Instruction *And = getAndInst(*I, ST1);
    And->insertAfter(Mul);
    Instruction* Base = getAddInst(And, Mul);
    Base->insertAfter(And);

//    Instruction* Mul = getMulInst(*I, CF);
//    Mul->insertAfter(*I);
    for (unsigned int index = CF; index >= 2; --index) {
      Instruction *Add = getAddInst(Base, (index - 1) * ST);
      Add->insertAfter(Base);
      Result.push_back(Add);
    }
    Result.push_back(Base);

    InstsTid.push_back(Shift);
    InstsTid.push_back(And);
  }
  return Result;
}

//------------------------------------------------------------------------------
void ThreadCoarsening::InsertSizeScale(unsigned int CD, unsigned int CF, 
                                       unsigned int ST) {
  InstVector SizeInsts = SDDA->getSizes();
  for (InstVector::iterator I = SizeInsts.begin(), E = SizeInsts.end(); 
       I != E; ++I) {
    Instruction* Mul = getMulInst(*I, CF);
    Mul->insertAfter(*I);
    SubstituteUsages(*I, Mul);
  }
}

//------------------------------------------------------------------------------
void ThreadCoarsening::InsertReplicatedInst(InstPairs &IP, const Map &map) {
  for (InstPairs::iterator I = IP.begin(), E = IP.end(); I != E; ++I) {
    Instruction *Old = I->first;
    Instruction *New = I->second;
    ApplyMap(New, map);
    New->insertAfter(Old);
  }
}

//------------------------------------------------------------------------------
InstVector ThreadCoarsening::ScaleSizeAndIds(unsigned int CD, unsigned int CF, 
                                             unsigned int ST, 
                                             InstVector& InstsTid) {
  // Insert the scaling factor after each query of the Size id.
  InsertSizeScale(CD, CF, ST);

  // For each TId insert the computation of the offsets. 
  return InsertIdOffset(CD, CF, ST, InstsTid);
}

//------------------------------------------------------------------------------
char ThreadCoarsening::ID = 0;
static RegisterPass<ThreadCoarsening> X(
       "tc", 
       "OpenCL Thread Coarsening Transformation Pass");
