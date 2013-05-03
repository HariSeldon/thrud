#include "llvm/ADT/ValueMap.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

#include <set>
#include <stack>
#include <vector>

using namespace llvm;

namespace llvm {
  class Instruction;
  class BranchInst;
  class PHINode;
}

// New types. 
typedef ValueToValueMapTy Map;
typedef std::vector<Instruction*> InstVector;
typedef std::vector<const Instruction*> ConstInstVector;
typedef std::set<Instruction*> InstSet;
typedef std::set<const Instruction*> ConstInstSet;
typedef std::vector<Value*> ValueVector;
typedef std::vector<const Value*> ConstValueVector;
typedef std::vector<InstVector*> DoubleInstVector;
typedef std::vector<BranchInst*> BranchVector;
typedef std::vector<BasicBlock*> BlockVector;
typedef std::set<BasicBlock*> BlockSet;
typedef std::pair<Instruction*, Instruction*> InstPair;
typedef std::set<InstPair> InstPairs;
typedef std::set<BranchInst*> BranchSet;
typedef std::pair<int, int> IntPair; 
typedef std::vector<PHINode*> PHIVector;
typedef std::pair<BranchInst*, BasicBlock*> Border;
typedef std::vector<Border> BorderVector;
typedef std::stack<BasicBlock*> BlockStack;
