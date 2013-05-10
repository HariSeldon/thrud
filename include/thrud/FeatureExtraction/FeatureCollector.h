#ifndef FEATURE_COLLECTOR_H
#define FEATURE_COLLECTOR_H

#include "thrud/DivergenceAnalysis/MultiDimDivAnalysis.h"

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
    //std::vector<std::pair<float, float> > blockMLP;
    std::vector<float> blockMLP;
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

    // Function calls.
    void countBarriers(const BasicBlock &block);
    void countMathFunctions(const BasicBlock &block);

    // Liveness ranges.
    std::vector<unsigned int> aliveOutBlocks;
    std::vector<float> avgLiveRange;
    void livenessAnalysis(BasicBlock &block);

    // Phis.
    // Map phi name with arg number.
    std::map<std::string, unsigned int> phiArgs; 
    // Map block with phi. 
    std::map<std::string, std::vector<std::string> > blockPhis; 
    void countPhis(const BasicBlock &block);
    // Average of arguments for a phi-node.

    // Constants.
    void countConstants(const BasicBlock &block);
    // Local memory usage.
    void countLocalMemoryUsage(const BasicBlock &block);

    //===================
    // Function counters.
    //===================  
    void countEdges(const Function &function);
    void countBranches(const Function &function);
    void countDivInsts(const Function &function, MultiDimDivAnalysis *mdda);
    void countArgs(const Function &function);

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
