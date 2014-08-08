#ifndef THREAD_VECTORIZING_H
#define THREAD_VECTORIZING_H

#include "thrud/DivergenceAnalysis/DivergenceAnalysis.h"

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/DivergentRegion.h"
#include "thrud/Support/NDRangeSpace.h"

#include "llvm/Pass.h"

#include "llvm/IR/IRBuilder.h"

namespace llvm {
class DominatorTree;
class Instruction;
class ScalarEvolution;
class LoopInfo;
}

class DivergenceAnalysis;
class OCLEnv;
class SubscriptAnalysis;

class ThreadVectorizing : public FunctionPass {
public:
  enum DivRegionOption { 
    FullReplication, 
    TrueBranchMerging, 
    FalseBranchMerging, 
    FullMerging 
  };

public:
  static char ID;

  ThreadVectorizing();

  virtual const char *getPassName() const;
  virtual bool doInitialization(Module &module);
  virtual bool runOnFunction(Function &function);
  virtual bool doFinalization(Module &module);
  virtual void getAnalysisUsage(AnalysisUsage &analysis_usage) const;

  // Perform the vectorization transformation.
  // @param function The function to vectorize.
  bool performVectorization(Function &function);

private:
  void init();

  // Shift and widen tid values into a vector.
  void widenTids();

  // Scale and widen the given tid into a vector and add a shift,
  // so to obtain:
  // (VW * tid, VW * tid + 1, VW * tid + 2, VW * tid + 3)
  // @param getTid_inst The get_tid value to be widened.
  void widenTid(Instruction *get_tid_inst);

  // Set the insertion point of the IR builder to be
  // right after the given instruction.
  void setInsertPoint(Instruction *inst);

  // Multiply the given tid by the vectorization width.
  Instruction *multiplyTid(Instruction *tid_inst);

  // Add a vector of consetive elements (1, 2, 3, ..., width_of_inst)
  // to the given instruction value.
  Instruction *createConsecutiveVector(Instruction *inst);

  // Widen the given value into a vector.
  Value *widenValue(Value *value);

  // Vectorize all the varying instructions in the function.
  void vectorizeFunction();

  // Transform the given instruction into the vectorized version.
  Value *vectorizeInst(Instruction *inst);

  void replicateRegion(DivergentRegion *region);

  void replicateRegionClassic(DivergentRegion *region);
  void initAliveMap(DivergentRegion *region, CoarseningMap &aliveMap);
  void replicateRegionImpl(DivergentRegion *region);
  void updateAliveMap(CoarseningMap &aliveMap, Map &regionMap);
  void updatePlaceholdersWithAlive(CoarseningMap &aliveMap);
  void createAliveVectors(BasicBlock *block, CoarseningMap &aliveMap);

  void replicateRegionFalseMerging(DivergentRegion *region);
  void replicateRegionTrueMerging(DivergentRegion *region);
  void replicateRegionMerging(DivergentRegion *region, unsigned int branch);
  void replicateRegionFullMerging(DivergentRegion *region);

  void removeOldRegion(DivergentRegion *region);
  void applyVectorMapToRegion(DivergentRegion &region, InstVector &incoming,
                              unsigned int index);

  // Return the vector value corresponding to the given vector value.
  Value *getVectorValue(Value *scalar_value);

  // Vectorize the branch controlling a divergent loop.
  BranchInst *vectorizeLoopBranch(BranchInst *inst);

  // Vectorize the instructions guarded by the given branch
  // instruction.
  void vectorizeShieldBranch(BranchInst *inst);

  // Create a temporary vector phi node of the vector type based
  // on the input phi. Add the created phi to a list. The list is going to be
  // updated at the end of vectorization setting the right vector operands.
  PHINode *vectorizePhiNode(PHINode *phiNode);

  // Set the operands of the vector phi-nodes with vector
  // values.
  void fixPhiNodes();

  // Create a vector version of the given binary operation.
  BinaryOperator *vectorizeBinaryOperator(BinaryOperator *binOp);

  // Create a vector version of the given select instruction.
  SelectInst *vectorizeSelect(SelectInst *selectInst);

  // Create a vector version of the given cmp instruction.
  CmpInst *vectorizeCmp(CmpInst *cmpInst);

  // If possibile create a vector version of the given store.
  // If the store is not consecutive for consecutive tid in the vectorizing
  // dimension then generate scalar stores.
  Value *vectorizeStore(StoreInst *storeInst);

  // If possibile create a vector version of the given load.
  // If the store is not consecutive for consecutive tid in the vectorizing
  // dimension then generate scalar loads.
  Value *vectorizeLoad(LoadInst *loadInst);

  // Create a vector version of the given cast instruction.
  CastInst *vectorizeCast(CastInst *castInst);

  // Create a vector version of the given function call.
  // Works only for intrincs functions!
  CallInst *vectorizeCall(CallInst *callInst);

  // Creat multiple scalar replicas of the given instruction. If
  // the instruction type is not void create a vector that contains the
  // the values of the single scalar instructions.
  Value *replicateInst(Instruction *inst);

  // Given a pointer to a scalar data type make the pointer
  // point to a vector data type. In particular pay attention to the
  // address space.
  Type *getVectorPointerType(Type *scalarPointer);

  // Remove from the current function all the scalar instructions
  // which have been vectorized.
  void removeScalarInsts();

  // Remove the vector place holders inserted during the first
  // vectorization sweep.
  void removeVectorPlaceholders();

  // Get the vector version of each operand of the given instruction.
  ValueVector getWidenedOperands(Instruction *inst);

  // Get the vector version of each operand of the function call.
  ValueVector getWidenedCallOperands(CallInst *callInst);

private:
  unsigned int direction;
  unsigned int width;
  DivRegionOption divRegionOption;

  PostDominatorTree *pdt;
  DominatorTree *dt;
  SingleDimDivAnalysis *sdda;
  LoopInfo *loopInfo;
  NDRange *ndr;
  NDRangeSpace ndrSpace;
  ScalarEvolution *scalarEvolution;
  OCLEnv *ocl;
  SubscriptAnalysis *subscriptAnalysis;

  // The kernel to be vectorize.
  Function *kernelFunction;

  // Mapping between the old scalar values and the new vector values.
  V2VMap vectorMap;
  // IR builder.
  IRBuilder<> *irBuilder;
  // Vectorized instructions.
  InstSet toRemoveInsts;
  // Phi nodes to fix.
  PhiVector vectorPhis;
  // Undef map.
  V2VMap phMap;
};

#endif
