#include "thrud/FeatureExtraction/FeatureCollector.h"

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/Graph.h"
#include "thrud/Support/MathUtils.h"
#include "thrud/Support/Utils.h"

#include "thrud/FeatureExtraction/ILPComputation.h"
#include "thrud/FeatureExtraction/MLPComputation.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Function.h"

#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"

#include "llvm/Support/CFG.h"
#include "llvm/Support/YAMLTraits.h"

#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <iterator>

using namespace llvm;

using yaml::MappingTraits;
using yaml::SequenceTraits;
using yaml::IO;
using yaml::Output;

template <>
struct MappingTraits<FeatureCollector> {
  static void mapping(IO &io, FeatureCollector &collector) {
    for (std::map<std::string, unsigned int>::iterator 
      iter = collector.instTypes.begin(), end = collector.instTypes.end(); 
      iter != end; ++iter) {
      io.mapRequired(iter->first.c_str(), iter->second);
    }
    // Instructions per block.
    io.mapRequired("instsPerBlock", collector.blockInsts);

    // Dump Phi nodes.
    std::vector<unsigned int> args;
    for (std::map<std::string, std::vector<std::string> >::iterator
      iter = collector.blockPhis.begin(), end = collector.blockPhis.end(); 
      iter != end; ++iter) {
      std::vector<std::string> phis = iter->second;
      unsigned int argSum = 0;
      for (std::vector<std::string>::iterator phiIter = phis.begin(),
           phiEnd = phis.end(); phiIter != phiEnd; ++phiIter) {
        unsigned int argNumber = collector.phiArgs[*phiIter];
        argSum += argNumber;
      }
      args.push_back(argSum);
    }
    
    io.mapRequired("phiArgs", args);

    // Dump ILP.
    io.mapRequired("ilpPerBlock", collector.blockILP);

    // Dump MLP.
    //io.mapRequired("mlpPerBlock", collector.blockMLP);
  }
};

//------------------------------------------------------------------------------
template <>
struct MappingTraits<std::pair<float, float> > {
  static void mapping(IO &io, std::pair<float, float> &avgVar) {
    io.mapRequired("avg", avgVar.first);
    io.mapRequired("var", avgVar.second);
  }
};

//------------------------------------------------------------------------------
// Sequence of unsigned ints.
template <>
struct SequenceTraits <std::vector<unsigned int> > {
  static size_t size(IO &io, std::vector<unsigned int> &seq) {
    return seq.size();
  }
  static unsigned int& element(IO &, std::vector<unsigned int> &seq, 
    size_t index) {
    if ( index >= seq.size() )
      seq.resize(index+1);
    return seq[index];
  } 

  static const bool flow = true;
};
//------------------------------------------------------------------------------
// Sequence of floats.
template <>
struct SequenceTraits <std::vector<float> > {
  static size_t size(IO &io, std::vector<float> &seq) {
    return seq.size();
  }
  static float& element(IO &, std::vector<float> &seq, 
    size_t index) {
    if ( index >= seq.size() )
      seq.resize(index+1);
    return seq[index];
  } 

  static const bool flow = true;
};

//------------------------------------------------------------------------------
// Sequence of pairs.
template <>
struct SequenceTraits <std::vector<std::pair<float, float> > > {
  static size_t size(IO &io, std::vector<std::pair<float, float> > &seq) {
    return seq.size();
  }
  static std::pair<float, float>& element(IO &, 
                                  std::vector<std::pair<float, float> > &seq,
                                  size_t index) {
    if ( index >= seq.size() )
      seq.resize(index+1);
    return seq[index];
  } 

  static const bool flow = true;
};

