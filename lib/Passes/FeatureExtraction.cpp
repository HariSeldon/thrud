#include "thrud/FeatureExtraction/FeatureExtraction.h"

#include "llvm/Analysis/ScalarEvolution.h"

#include "thrud/DivergenceAnalysis/MultiDimDivAnalysis.h"
#include "thrud/DivergenceAnalysis/SingleDimDivAnalysis.h"
#include "thrud/Support/NDRange.h"
#include "thrud/Support/Utils.h"

cl::opt<std::string> kernelName("count-kernel-name", cl::init(""), cl::Hidden,
                                cl::desc("Name of the kernel to analyze"));

extern cl::opt<int> CoarseningDirection;

char OpenCLFeatureExtractor::ID = 0;
static RegisterPass<OpenCLFeatureExtractor> X("opencl-instcount",
                                              "Collect opencl features");

//------------------------------------------------------------------------------
bool OpenCLFeatureExtractor::runOnFunction(Function &F) {
  if (F.getName() != kernelName)
    return false;

  PDT = &getAnalysis<PostDominatorTree>();
  DT = &getAnalysis<DominatorTree>();
  MDDA = &getAnalysis<MultiDimDivAnalysis>();
  SDDA = &getAnalysis<SingleDimDivAnalysis>();
  SE = &getAnalysis<ScalarEvolution>();
  NDR = &getAnalysis<NDRange>();

  visit(F);
  collector.dump();
  return false;
}

//------------------------------------------------------------------------------
void OpenCLFeatureExtractor::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MultiDimDivAnalysis>();
  AU.addRequired<SingleDimDivAnalysis>();
  AU.addRequired<PostDominatorTree>();
  AU.addRequired<DominatorTree>();
  AU.addRequired<ScalarEvolution>();
  AU.addRequired<NDRange>();
  AU.setPreservesAll();
}

//------------------------------------------------------------------------------
// Count all instruction types.
#define HANDLE_INST(N, OPCODE, CLASS)                                          \
  void OpenCLFeatureExtractor::visit##OPCODE(CLASS &) {                        \
    collector.instTypes[#OPCODE] += 1;                                         \
    collector.instTypes["insts"] += 1;                                         \
  }
#include "llvm/IR/Instruction.def"

//------------------------------------------------------------------------------
void OpenCLFeatureExtractor::visitInstruction(Instruction &inst) {
  errs() << "Unknown instruction: " << inst;
  llvm_unreachable(0);
}

//------------------------------------------------------------------------------
void OpenCLFeatureExtractor::visitBasicBlock(BasicBlock &basicBlock) {
  BasicBlock *block = (BasicBlock *)&basicBlock;
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
void OpenCLFeatureExtractor::visitFunction(Function &function) {
  // Extract ThreadId values. 
  collector.countDimensions(function);
  collector.countBranches(function);
  collector.countEdges(function);
  collector.countDivInsts(function, MDDA, SDDA);
  collector.countArgs(function);
}
