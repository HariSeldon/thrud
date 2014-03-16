#include "thrud/DivergenceAnalysis/DependenceAnalysis.h"

#include "thrud/Support/Utils.h"

#include "llvm/Support/CFG.h"

#include <algorithm>

DependenceAnalysis::DependenceAnalysis() : FunctionPass(ID) {}

DependenceAnalysis::~DependenceAnalysis() {}

void DependenceAnalysis::getAnalysisUsage(AnalysisUsage &au) const {
  au.addRequired<PostDominatorTree>();
  au.setPreservesAll();
}

bool DependenceAnalysis::runOnFunction(Function &function) {
  pdt = &getAnalysis<PostDominatorTree>();

  findS(function);
  findLs();
  buildGraph();
  fillGraph(function);
  transitiveClosure();
  buildBackwardGraph();

  return false;
}

void DependenceAnalysis::findS(Function &function) {
  for (Function::iterator iter = function.begin(), iterEnd = function.end(); iter != iterEnd; ++iter) {
    BasicBlock *block = iter;
    for (succ_iterator succIter = succ_begin(block), succEnd = succ_end(block); succIter != succEnd; ++succIter) {
      BasicBlock *child = *succIter;
      if(!pdt->dominates(child, block)) {
        s.push_back(std::pair<BasicBlock*, BasicBlock*>(block, child));
      }
    }
  }
}

void DependenceAnalysis::findLs() {
  for (std::vector<std::pair<BasicBlock *, BasicBlock *> >::iterator
           iter = s.begin(),
           iterEnd = s.end();
       iter != iterEnd; ++iter) {
    std::pair<BasicBlock *, BasicBlock *> edge = *iter; 
    BasicBlock *block = pdt->findNearestCommonDominator(edge.first, edge.second);
    assert(block != NULL && "Ill-formatted function");
    ls.push_back(block);
  }
  assert(ls.size() == s.size() && "Mismatching S and Ls");
}

void DependenceAnalysis::buildGraph() {
  unsigned int edgeNumber = s.size(); 
  for (unsigned int index = 0; index < edgeNumber; ++index) {
    std::pair<BasicBlock*, BasicBlock *> edge = s[index];
    BasicBlock *l = ls[index]; 
    
    BasicBlock *a = edge.first;
    BasicBlock *b = edge.second;
  
    BlockVector children;
    BasicBlock *aParent = pdt->getNode(a)->getIDom()->getBlock();

    // Case 1.
    if(l == aParent) {
      BasicBlock *current = b;
      while(current != l) {
        children.push_back(current);
        current = pdt->getNode(current)->getIDom()->getBlock(); 
      }
    }

    // Case 2.
    if(l == a) {
      BasicBlock *current = b;
      while(current != aParent) {
        children.push_back(current);
        current = pdt->getNode(current)->getIDom()->getBlock();
      }
    }
    forwardGraph.insert(std::pair<BasicBlock *, BlockVector>(a, children)); 
  }
}

void DependenceAnalysis::fillGraph(Function &function) {
  for (Function::iterator iter = function.begin(), iterEnd = function.end(); iter != iterEnd; ++iter) {
    BasicBlock *block = iter;
    forwardGraph[block];
  }
}

void DependenceAnalysis::transitiveClosure() {
  for (GraphMap::iterator iter = forwardGraph.begin(), iterEnd = forwardGraph.end();
       iter != iterEnd; ++iter) {
    BlockVector &seeds = iter->second;
    // This implements a traversal of the tree starting from block.

    BlockVector worklist(seeds.begin(), seeds.end());
    BlockVector result;
    result.reserve(worklist.size());
    while (!worklist.empty()) {
      BasicBlock *current = worklist.back();
      worklist.pop_back();
      result.push_back(current);

      BlockVector &children = forwardGraph[current];

      // Add new children to the worklist.
      std::sort(children.begin(), children.end());
      std::sort(worklist.begin(), worklist.end());
      std::set_difference(worklist.begin(), worklist.end(), children.begin(),
                          children.end(), back_inserter(worklist));
    }

    // Update block vector.
    iter->second.assign(result.begin(), result.end());
  }
}

void DependenceAnalysis::buildBackwardGraph() {
  for (GraphMap::iterator iter = forwardGraph.begin(), iterEnd = forwardGraph.end();
       iter != iterEnd; ++iter) {
    BasicBlock *block = iter->first;
    backwardGraph.insert(std::pair<BasicBlock*, BlockVector>(block, BlockVector()));
    BlockVector &result = backwardGraph[block];

    for (GraphMap::iterator iter2 = forwardGraph.begin(), iterEnd2 = forwardGraph.end();
         iter2 != iterEnd2; ++iter2) {
      if(iter->first == iter2->first) continue;
      BlockVector &children = iter2->second;

      if(isPresent(block, children)) {
        result.push_back(iter2->first);
      }
    }
  }
}

// Public functions.
// -----------------------------------------------------------------------------
bool DependenceAnalysis::dependsOn(BasicBlock *first, BasicBlock *second) {
  BlockVector &blocks = backwardGraph[first];
  return isPresent(second, blocks);
}

bool DependenceAnalysis::dependsOnAny(BasicBlock *block, BlockVector &blocks) {
  bool result = false;
  for (BlockVector::iterator iter = blocks.begin(), iterEnd = blocks.end(); iter != iterEnd; ++iter) {
    result |= dependsOn(block, *iter);
  }
  return result;
}

bool DependenceAnalysis::controls(BasicBlock *first, BasicBlock *second) {
  BlockVector &blocks = forwardGraph[first];
  return isPresent(second, blocks);
}

void DependenceAnalysis::dump() {
  errs() << "Forward:\n";
  for (GraphMap::iterator iter = forwardGraph.begin(), iterEnd = forwardGraph.end();
       iter != iterEnd; ++iter) {
    BasicBlock *block = iter->first;
    errs() << block->getName() << ": ";
    BlockVector &children = iter->second;
    for (BlockVector::iterator childIter = children.begin(), childEnd = children.end();
         childIter != childEnd; ++childIter) {
      errs() << (*childIter)->getName() << " ";
    }     
    errs() << "\n";
  }
  errs() << "Backward:\n";
  for (GraphMap::iterator iter = backwardGraph.begin(), iterEnd = backwardGraph.end();
       iter != iterEnd; ++iter) {
    BasicBlock *block = iter->first;
    errs() << block->getName() << ": ";
    BlockVector &children = iter->second;
    for (BlockVector::iterator childIter = children.begin(), childEnd = children.end();
         childIter != childEnd; ++childIter) {
      errs() << (*childIter)->getName() << " ";
    }     
    errs() << "\n";
  }
}

char DependenceAnalysis::ID = 0;
static RegisterPass<DependenceAnalysis>
    X("dependence-analysis", "Control dependence analysis");
