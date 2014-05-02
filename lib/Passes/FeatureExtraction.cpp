#include "thrud/FeatureExtraction/FeatureExtraction.h"

#include "llvm/Analysis/ScalarEvolution.h"

#include "thrud/DivergenceAnalysis/DivergenceAnalysis.h"
#include "thrud/Support/NDRange.h"
#include "thrud/Support/Utils.h"

cl::opt<std::string> kernelName("count-kernel-name", cl::init(""), cl::Hidden,
                                cl::desc("Name of the kernel to analyze"));

extern cl::opt<unsigned int> CoarseningDirectionCL;

char OpenCLFeatureExtractor::ID = 0;
static RegisterPass<OpenCLFeatureExtractor> X("opencl-instcount",
                                              "Collect opencl features");

//------------------------------------------------------------------------------
bool OpenCLFeatureExtractor::runOnFunction(Function &function) {
  if (function.getName() != kernelName)
    return false;

  pdt = &getAnalysis<PostDominatorTree>();
  dt = &getAnalysis<DominatorTree>();
  mdda = &getAnalysis<MultiDimDivAnalysis>();
  sdda = &getAnalysis<SingleDimDivAnalysis>();
  se = &getAnalysis<ScalarEvolution>();
  ndr = &getAnalysis<NDRange>();
 
  NDRangeSpace ndrSpace(4, 32, 1, 1024, 1024, 1);  
  ocl = new OCLEnv(function, ndr, ndrSpace);

  visit(function);
  collector.dump();
  return false;
}

//------------------------------------------------------------------------------
void OpenCLFeatureExtractor::getAnalysisUsage(AnalysisUsage &au) const {
  au.addRequired<MultiDimDivAnalysis>();
  au.addRequired<SingleDimDivAnalysis>();
  au.addRequired<PostDominatorTree>();
  au.addRequired<DominatorTree>();
  au.addRequired<ScalarEvolution>();
  au.addRequired<NDRange>();
  au.setPreservesAll();
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
  collector.computeMLP(block, dt, pdt);
  collector.countInstsBlock(basicBlock);
  collector.countConstants(basicBlock);
  collector.countBarriers(basicBlock);
  collector.countMathFunctions(basicBlock);
  collector.countOutgoingEdges(basicBlock);
  collector.countIncomingEdges(basicBlock);
  collector.countLocalMemoryUsage(basicBlock);
  collector.countPhis(basicBlock);
  collector.livenessAnalysis(basicBlock);
  //collector.coalescingAnalysis(basicBlock, se, ocl, CoarseningDirectionCL);
}

//------------------------------------------------------------------------------
void OpenCLFeatureExtractor::visitFunction(Function &function) {
  // Extract ThreadId values. 
  collector.countDimensions(ndr);
  collector.countBranches(function);
  collector.countEdges(function);
  collector.countDivInsts(function, mdda, sdda);
  collector.countArgs(function);
}
