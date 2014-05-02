#ifndef SYMBOLIC_EXECUTION_H
#define SYMBOLIC_EXECUTION_H

#include "llvm/Pass.h"
#include "llvm/InstVisitor.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/ScalarEvolution.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace llvm { class Function; }

class MultiDimDivAnalysis;
class NDRange;
class OCLEnv;

/// Collect information about the kernel function.
namespace {
class SymbolicExecution : public FunctionPass,
                               public InstVisitor<SymbolicExecution> {

  friend class InstVisitor<SymbolicExecution>;

  void visitBasicBlock(BasicBlock &block);

public:
  static char ID; 
  SymbolicExecution() : FunctionPass(ID) {}

  virtual bool runOnFunction(Function &F);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;

public:
  std::vector<int> loadTransactions;
  std::vector<int> storeTransactions;
  std::vector<int> localLoadTransactions;
  std::vector<int> localStoreTransactions;

  std::vector<int> loopLoadTransactions;
  std::vector<int> loopStoreTransactions;
  std::vector<int> loopLocalLoadTransactions;
  std::vector<int> loopLocalStoreTransactions;

private:
  void memoryAccessAnalysis(BasicBlock &block, std::vector<int> &loadTrans,
                            std::vector<int> &storeTrans,
                            std::vector<int> &localLoadTrans,
                            std::vector<int> &localStoreTrans);
  void init();
  void dump();

private:
  ScalarEvolution *se;
  OCLEnv *ocl;
  NDRange *ndr;
  LoopInfo *loopInfo;

};
}

#endif
