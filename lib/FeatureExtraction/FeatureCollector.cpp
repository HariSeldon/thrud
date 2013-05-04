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
    // Dump specific instructions.
    #define HANDLE_INST(N, OPCODE, CLASS) \
      io.mapRequired(#OPCODE, collector.instTypes[#OPCODE]);
    #include "llvm/IR/Instruction.def"

    io.mapRequired("insts", collector.insts);
    io.mapRequired("edges", collector.edges);
    io.mapRequired("criticalEdges", collector.critialEdges);
    io.mapRequired("condBranches", collector.condBranches);
    io.mapRequired("uncondBranches", collector.uncondBranches);
    io.mapRequired("zeros", collector.zeros);
    io.mapRequired("32bConstants", collector.fourB);
    io.mapRequired("ones", collector.ones);
    io.mapRequired("64bConstants", collector.eightB);
    io.mapRequired("fps", collector.fp);

    // Instructions per block.
    io.mapRequired("instsPerBlock", collector.blockInsts);
    // Dump Phi nodes.
    
    // Dump ILP.
    io.mapRequired("ilpPerBlock", collector.blockILP);
    
    // Dump MLP.
    io.mapRequired("mlpPerBlock", collector.blockMLP);
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
FeatureCollector::FeatureCollector() :
  insts(0), 
  blocks(0),
  edges(0),
  critialEdges(0),
  condBranches(0),
  uncondBranches(0),
  zeros(0),
  fourB(0),
  ones(0),
  eightB(0)
{ 
  // Instruction-specific counters.   
  #define HANDLE_INST(N, OPCODE, CLASS) \
    instTypes[#OPCODE] = 0;
  #include "llvm/IR/Instruction.def"
}

//------------------------------------------------------------------------------
void FeatureCollector::computeILP(BasicBlock *block) {
  //blockILP[block->getName()] = getILP(block);
  blockILP.push_back(getILP(block));
}

//------------------------------------------------------------------------------
void FeatureCollector::computeMLP(BasicBlock *block, DominatorTree *DT, 
     PostDominatorTree *PDT) {
  //blockMLP[block->getName()] = getMLP(block, DT, PDT);
  blockMLP.push_back(getMLP(block, DT, PDT));
}

//------------------------------------------------------------------------------
void FeatureCollector::computeIncomingEdges(BasicBlock &block) {
  BasicBlock *tmpBlock = (BasicBlock *)&block;
  pred_iterator first = pred_begin(tmpBlock), last = pred_end(tmpBlock);
  blockIncoming[block.getName()] = std::distance(first, last); 
}

//------------------------------------------------------------------------------
void FeatureCollector::computeOutgoingEdges(BasicBlock &block) {
  blockOutgoing[block.getName()] = block.getTerminator()->getNumSuccessors();
}

//------------------------------------------------------------------------------
void FeatureCollector::computeInstsBlock(BasicBlock &block) {
  blockInsts.push_back(
    static_cast<unsigned int>(block.getInstList().size()));
}

//------------------------------------------------------------------------------
void FeatureCollector::countEdges(Function &function) {
  unsigned int counter = 0;
  for (Function::iterator block = function.begin(), end = function.end(); 
    block != end; ++block) {
    counter += block->getTerminator()->getNumSuccessors();
  }
  edges = counter;
}

//------------------------------------------------------------------------------
void FeatureCollector::countBranches(Function &function) {
  condBranches = 0;
  uncondBranches = 0;
  for (Function::iterator block = function.begin(), end = function.end(); 
    block != end; ++block) {
    TerminatorInst *term = block->getTerminator();
    if(BranchInst *branch = dyn_cast<BranchInst>(term)) {
      if(branch->isConditional() == true)
        ++condBranches;
      else
        ++uncondBranches;
    }
  }
}

//------------------------------------------------------------------------------
void FeatureCollector::countPhis(BasicBlock &block) {
  std::vector<std::string> names;
  for (BasicBlock::iterator inst = block.begin(); isa<PHINode>(inst); ++inst) {
    std::string name = inst->getName(); 
    unsigned int argCount = inst->getNumOperands();

    phiArgs[name] = argCount;
    names.push_back(name);

  }

  blockPhis[block.getName()] = names;
}

//------------------------------------------------------------------------------
void FeatureCollector::countConstants(BasicBlock &block) {
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

        if(constInt->isZero())
          zeros++;

        if(constInt->isOne())
          ones++;
      }

      if(isa<ConstantFP>(operand)) 
        fp++;

    } 
  }
}

//------------------------------------------------------------------------------
void FeatureCollector::dump() {
  Output yout(llvm::outs());
  yout << *this;
}
