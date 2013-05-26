#include "thrud/FeatureExtraction/MLPComputation.h"

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/MathUtils.h"
#include "thrud/Support/Utils.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Function.h"

#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"

#include <algorithm>

//------------------------------------------------------------------------------
// Filter the vector of uses removing all the instructions that are dominated
// by others.
//InstVector filterUsers(InstVector &insts, DominatorTree *DT) {
//  InstVector result;
//  for (InstVector::iterator iter = insts.begin(), end = insts.end(); 
//    iter != end; ++iter) {
//    Instruction *inst = *iter;
//
//    bool include = true;
//    for (InstVector::iterator iter2 = insts.begin(), end2 = insts.end();
//      iter2 != end2; ++iter2) {
//      Instruction *inst2 = *iter2;
//
//      if(inst == inst2)
//        continue;
//      
//      include &= !DT->dominates(inst2, inst);
//    } 
//
//    if(include) 
//      result.push_back(inst);
//  }
// 
//  return result;
//}

//------------------------------------------------------------------------------
InstVector filterUsers(InstVector &insts, BasicBlock *block) {
  InstVector result;
  for (InstVector::iterator iter = insts.begin(), end = insts.end(); 
    iter != end; ++iter) {
    Instruction *inst = *iter;

    if(inst->getParent() == block)
      result.push_back(inst);
  }
 
  return result;
}

//------------------------------------------------------------------------------
bool isLoad(const llvm::Instruction &inst) {
  return isa<LoadInst>(inst);
}

//------------------------------------------------------------------------------
unsigned int countLoadsBounded(BasicBlock *block, Instruction *def, 
  Instruction *user) {
  BasicBlock::iterator iter(def), end(user);
  ++iter;
  return std::count_if(iter, end, isLoad); 
}

//------------------------------------------------------------------------------
unsigned int countLoads(BlockVector blocks, BasicBlock *defBlock, 
                        BasicBlock *userBlock, Instruction *def, 
                        Instruction *user) {

  if(defBlock == userBlock) {
    return countLoadsBounded(defBlock, def, user);
  }

  unsigned int result = 0;
  for (BlockVector::iterator iter = blocks.begin(), end = blocks.end();
    iter != end; ++iter) {
    BasicBlock *block = *iter;
    if(block == defBlock) {
      result += countLoadsBounded(block, def, block->end());
      continue;  
    }
    if(block == userBlock) {
      result += countLoadsBounded(block, block->begin(), user);
      continue;
    }
    
    result += countLoadsBounded(block, block->begin(), block->end()); 
  }
  return result;
}

//------------------------------------------------------------------------------
// FIXME: this might not work with loops.
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
  
  // Manage the special case in which the user is a phi-node.
  if(PHINode *phi = dyn_cast<PHINode>(user)) {
    for (unsigned int index = 0; index < phi->getNumIncomingValues(); ++index) {
      if(def == phi->getIncomingValue(index)) {
        userBlock = phi->getIncomingBlock(index);
        BlockVector blocks = getRegionBlocks(defBlock, userBlock);
        return countLoads(blocks, defBlock, userBlock, def, userBlock->end());
      }
    }   
  } 

  BlockVector blocks = getRegionBlocks(defBlock, userBlock);
  return countLoads(blocks, defBlock, userBlock, def, user);
}

//------------------------------------------------------------------------------
// MLP computation. 
// MLP: count the number of loads that fall in each load-use interval 
// (interval between a load and the first use of the loaded value).
float getMLP(BasicBlock *block, DominatorTree *DT, PostDominatorTree *PDT) {
  std::vector<unsigned int> distances;
  for (BasicBlock::iterator inst = block->begin(), end = block->end();
    inst != end; ++inst) {
    if(isa<LoadInst>(inst)) {
      InstVector users = findUsers(inst);
      users = filterUsers(users, block);

      for (InstVector::iterator iter = users.begin(), end = users.end();
        iter != end; ++iter) {
           
        Instruction *user = *iter;
        unsigned int distance = computeDistance(inst, user);
        distances.push_back(distance);
      }
    }
  }
  
  return getAverage(distances);
}
