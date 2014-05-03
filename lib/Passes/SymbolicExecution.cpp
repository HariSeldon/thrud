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
    io.mapRequired("load_transactions", exe.loadTransactions);
    io.mapRequired("store_transactions", exe.storeTransactions);

    io.mapRequired("loop_load_transactions", exe.loopLoadTransactions);
    io.mapRequired("loop_store_transactions", exe.loopStoreTransactions);
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
  NDRangeSpace ndrSpace(512, 1, 1, 1024, 1024, 1);
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

  loopLoadTransactions.clear();
  loopStoreTransactions.clear();
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
  if (isInLoop(block, loopInfo))
    memoryAccessAnalysis(basicBlock, loopLoadTransactions,
                         loopStoreTransactions);
  else
    memoryAccessAnalysis(basicBlock, loadTransactions, storeTransactions);
}

//------------------------------------------------------------------------------
void SymbolicExecution::memoryAccessAnalysis(BasicBlock &block,
                                             std::vector<int> &loadTrans,
                                             std::vector<int> &storeTrans) {
  SubscriptAnalysis sa(se, ocl);

  for (BasicBlock::iterator iter = block.begin(), end = block.end();
       iter != end; ++iter) {
    llvm::Instruction *inst = iter;
    // Load instruction.
    if (LoadInst *LI = dyn_cast<LoadInst>(inst)) {
      Value *pointer = LI->getOperand(0);
      if (isa<GetElementPtrInst>(pointer)) {
        //if (gep->getPointerAddressSpace() == LOCAL_AS) {
        //  localLoadTrans.push_back(sa.getThreadStride(pointer));
        //  continue;
        //}
        errs() << "LOAD:\n";
        inst->dump();
        loadTrans.push_back(sa.getThreadStride(pointer));
      }
    }

    // Store instruction.
    if (StoreInst *SI = dyn_cast<StoreInst>(inst)) {
      Value *pointer = SI->getOperand(1);
      if (isa<GetElementPtrInst>(pointer)) {
        //if (gep->getPointerAddressSpace() == LOCAL_AS) {
        //  localStoreTrans.push_back(sa.getThreadStride(pointer));
        //  continue;
        //}
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
