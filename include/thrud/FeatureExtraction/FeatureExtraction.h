#define DEBUG_TYPE "instcount"

#include "FeatureCollector.h"

#include "llvm/Pass.h"
#include "llvm/InstVisitor.h"

#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/PostDominators.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include "thrud/Support/NDRange.h"

using namespace llvm;

namespace llvm { class Function; }

class SingleDimDivAnalysis;
class MultiDimDivAnalysis;

//cl::opt<std::string>
//kernelName("count-kernel-name", cl::init(""), cl::Hidden,
//  cl::desc("Name of the kernel to analyze"));

/// Collect information about the kernel function.
namespace {
class OpenCLFeatureExtractor : public FunctionPass,
                               public InstVisitor<OpenCLFeatureExtractor> {

  friend class InstVisitor<OpenCLFeatureExtractor>;

  // Visitor methods.
  void visitBasicBlock(BasicBlock &block);
  void visitFunction(Function &function);
  void visitInstruction(Instruction &inst);
#define HANDLE_INST(N, OPCODE, CLASS) void visit##OPCODE(CLASS &);
#include "llvm/IR/Instruction.def"

  // Function pass methods.
public:
  static char ID; // Pass identification, replacement for typeid
  OpenCLFeatureExtractor() : FunctionPass(ID) {}

  virtual bool runOnFunction(Function &F);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  virtual void print(raw_ostream &out, const Module *module) const {}

private:
  MultiDimDivAnalysis *MDDA;
  SingleDimDivAnalysis *SDDA;
  FeatureCollector collector;
  PostDominatorTree *PDT;
  DominatorTree *DT;
  ScalarEvolution *SE;
  NDRange *NDR;
  ValueVector TIds;
};
}
