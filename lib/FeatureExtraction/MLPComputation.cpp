#include "thrud/FeatureExtraction/MLPComputation.h"

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/MathUtils.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Function.h"

#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"

//------------------------------------------------------------------------------
// Build a vector with all the uses of the given value.
InstVector findUsers(llvm::Value *value) {
  InstVector result;
  for (Value::use_iterator use = value->use_begin(), end = value->use_end(); 
    use != end; ++use) {
    if(Instruction *inst = dyn_cast<Instruction>(*use)) {
      result.push_back(inst); 
    }  
  }

  return result;    
}

//------------------------------------------------------------------------------
// Filter the vector of uses removing all the instructions that are dominated
// by others.
InstVector filterUsers(InstVector &insts, DominatorTree *DT) {
  InstVector result;
  for (InstVector::iterator iter = insts.begin(), end = insts.end(); iter != end;
    ++iter) {
    Instruction *inst = *iter;

    bool include = true;
    for (InstVector::iterator iter2 = insts.begin(), end2 = insts.end();
      iter2 != end2; ++iter2) {
      Instruction *inst2 = *iter2;

      if(inst == inst2)
        continue;
      
      include &= !DT->dominates(inst2, inst);
    } 

    if(include) 
      result.push_back(inst);
  }
 
  return result;
}

//------------------------------------------------------------------------------
unsigned int countLoadsLowerBound(BasicBlock *block, Instruction *end) {
  unsigned int count = 0;
  for (BasicBlock::iterator iter = block->begin();; ++iter) {
    Instruction *inst = iter;
    if(inst == end)
      break;
    if(isa<LoadInst>(inst))
      count++;
  }
  return count;
}

//------------------------------------------------------------------------------
unsigned int countLoadsUpperBound(BasicBlock *block, Instruction *begin) {
  unsigned int count = 0;
  bool start = false;
  for (BasicBlock::iterator iter = block->begin(), end = block->end();
    iter != end; ++iter) {
    Instruction *inst = iter;
    if(inst == begin) {
      start = true;
      continue;
    }
    
    if(start) {
      if(isa<LoadInst>(inst))
        count++;
    }
  }
  return count;
}

//------------------------------------------------------------------------------
unsigned int countLoadsBlock(BasicBlock *block) {
  unsigned int count = 0;
  for (BasicBlock::iterator inst = block->begin(), end = block->end();
    inst != end; ++inst) {
    if(isa<LoadInst>(inst))
      count++;
  }
  return count;
}

//------------------------------------------------------------------------------
unsigned int countLoads(BlockVector blocks, BasicBlock *defBlock, 
             BasicBlock *userBlock, Instruction *def, Instruction *user) {
  unsigned int result = 0;
  for (BlockVector::iterator iter = blocks.begin(), end = blocks.end();
    iter != end; ++iter) {
    BasicBlock *block = *iter;
    if(block == defBlock) {
      result += countLoadsUpperBound(block, def);
      continue;  
    }
    if(block == userBlock) {
      result += countLoadsLowerBound(block, user);
      continue;
    }
    
    result += countLoadsBlock(block); 
  }
  return result;
}

//------------------------------------------------------------------------------
BlockVector getRegionBlocks(BasicBlock *defBlock, BasicBlock *userBlock) {
  BlockVector result;
  BlockStack stack;
  stack.push(userBlock);

  while (!stack.empty()) {
    // Pop the first block.
    BasicBlock *block = stack.top();
    result.push_back(block);
    stack.pop();

    // Don't put to the stack the defBlock predecessors.
    if(block == defBlock)
      continue;

    // Push to the stack the defBlock predecessors. 
    for (pred_iterator iter = pred_begin(block), end = pred_end(block); 
      iter != end; ++iter) {
      BasicBlock *pred = *iter;
      stack.push(pred); 
    }
  }

  return result;
}

//------------------------------------------------------------------------------
unsigned int computeDistance(Instruction *def, Instruction *user) {
  BasicBlock *defBlock = def->getParent();
  BasicBlock *userBlock = user->getParent();
  
  BlockVector blocks = getRegionBlocks(defBlock, userBlock);

  return countLoads(blocks, defBlock, userBlock, def, user);
}


//------------------------------------------------------------------------------
// MLP computation. 
// MLP: count the number of loads that fall in each load-use interval 
// (interval between a load and the first use of the loaded value).
std::pair<float, float> getMLP(BasicBlock *block, DominatorTree *DT, 
                        PostDominatorTree *PDT) {

  std::vector<unsigned int> distances;
  for (BasicBlock::iterator inst = block->begin(), end = block->end();
    inst != end; ++inst) {
    if(isa<LoadInst>(inst)) {
      InstVector users = findUsers(inst);
      users = filterUsers(users, DT);

      for (InstVector::iterator iter = users.begin(), end = users.end();
        iter != end; ++iter) {
           
        Instruction *user = *iter;
        distances.push_back(computeDistance(inst, user));
      }
    }
  }
  return std::make_pair<float, float>(getAverage(distances), 
                                      getVariance(distances)); 
}
