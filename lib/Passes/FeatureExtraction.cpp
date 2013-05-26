#include "thrud/FeatureExtraction/FeatureExtraction.h"

#include "thrud/DivergenceAnalysis/MultiDimDivAnalysis.h"

//cl::opt<std::string>
//kernelName("count-kernel-name", cl::init(""), cl::Hidden,
//               cl::desc("Name of the kernel to analyze"));

extern cl::opt<std::string> kernelName;

char OpenCLFeatureExtractor::ID = 0;
static RegisterPass<OpenCLFeatureExtractor> X(
       "opencl-instcount", "Collect opencl features");

bool OpenCLFeatureExtractor::runOnFunction(Function &F) {
  if(F.getName() != kernelName)
    return false;

  PDT = &getAnalysis<PostDominatorTree>();
  DT = &getAnalysis<DominatorTree>();
  MDDA = &getAnalysis<MultiDimDivAnalysis>();

  visit(F);
  collector.dump();
  return false;
}

void OpenCLFeatureExtractor::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MultiDimDivAnalysis>();
  AU.addRequired<PostDominatorTree>();
  AU.addRequired<DominatorTree>();
  AU.setPreservesAll();
}

// Count all instruction types.
#define HANDLE_INST(N, OPCODE, CLASS)                     \
    void OpenCLFeatureExtractor::visit##OPCODE(CLASS &) { \
      collector.instTypes[#OPCODE] += 1;                  \
      collector.instTypes["insts"] += 1;                  \
    }
#include "llvm/IR/Instruction.def"

void OpenCLFeatureExtractor::visitInstruction(Instruction &inst) {
  errs() << "Unknown instruction: " << inst;
  llvm_unreachable(0);
}

void OpenCLFeatureExtractor::visitBasicBlock(BasicBlock &basicBlock) { 
  BasicBlock *block = (BasicBlock *) &basicBlock;
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
}

void OpenCLFeatureExtractor::visitFunction(Function &function) { 
  collector.countBranches(function);
  collector.countEdges(function);
  collector.countDivInsts(function, MDDA);
  collector.countArgs(function);
}
