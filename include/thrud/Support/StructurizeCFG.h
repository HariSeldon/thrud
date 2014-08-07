// Transforms the control flow graph on one single entry/exit region
// at a time.
//
// After the transform all "If"/"Then"/"Else" style control flow looks like
// this:
//
// \verbatim
// 1
// ||
// | |
// 2 |
// | /
// |/   
// 3
// ||   Where:
// | |  1 = "If" block, calculates the condition
// 4 |  2 = "Then" subregion, runs if the condition is true
// | /  3 = "Flow" blocks, newly inserted flow blocks, rejoins the flow
// |/   4 = "Else" optional subregion, runs if the condition is false
// 5    5 = "End" block, also rejoins the control flow
// \endverbatim
//
// Control flow is expressed as a branch where the true exit goes into the
// "Then"/"Else" region, while the false exit skips the region
// The condition for the optional "Else" region is expressed as a PHI node.
// The incomming values of the PHI node are true for the "If" edge and false
// for the "Then" edge.
//
// Additionally to that even complicated loops look like this:
//
// \verbatim
// 1
// ||
// | |
// 2 ^  Where:
// | /  1 = "Entry" block
// |/   2 = "Loop" optional subregion, with all exits at "Flow" block
// 3    3 = "Flow" block, with back edge to entry block
// |
// \endverbatim
//
// The back edge of the "Flow" block is always on the false side of the branch
// while the true side continues the general flow. So the loop condition
// consist of a network of PHI nodes where the true incoming values expresses
// breaks and the false values expresses continue states.

#include "llvm/Pass.h"

#include "llvm/ADT/MapVector.h"

#include "llvm/Analysis/RegionPass.h"

#include "llvm/IR/Instructions.h"

using namespace llvm;

// Definition of the complex types used in this pass.

typedef std::pair<BasicBlock *, Value *> BBValuePair;

typedef SmallVector<RegionNode*, 8> RNVector;
typedef SmallVector<BasicBlock*, 8> BBVector;
typedef SmallVector<BranchInst*, 8> BranchVector;
typedef SmallVector<BBValuePair, 2> BBValueVector;

typedef SmallPtrSet<BasicBlock *, 8> BBSet;

typedef MapVector<PHINode *, BBValueVector> PhiMap;
typedef MapVector<BasicBlock *, BBVector> BB2BBVecMap;

typedef DenseMap<DomTreeNode *, unsigned> DTN2UnsignedMap;
typedef DenseMap<BasicBlock *, PhiMap> BBPhiMap;
typedef DenseMap<BasicBlock *, Value *> BBPredicates;
typedef DenseMap<BasicBlock *, BBPredicates> PredMap;
typedef DenseMap<BasicBlock *, BasicBlock*> BB2BBMap;

class StructurizeCFG : public RegionPass {
public:
  static char ID;
  StructurizeCFG();

public:
  virtual bool runOnRegion(Region *R, RGPassManager &RGM);
  virtual void getAnalysisUsage(AnalysisUsage &au) const;
  using Pass::doInitialization;
  virtual bool doInitialization(Region *R, RGPassManager &RGM);

  virtual const char *getPassName() const {
    return "Structurize CFG";
  }

private:
  void orderNodes();
  void analyzeLoops(RegionNode *N);
  Value *invert(Value *Condition);
  Value *buildCondition(BranchInst *Term, unsigned Idx, bool Invert);
  void gatherPredicates(RegionNode *N);
  void collectInfos();
  void insertConditions(bool Loops);
  void delPhiValues(BasicBlock *From, BasicBlock *To);
  void addPhiValues(BasicBlock *From, BasicBlock *To);
  void setPhiValues();
  void killTerminator(BasicBlock *BB);
  void changeExit(RegionNode *Node, BasicBlock *NewExit,
                  bool IncludeDominator);
  BasicBlock *getNextFlow(BasicBlock *Dominator);
  BasicBlock *needPrefix(bool NeedEmpty);
  BasicBlock *needPostfix(BasicBlock *Flow, bool ExitUseAllowed);
  void setPrevNode(BasicBlock *BB);
  bool dominatesPredicates(BasicBlock *BB, RegionNode *Node);
  bool isPredictableTrue(RegionNode *Node);
  void wireFlow(bool ExitUseAllowed, BasicBlock *LoopEnd);
  void handleLoops(bool ExitUseAllowed, BasicBlock *LoopEnd);
  void createFlow();
  void rebuildSSA();

private:
  Type *Boolean;
  ConstantInt *BoolTrue;
  ConstantInt *BoolFalse;
  UndefValue *BoolUndef;

  Function *Func;
  Region *ParentRegion;

  DominatorTree *DT;

  RNVector Order;
  BBSet Visited;

  BBPhiMap DeletedPhis;
  BB2BBVecMap AddedPhis;

  PredMap Predicates;
  BranchVector Conditions;

  BB2BBMap Loops;
  PredMap LoopPreds;
  BranchVector LoopConds;

  RegionNode *PrevNode;
};
