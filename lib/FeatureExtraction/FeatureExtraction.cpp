#include "thrud/FeatureExtraction/FeatureExtraction.h"

char OpenCLFeatureExtractor::ID = 0;
static RegisterPass<OpenCLFeatureExtractor> X(
       "opencl-instcount", "Collect opencl features");

bool OpenCLFeatureExtractor::runOnFunction(Function &F) {
  if(F.getName() != kernelName)
    return false;

  PDT = &getAnalysis<PostDominatorTree>();
  DT = &getAnalysis<DominatorTree>();

  visit(F);
  collector.dump();
  return false;
}

// Count all instruction types.
#define HANDLE_INST(N, OPCODE, CLASS)                     \
    void OpenCLFeatureExtractor::visit##OPCODE(CLASS &) { \
      collector.instTypes[#OPCODE] += 1;                  \
    }
#include "llvm/IR/Instruction.def"

void OpenCLFeatureExtractor::visitInstruction(Instruction &inst) {
  errs() << "Unknown instruction: " << inst;
  llvm_unreachable(0);
}

void OpenCLFeatureExtractor::visitBasicBlock(BasicBlock &basicBlock) { 
  BasicBlock *block = (BasicBlock *) &basicBlock;
  collector.computeILP(block);
  collector.computeMLP(block, DT, PDT);
}

void OpenCLFeatureExtractor::visitFunction(Function &function) { 
}
