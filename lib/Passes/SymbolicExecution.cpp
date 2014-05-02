#include "thrud/FeatureExtraction/SymbolicExecution.h"

#include "llvm/Analysis/ScalarEvolution.h"

#include "llvm/IR/BasicBlock.h"

#include "thrud/Support/NDRange.h"
#include "thrud/Support/NDRangeSpace.h"
#include "thrud/Support/OCLEnv.h"
#include "thrud/Support/SubscriptAnalysis.h"
#include "thrud/Support/Utils.h"
#include "llvm/Support/YAMLTraits.h"

#include <cassert>

static cl::opt<std::string>
    kernelName("symbolic-kernel-name", cl::init(""), cl::Hidden,
               cl::desc("Name of the kernel to analyze"));

char SymbolicExecution::ID = 0;
static RegisterPass<SymbolicExecution>
    X("symbolic-execution",
      "Perform symbolic execution tracing memory accesses");

using namespace llvm;

using yaml::MappingTraits;
using yaml::SequenceTraits;
using yaml::IO;
using yaml::Output;

namespace llvm {
namespace yaml {

//------------------------------------------------------------------------------
template <> struct MappingTraits<SymbolicExecution> {
  static void mapping(IO &io, SymbolicExecution &exe) {
    io.mapRequired("loadTransactions", exe.loadTransactions);
    io.mapRequired("storeTransactions", exe.storeTransactions);
    io.mapRequired("localLoadTransactions", exe.localLoadTransactions);
    io.mapRequired("localStoreTransactions", exe.localStoreTransactions);

    io.mapRequired("loopLoadTransactions", exe.loopLoadTransactions);
    io.mapRequired("loopStoreTransactions", exe.loopStoreTransactions);
    io.mapRequired("loopLocalLoadTransactions", exe.loopLocalLoadTransactions);
    io.mapRequired("loopLocalStoreTransactions", exe.loopLocalStoreTransactions);
  }
};

//------------------------------------------------------------------------------
// Sequence of ints.
template <> struct SequenceTraits<std::vector<int> > {
  static size_t size(IO &io, std::vector<int> &seq) { return seq.size(); }
  static int &element(IO &, std::vector<int> &seq, size_t index) {
    if (index >= seq.size())
      seq.resize(index + 1);
    return seq[index];
  }

  static const bool flow = true;
};

}
}

//------------------------------------------------------------------------------
bool SymbolicExecution::runOnFunction(Function &function) {
  if (function.getName() != kernelName)
    return false;

  loopInfo = &getAnalysis<LoopInfo>();
  se = &getAnalysis<ScalarEvolution>();
  ndr = &getAnalysis<NDRange>();

  // Define the NDRange space.
  // FIXME: this is going to be read from a config file.
  NDRangeSpace ndrSpace(4, 32, 1, 1024, 1024, 1);
  ocl = new OCLEnv(function, ndr, ndrSpace);

  init();

  visit(function);

  dump();

  return false;
}

//------------------------------------------------------------------------------
void SymbolicExecution::init() {
  loadTransactions.clear();
  storeTransactions.clear();
  localLoadTransactions.clear();
  localStoreTransactions.clear();

  loopLoadTransactions.clear();
  loopStoreTransactions.clear();
  loopLocalLoadTransactions.clear();
  loopLocalStoreTransactions.clear();
}

//------------------------------------------------------------------------------
void SymbolicExecution::getAnalysisUsage(AnalysisUsage &au) const {
  au.addRequired<ScalarEvolution>();
  au.addRequired<NDRange>();
  au.addRequired<LoopInfo>();
  au.setPreservesAll();
}

//------------------------------------------------------------------------------
void SymbolicExecution::visitBasicBlock(BasicBlock &basicBlock) {
  BasicBlock *block = (BasicBlock *)&basicBlock;
  if (!IsInLoop(block, loopInfo))
    memoryAccessAnalysis(basicBlock, loadTransactions, storeTransactions,
                         localLoadTransactions, localStoreTransactions);
  else
    memoryAccessAnalysis(basicBlock, loopLoadTransactions,
                         loopStoreTransactions, loopLocalLoadTransactions,
                         loopLocalStoreTransactions);
}

//------------------------------------------------------------------------------
void SymbolicExecution::memoryAccessAnalysis(
    BasicBlock &block, std::vector<int> &loadTrans, std::vector<int> &storeTrans,
    std::vector<int> &localLoadTrans, std::vector<int> &localStoreTrans) {
  SubscriptAnalysis sa(se, ocl);

  for (BasicBlock::iterator iter = block.begin(), end = block.end();
       iter != end; ++iter) {
    llvm::Instruction *inst = iter;
    // Load instruction.
    if (LoadInst *LI = dyn_cast<LoadInst>(inst)) {
      Value *pointer = LI->getOperand(0);
      if (GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(pointer)) {
        if (gep->getPointerAddressSpace() == LOCAL_AS) {
          localLoadTrans.push_back(sa.getThreadStride(pointer));
          continue;
        }
        errs() << "LOAD:\n";
        inst->dump();
        loadTrans.push_back(sa.getThreadStride(pointer));
      }
    }

    // Store instruction.
    if (StoreInst *SI = dyn_cast<StoreInst>(inst)) {
      Value *pointer = SI->getOperand(1);
      if (GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(pointer)) {
        if (gep->getPointerAddressSpace() == LOCAL_AS) {
          localStoreTrans.push_back(sa.getThreadStride(pointer));
          continue;
        }
        errs() << "STORE:\n";
        inst->dump();
        storeTrans.push_back(sa.getThreadStride(pointer));
      }
    }
  }
}

//------------------------------------------------------------------------------
void SymbolicExecution::dump() {
  Output yout(llvm::outs());
  yout << *this;
}
