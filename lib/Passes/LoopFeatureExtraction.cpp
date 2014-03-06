#include "thrud/FeatureExtraction/LoopFeatureExtraction.h"

#include "thrud/DivergenceAnalysis/MultiDimDivAnalysis.h"
#include "thrud/DivergenceAnalysis/SingleDimDivAnalysis.h"

#include "thrud/Support/NDRange.h"
#include "thrud/Support/Utils.h"

cl::opt<std::string> loopKernelName("count-loop-kernel-name", cl::init(""),
                                    cl::Hidden,
                                    cl::desc("Name of the kernel to analyze"));

extern int CoarseningDirection;

char OpenCLLoopFeatureExtractor::ID = 0;
static RegisterPass<OpenCLLoopFeatureExtractor>
    X("opencl-loop-instcount", "Collect opencl features in loops");

//------------------------------------------------------------------------------
bool OpenCLLoopFeatureExtractor::runOnFunction(Function &F) {
  if (F.getName() != loopKernelName)
    return false;

  PDT = &getAnalysis<PostDominatorTree>();
  DT = &getAnalysis<DominatorTree>();
  MDDA = &getAnalysis<MultiDimDivAnalysis>();
  SDDA = &getAnalysis<SingleDimDivAnalysis>();
  LI = &getAnalysis<LoopInfo>();
  SE = &getAnalysis<ScalarEvolution>();
  NDR = &getAnalysis<NDRange>();

  visit(F);
  collector.dump();
  return false;
}

//------------------------------------------------------------------------------
void OpenCLLoopFeatureExtractor::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MultiDimDivAnalysis>();
  AU.addRequired<SingleDimDivAnalysis>();
  AU.addRequired<PostDominatorTree>();
  AU.addRequired<DominatorTree>();
  AU.addRequired<LoopInfo>();
  AU.addRequired<ScalarEvolution>();
  AU.addRequired<NDRange>();
  AU.setPreservesAll();
}

//------------------------------------------------------------------------------
// Count all instruction types.
#define HANDLE_INST(N, OPCODE, CLASS)                                          \
  void OpenCLLoopFeatureExtractor::visit##OPCODE(CLASS &I) {                   \
    Instruction *inst = (Instruction *)&I;                                     \
    if (!IsInLoop(inst, LI))                                                   \
      return;                                                                  \
    collector.instTypes[#OPCODE] += 1;                                         \
    collector.instTypes["insts"] += 1;                                         \
  }
#include "llvm/IR/Instruction.def"

//------------------------------------------------------------------------------
void OpenCLLoopFeatureExtractor::visitInstruction(Instruction &inst) {
  errs() << "Unknown instruction: " << inst;
  llvm_unreachable(0);
}

//------------------------------------------------------------------------------
void OpenCLLoopFeatureExtractor::visitBasicBlock(BasicBlock &basicBlock) {
  BasicBlock *block = (BasicBlock *)&basicBlock;
  
  if(!IsInLoop(block, LI))
    return;

  collector.instTypes["blocks"] += 1;
  collector.computeILP(block);
  collector.computeMLP(block, DT, PDT);
  collector.countInstsBlock(basicBlock);
  collector.countConstants(basicBlock);
  collector.countBarriers(basicBlock);
  collector.countMathFunctions(basicBlock);
  collector.countOutgoingEdges(basicBlock);
  collector.countIncomingEdges(basicBlock);
  collector.countLocalMemoryUsage(basicBlock);
  collector.countPhis(basicBlock);
  collector.livenessAnalysis(basicBlock);
  collector.coalescingAnalysis(basicBlock, SE, NDR, CoarseningDirection);
}

//------------------------------------------------------------------------------
void OpenCLLoopFeatureExtractor::visitFunction(Function &function) {
  // Extract ThreadId values. 
//  InstVector tmp = SDDA->getThreadIds();
//  TIds = ToValueVector(tmp);
  collector.loopCountBranches(function, LI);
  collector.loopCountEdges(function, LI);
  collector.loopCountDivInsts(function, MDDA, SDDA, LI);
}