//------------------------------------------------------------------------------
FeatureCollector::FeatureCollector() { 
  // Instruction-specific counters.   
  #define HANDLE_INST(N, OPCODE, CLASS) \
    instTypes[#OPCODE] = 0;
  #include "llvm/IR/Instruction.def"
  
  instTypes["insts"] = 0;
  instTypes["blocks"] = 0;
}

//------------------------------------------------------------------------------
void FeatureCollector::computeILP(BasicBlock *block) {
  blockILP.push_back(getILP(block));
}

//------------------------------------------------------------------------------
void FeatureCollector::computeMLP(BasicBlock *block, DominatorTree *DT, 
     PostDominatorTree *PDT) {
  blockMLP.push_back(getMLP(block, DT, PDT));
}

//------------------------------------------------------------------------------
void FeatureCollector::countIncomingEdges(const BasicBlock &block) {
  BasicBlock *tmpBlock = (BasicBlock *)&block;
  pred_iterator first = pred_begin(tmpBlock), last = pred_end(tmpBlock);
  blockIncoming[block.getName()] = std::distance(first, last); 
}

//------------------------------------------------------------------------------
void FeatureCollector::countOutgoingEdges(const BasicBlock &block) {
  blockOutgoing[block.getName()] = block.getTerminator()->getNumSuccessors();
}

//------------------------------------------------------------------------------
void FeatureCollector::countInstsBlock(const BasicBlock &block) {
  blockInsts.push_back(
    static_cast<unsigned int>(block.getInstList().size()));
}

//------------------------------------------------------------------------------
void FeatureCollector::countEdges(const Function &function) {
  unsigned int edges = 0;
  unsigned int criticalEdges = 0;
  for (Function::const_iterator block = function.begin(), end = function.end(); 
    block != end; ++block) {
    edges += block->getTerminator()->getNumSuccessors();
  }

  for (Function::const_iterator block = function.begin(), end = function.end();
    block != end; ++block) {
    const TerminatorInst *termInst = block->getTerminator();
    unsigned int termNumber = termInst->getNumSuccessors();
    
    for (unsigned int index = 0; index < termNumber; ++index) {
      criticalEdges += isCriticalEdge(termInst, index);
    } 
  }

  instTypes["edges"] = edges;
  instTypes["criticalEdges"] = criticalEdges;
}

//------------------------------------------------------------------------------
void FeatureCollector::countBranches(const Function &function) {
  unsigned int condBranches = 0;
  unsigned int uncondBranches = 0;
  for (Function::const_iterator block = function.begin(), end = function.end(); 
    block != end; ++block) {
    const TerminatorInst *term = block->getTerminator();
    if(const BranchInst *branch = dyn_cast<BranchInst>(term)) {
      if(branch->isConditional() == true)
        ++condBranches;
      else
        ++uncondBranches;
    }
  }

  instTypes["condBranches"] = condBranches;
  instTypes["uncondBranches"] = uncondBranches;
}

//------------------------------------------------------------------------------
void FeatureCollector::countPhis(const BasicBlock &block) {
  std::vector<std::string> names;
  for (BasicBlock::const_iterator inst = block.begin(); isa<PHINode>(inst); ++inst) {
    std::string name = inst->getName(); 
    unsigned int argCount = inst->getNumOperands();

    phiArgs[name] = argCount;
    names.push_back(name);

  }

  blockPhis[block.getName()] = names;
}

//------------------------------------------------------------------------------
void FeatureCollector::countConstants(const BasicBlock &block) {
  unsigned int fourB = 0;
  unsigned int eightB = 0;
  unsigned int fps = 0;

  for (BasicBlock::const_iterator iter = block.begin(), end = block.end();
       iter != end; ++iter) {
    const Instruction *inst = iter;
    for (Instruction::const_op_iterator opIter = inst->op_begin(), 
         opEnd = inst->op_end(); opIter != opEnd; ++opIter) {
      const Value *operand = opIter->get();
      if(const ConstantInt *constInt = dyn_cast<ConstantInt>(operand)) {
        if(constInt->getBitWidth() == 32) 
          fourB++;
  
        if(constInt->getBitWidth() == 64) 
          eightB++;
      }

      if(isa<ConstantFP>(operand)) 
        fps++;
    } 
  }

  instTypes["fourB"] = fourB;
  instTypes["eightB"] = eightB;
  instTypes["fps"] = fps;
}

//------------------------------------------------------------------------------
void FeatureCollector::countBarriers(const BasicBlock &block) {
  for (BasicBlock::const_iterator iter = block.begin(), end = block.end();
    iter != end; ++iter) {
    const Instruction *inst = iter; 
    if (const CallInst *callInst = dyn_cast<CallInst>(inst)) {
      const Function *function = callInst->getCalledFunction(); 
      if (function->getName() == "barrier") {
        safeIncrement(instTypes, "barriers");
      }
    }
  }
}

//------------------------------------------------------------------------------
void FeatureCollector::countMathFunctions(const BasicBlock &block) {
  for (BasicBlock::const_iterator iter = block.begin(), end = block.end();
    iter != end; ++iter) {
    const Instruction *inst = iter;
    if (const CallInst *callInst = dyn_cast<CallInst>(inst)) {
      const Function *function = callInst->getCalledFunction();
      if (isMathName(function->getName())) {
        safeIncrement(instTypes, "mathFunctions");
      }
    }
  }
}

//------------------------------------------------------------------------------
void FeatureCollector::countLocalMemoryUsage(const BasicBlock &block) {
  for (BasicBlock::const_iterator iter = block.begin(), end = block.end();
    iter != end; ++iter) {
    const Instruction *inst = iter;
    if (const LoadInst *loadInst = dyn_cast<LoadInst>(inst)) { 
      if(loadInst->getPointerAddressSpace() == LOCAL_AS)
        safeIncrement(instTypes, "localLoads"); 
    } 
    if (const StoreInst *storeInst = dyn_cast<StoreInst>(inst)) {
      if(storeInst->getPointerAddressSpace() == LOCAL_AS) 
        safeIncrement(instTypes, "localStores"); 
    }
  }
}

//------------------------------------------------------------------------------
void FeatureCollector::dump() {
  Output yout(llvm::outs());
  yout << *this;
}
