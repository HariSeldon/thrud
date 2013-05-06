#ifndef FEATURE_COLLECTOR_H
#define FEATURE_COLLECTOR_H

// Try to remove these two.
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"

#include "llvm/Support/YAMLTraits.h"

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
    //std::map<std::string, float> blockILP;
    std::vector<float> blockILP;
    void computeILP(BasicBlock *blockblock);

    // Map block with estimated MLP: avg and variance.
    //std::map<std::string, std::pair<float, float> > blockMLP;
    std::vector<std::pair<float, float> > blockMLP;
    void computeMLP(BasicBlock *block, DominatorTree *DT,      
      PostDominatorTree *PDT);

    // Map block with number of incoming edges.
    std::map<std::string, unsigned int> blockIncoming;
    void countIncomingEdges(const BasicBlock &block);

    // Map block with number of outgoing edges.
    std::map<std::string, unsigned int> blockOutgoing;
    void countOutgoingEdges(const BasicBlock &block);

    // Number of instructions per block.
    //std::map<std::string, unsigned int> blockInsts;
    std::vector<unsigned int> blockInsts;
    void countInstsBlock(const BasicBlock &block);

    void countEdges(const Function &function);
    void countBranches(const Function &function);

    // Function calls.
    void countBarriers(const BasicBlock &block);
    void countMathFunctions(const BasicBlock &block);

    // Phis.
    // Map phi name with arg number.
    std::map<std::string, unsigned int> phiArgs; 
    // Map block with phi. 
    std::map<std::string, std::vector<std::string> > blockPhis; 
    void countPhis(const BasicBlock &block);
   
    // Average of arguments for a phi-node.
    // Number of basic blocks with no phi nodes.
    // Number of basic blocks with phi nodes in the interval [0, 3].
    // Number of basic blocks with more then 3 phi nodes.
    // Number of basic block where total number of arguments for all phi-nodes 
    // is in greater then 5.
    // Number of basic block where total number of arguments for all phi-nodes 
    // is in the interval [1, 5].

    // Constants.
    void countConstants(const BasicBlock &block);
    // Local memory usage.
    void countLocalMemoryUsage(const BasicBlock &block);

    //// Number of unary operations in the method.
    //unsigned int
    //// Number of indirect references via pointers (“*” in C).
    //unsigned int
    //// Number of times the address of a variables is taken (“&” in C).
    //unsigned int
    //// Number of assignment instructions with the left operand an integer 
    //// constant in the method.
    //unsigned int
    //// Number of binary operations with one of the operands an integer 
    //// constant in the method.
    //unsigned int
};
 
#endif
