#ifndef FEATURE_COLLECTOR_H
#define FEATURE_COLLECTOR_H

// Try to remove these two.
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"

#include <map>
#include <string>
#include <vector>

namespace llvm {
  class BasicBlock;
  class Function;
}

using namespace llvm;

class FeatureCollector {
  public:
    FeatureCollector();

  public:
    void dump();

  public:
    std::map<std::string, unsigned int> instTypes;

    // Overall counters.
    // Map block with estimated ILP.
    std::map<std::string, float> blockILP;
    void computeILP(BasicBlock *blockblock);

    // Map block with estimated MLP: avg and variance.
    std::map<std::string, std::pair<float, float> > blockMLP;
    void computeMLP(BasicBlock *block, DominatorTree *DT,      
      PostDominatorTree *PDT);

    // Map block with number of incoming edges.
    std::map<std::string, unsigned int> blockIncoming;
    void computeIncomingEdges(BasicBlock &block);

    // Map block with number of outgoing edges.
    std::map<std::string, unsigned int> blockOutgoing;
    void computeOutgoingEdges(BasicBlock &block);

    // Map block with number of instructions.
    std::map<std::string, unsigned int> blockInsts;
    void computeInstsBlock(BasicBlock &block);

    // Number of instructions (of all types).
    unsigned int instsNumber; 

    // Number of basic blocks.
    unsigned int blocksNumber;

    // Number of edges in the control flow graph.
    unsigned int edgesNumber;
    // Number of critical edges in the control flow graph.
    unsigned int critialEdgesNumber;
    void countEdges(Function &function);

    // Number of conditional branches in the method.
    unsigned int condBranchesNumber;
    // Number of unconditional branches in the method.
    unsigned int uncondBranchesNumber;
    void countBranches(Function &function);

    // Phis.
    // Map phi name with arg number.
    std::map<std::string, unsigned int> phiArgs; 
    // Map block with phi. 
    std::map<std::string, std::vector<std::string> > blockPhis; 
    void countPhis(BasicBlock &block);
   
    // Average of arguments for a phi-node.
    // Number of basic blocks with no phi nodes.
    // Number of basic blocks with phi nodes in the interval [0, 3].
    // Number of basic blocks with more then 3 phi nodes.
    // Number of basic block where total number of arguments for all phi-nodes is in greater then 5.
    // Number of basic block where total number of arguments for all phi-nodes is in the interval [1, 5].

    // Constants.
    // Number of integer constant zero.
    unsigned int zerosNumber; 
    // Number of 32-bit integer constants.
    unsigned int fourBNumber;
    // Number of integer constant one.
    unsigned int onesNumber;
    // Number of 64-bit integer constants.
    unsigned int eightBNumber;
    // Number of FP constants.
    unsigned int fpNumber;
    void countConstants(BasicBlock &block);

    //// Number of unary operations in the method.
    //unsigned int
    //// Number of indirect references via pointers (“*” in C).
    //unsigned int
    //// Number of times the address of a variables is taken (“&” in C).
    //unsigned int
    //// Number of assignment instructions with the left operand an integer constant in the method.
    //unsigned int
    //// Number of binary operations with one of the operands an integer constant in the method.
    //unsigned int
};

#endif
