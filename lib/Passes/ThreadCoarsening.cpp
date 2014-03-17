// Required passes: -mem2reg and -instnamer
// At the end perform CSE / DCE.

#define DEBUG_TYPE "thread_coarsening"

#include "thrud/ThreadCoarsening/ThreadCoarsening.h"

#include "thrud/DivergenceAnalysis/DivergenceAnalysis.h"

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/NDRange.h"
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

// Command line options.
extern cl::opt<unsigned int> CoarseningDirectionCL;
cl::opt<unsigned int> CoarseningFactorCL("coarsening-factor", cl::init(1),
                                         cl::Hidden,
                                         cl::desc("The coarsening factor"));
cl::opt<unsigned int> CoarseningStrideCL("coarsening-stride", cl::init(1),
                                         cl::Hidden,
                                         cl::desc("The coarsening stride"));
cl::opt<std::string> KernelNameCL("kernel-name", cl::init(""), cl::Hidden,
                                  cl::desc("Name of the kernel to coarsen"));
cl::opt<ThreadCoarsening::DivRegionOption> DivRegionOptionCL(
    "div-region-mgt", cl::init(ThreadCoarsening::FullReplication), cl::Hidden,
    cl::desc("Divergent region management"),
    cl::values(clEnumValN(ThreadCoarsening::FullReplication, "classic",
                          "Replicate full region"),
               clEnumValN(ThreadCoarsening::TrueBranchMerging, "merge-true",
                          "Merge true branch"),
               clEnumValN(ThreadCoarsening::FalseBranchMerging, "merge-false",
                          "Merge false branch"),
               clEnumValN(ThreadCoarsening::FullMerging, "merge",
                          "Merge both true and false branches"),
               clEnumValEnd));

//------------------------------------------------------------------------------
ThreadCoarsening::ThreadCoarsening() : FunctionPass(ID) {}

//------------------------------------------------------------------------------
void ThreadCoarsening::getAnalysisUsage(AnalysisUsage &au) const {
  au.addRequired<LoopInfo>();
  au.addRequired<SingleDimDivAnalysis>();
  au.addRequired<PostDominatorTree>();
  au.addRequired<DominatorTree>();
  au.addRequired<NDRange>();
}

//------------------------------------------------------------------------------
bool ThreadCoarsening::runOnFunction(Function &F) {
  // Apply the pass to kernels only.
  if (!IsKernel((const Function *)&F))
    return false;

  // Apply the pass to the selected kernel only.
  std::string FunctionName = F.getName();
  if (KernelNameCL != "" && FunctionName != KernelNameCL)
    return false;

  // Get command line options.
  direction = CoarseningDirectionCL;
  factor = CoarseningFactorCL;
  stride = CoarseningStrideCL;
  divRegionOption = DivRegionOptionCL;

  // Perform analysis.
  loopInfo = &getAnalysis<LoopInfo>();
  pdt = &getAnalysis<PostDominatorTree>();
  dt = &getAnalysis<DominatorTree>();
  sdda = &getAnalysis<SingleDimDivAnalysis>();
  ndr = &getAnalysis<NDRange>();

  errs() << "ThreadCoarsening::runOnFunction\n";
    
  // Transform the kernel.
  scaleNDRange();
  coarsenFunction();
  replacePlaceholders();

  return true;
}

//------------------------------------------------------------------------------
char ThreadCoarsening::ID = 0;
static RegisterPass<ThreadCoarsening>
    X("tc", "OpenCL Thread Coarsening Transformation Pass");

//  for (unsigned int CI = 1; CI < CF; ++CI) {
//    // Mapping between the old instruction in the old region and the
//    // new instructions in the new region. These new values have to be
//    // applied to the instructions duplicated using the current
//    // coarsening index.
//    Map CIMap;
//    // Initialize the map with the TId -> newTId mapping.
//    InitializeMap(CIMap, TIds, newTIds, CI, CF);
//
//    InstPairs InstMapping;
//    DuplicateInsts(Insts, InstMapping, CIMap, CI);
//    InsertReplicatedInst(InstMapping, CIMap);
//
//    // Duplicate divergent regions.
//    Map RegionsMap;
//    ReplicateRegions(Regions, RegionsMap, CI, CIMap);
//
//    // Apply the RegionsMap to the replicated instructions.
//    for (InstPairs::iterator I = InstMapping.begin(), E = InstMapping.end();
//         I != E; ++I) {
//      Instruction *inst = I->second;
//      applyMap(inst, RegionsMap);
//    }
//  }
//
//  // Apply the map to all the instrucions.
//  // This replaces tid with 2 * tid.
//  Map map;
//  InitializeMap(map, TIds, newTIds, 0, CF);
//  for (RegionVector::iterator regionInfo = Regions.begin(), RE =
// Regions.end();
//       regionInfo != RE; ++regionInfo) {
//    DivergentRegion *R = *regionInfo;
//    BlockVector *Blocks = R->getBlocks();
//    for (BlockVector::iterator BI = Blocks->begin(), BE = Blocks->end();
//         BI != BE; ++BI) {
//      BasicBlock *BB = *BI;
//      for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I) {
//        if (!isPresent<Instruction>(I, InstTIds))
//          applyMap(I, map);
//      }
//    }
//  }
//  // Apply the map to all the original divergent instructions.
//  for (InstVector::iterator I = Insts.begin(), E = Insts.end(); I != E; ++I) {
//    applyMap(*I, map);
//  }
