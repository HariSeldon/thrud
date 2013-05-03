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

#include <algorithm>
#include <functional>
#include <iostream>
#include <iterator>

using namespace llvm;

//------------------------------------------------------------------------------
FeatureCollector::FeatureCollector() :
  instsNumber(0), 
  blocksNumber(0),
  edgesNumber(0),
  critialEdgesNumber(0),
  condBranchesNumber(0),
  uncondBranchesNumber(0),
  zerosNumber(0),
  fourBNumber(0),
  onesNumber(0),
  eightBNumber(0)
{ 
  // Instruction-specific counters.   
  #define HANDLE_INST(N, OPCODE, CLASS) \
    instTypes[#OPCODE] = 0;
  #include "llvm/IR/Instruction.def"
}

//------------------------------------------------------------------------------
void FeatureCollector::computeILP(BasicBlock *block) {
  blockILP[block->getName()] = getILP(block);
}

//------------------------------------------------------------------------------
void FeatureCollector::computeMLP(BasicBlock *block, DominatorTree *DT, 
     PostDominatorTree *PDT) {
  blockMLP[block->getName()] = getMLP(block, DT, PDT);
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
  blockInsts[block.getName()] = 
    static_cast<unsigned int>(block.getInstList().size());
}

//------------------------------------------------------------------------------
void FeatureCollector::countEdges(Function &function) {
  unsigned int counter = 0;
  for (Function::iterator block = function.begin(), end = function.end(); 
    block != end; ++block) {
    counter += block->getTerminator()->getNumSuccessors();
  }
  edgesNumber = counter;
}

//------------------------------------------------------------------------------
void FeatureCollector::countBranches(Function &function) {
  condBranchesNumber = 0;
  uncondBranchesNumber = 0;
  for (Function::iterator block = function.begin(), end = function.end(); 
    block != end; ++block) {
    TerminatorInst *term = block->getTerminator();
    if(BranchInst *branch = dyn_cast<BranchInst>(term)) {
      if(branch->isConditional() == true)
        ++condBranchesNumber;
      else
        ++uncondBranchesNumber;
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
          fourBNumber++;
  
        if(constInt->getBitWidth() == 64) 
          eightBNumber++;

        if(constInt->isZero())
          zerosNumber++;

        if(constInt->isOne())
          onesNumber++;
      }

      if(isa<ConstantFP>(operand)) 
        fpNumber++;

    } 
  }
}

//------------------------------------------------------------------------------
void FeatureCollector::dump() {
  // Dump ILP result.
  std::cout << "ILP:\n";
  for (std::map<std::string, float>::iterator iter = blockILP.begin(), 
    end = blockILP.end(); iter != end; ++iter) {
    std::cout << iter->first << ": " << iter->second << "\n";
  }
  std::cout << "\n";
  // Dump MLP result.

  std::cout << "MLP:\n";
  for (std::map<std::string, std::pair<float, float> >::iterator iter = blockMLP.begin(),
    end = blockMLP.end(); iter != end; ++iter) {
    std::cout << iter->first << ": " << iter->second.first << "\n";
  }
  std::cout << "\n";
  
  std::cout << "Inst Types:\n";

  for (std::map<std::string, unsigned int>::iterator iter = instTypes.begin(),
       end = instTypes.end(); iter != end; ++iter) {
    std::cout << iter->first << ": " << iter->second << "\n";
  }
}
