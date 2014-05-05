#include "thrud/FeatureExtraction/SymbolicExecution.h"

#include "llvm/Analysis/ScalarEvolution.h"

#include "llvm/IR/Instructions.h"

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

static cl::opt<int> localSizeX("localSizeX", cl::init(128), cl::Hidden,
                               cl::desc("localSizeX for symbolic execution"));
static cl::opt<int> localSizeY("localSizeY", cl::init(1), cl::Hidden,
                               cl::desc("localSizeY for symbolic execution"));
static cl::opt<int> localSizeZ("localSizeZ", cl::init(1), cl::Hidden,
                               cl::desc("localSizeZ for symbolic execution"));

static cl::opt<int>
    numberOfGroupsX("numberOfGroupsX", cl::init(1024), cl::Hidden,
                    cl::desc("numberOfGroupsX for symbolic execution"));
static cl::opt<int>
    numberOfGroupsY("numberOfGroupsY", cl::init(1024), cl::Hidden,
                    cl::desc("numberOfGroupsY for symbolic execution"));
static cl::opt<int>
    numberOfGroupsZ("numberOfGroupsZ", cl::init(1024), cl::Hidden,
                    cl::desc("numberOfGroupsZ for symbolic execution"));

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

SymbolicExecution::SymbolicExecution()
    : FunctionPass(ID),
      ndrSpace(localSizeX, localSizeY, localSizeZ, numberOfGroupsX,
               numberOfGroupsY, numberOfGroupsZ) {
}

SymbolicExecution::~SymbolicExecution() {
  delete ocl;
}

//------------------------------------------------------------------------------
bool SymbolicExecution::runOnFunction(Function &function) {
  if (function.getName() != kernelName)
    return false;

  loopInfo = &getAnalysis<LoopInfo>();
  scalarEvolution = &getAnalysis<ScalarEvolution>();
  ndr = &getAnalysis<NDRange>();

  ocl = new OCLEnv(function, ndr, ndrSpace);
  Warp warp(0, 0, 0, 0, ndrSpace);
  subscriptAnalysis = new SubscriptAnalysis(scalarEvolution, ocl, warp);

  initBuffers();
  visit(function);
  dump();

  return false;
}

//------------------------------------------------------------------------------
void SymbolicExecution::initBuffers() {
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
void SymbolicExecution::visitMemoryInst(Value *pointer,
                                        std::vector<int> &resultVector) {
  if(const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(pointer)) {
    // Ignore operations to local memory.
    if(gep->getPointerAddressSpace() == OCLEnv::LOCAL_AS)
      return;

    resultVector.push_back(subscriptAnalysis->getTransactionNumber(pointer));
  } 
}
void SymbolicExecution::visitStoreInst(StoreInst &storeInst) {
//  errs() << "SymbolicExecution::visitStoreInst\n";
  Value *pointer = storeInst.getOperand(1);
  if (isInLoop(storeInst, loopInfo))
    visitMemoryInst(pointer, loopStoreTransactions);
  else {
    visitMemoryInst(pointer, storeTransactions);
  }
}
void SymbolicExecution::visitLoadInst(LoadInst &loadInst) {
//  errs() << "SymbolicExecution::visitLoadInst\n";
  Value *pointer = loadInst.getOperand(0);
  if (isInLoop(loadInst, loopInfo))
    visitMemoryInst(pointer, loopLoadTransactions);
  else
    visitMemoryInst(pointer, loadTransactions);
}

//------------------------------------------------------------------------------
void SymbolicExecution::dump() {
  Output yout(llvm::outs());
  yout << *this;
}
