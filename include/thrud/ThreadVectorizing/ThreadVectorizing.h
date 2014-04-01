#ifndef THREAD_VECTORIZING_H
#define THREAD_VECTORIZING_H

#include "thrud/DivergenceAnalysis/DivergenceAnalysis.h"

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/DivergentRegion.h"

#include "llvm/Pass.h"

#include "llvm/IR/IRBuilder.h"

namespace llvm {
class DominatorTree;
class Instruction;
class ScalarEvolution;
class LoopInfo;
}

class DivergenceAnalysis;

class ThreadVectorizing : public llvm::FunctionPass {
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
  virtual bool doInitialization(llvm::Module &module);
  virtual bool runOnFunction(llvm::Function &function);
  virtual bool doFinalization(llvm::Module &module);
  virtual void getAnalysisUsage(llvm::AnalysisUsage &analysis_usage) const;

  /// Perform the vectorization transformation.
  /// @param function The function to vectorize.
  bool performVectorization(llvm::Function &function);

private:
  void init();

  /// @brief Shift and widen tid values into a vector.
  void widenTids();

  /// @brief Scale and widen the given tid into a vector and add a shift,
  /// so to obtain:
  /// (VW * tid, VW * tid + 1, VW * tid + 2, VW * tid + 3)
  /// @param getTid_inst The get_tid value to be widened.
  void widenTid(llvm::Instruction *get_tid_inst);

  /// @brief Set the insertion point of the IR builder to be
  /// right after the given instruction.
  void setInsertPoint(llvm::Instruction *inst);

  /// @brief Multiply the given tid by the vectorization width.
  llvm::Instruction *multiplyTid(llvm::Instruction *tid_inst);

  /// @brief Add a vector of consetive elements (1, 2, 3, ..., width_of_inst)
  /// to the given instruction value.
  llvm::Instruction *createConsecutiveVector(llvm::Instruction *inst);

  /// @brief Widen the given value into a vector.
  llvm::Value *widenValue(llvm::Value *value);

  /// @brief Vectorize all the varying instructions in the function.
  void vectorizeFunction();

  /// @brief Transform the given instruction into the vectorized version.
  llvm::Value *vectorizeInst(llvm::Instruction *inst);

  void replicateRegion(DivergentRegion *region);

  void replicateRegionClassic(DivergentRegion *region);
  void initAliveMap(DivergentRegion *region, CoarseningMap &aliveMap);
  void replicateRegionImpl(DivergentRegion *region);
  void updateAliveMap(CoarseningMap &aliveMap, Map &regionMap);
  void updatePlaceholdersWithAlive(CoarseningMap &aliveMap);

  void replicateRegionFalseMerging(DivergentRegion *region);
  void replicateRegionTrueMerging(DivergentRegion *region);
  void replicateRegionMerging(DivergentRegion *region, unsigned int branch);
  void replicateRegionFullMerging(DivergentRegion *region);

  /// @brief Return the vector value corresponding to the given vector value.
  llvm::Value *getVectorValue(llvm::Value *scalar_value);

  /// @brief Vectorize the branch controlling a divergent loop.
  llvm::BranchInst *vectorizeLoopBranch(llvm::BranchInst *inst);

  /// @brief Vectorize the instructions guarded by the given branch
  /// instruction.
  void vectorizeShieldBranch(llvm::BranchInst *inst);

  /// @brief Create a temporary vector phi node of the vector type based
  /// on the input phi. Add the created phi to a list. The list is going to be
  /// updated at the end of vectorization setting the right vector operands.
  llvm::PHINode *vectorizePhiNode(llvm::PHINode *phiNode);

  /// @brief Set the operands of the vector phi-nodes with vector
  /// values.
  void fixPhiNodes();

  /// @brief Create a vector version of the given binary operation.
  llvm::BinaryOperator *vectorizeBinaryOperator(llvm::BinaryOperator *binOp);

  /// @brief Create a vector version of the given select instruction.
  llvm::SelectInst *vectorizeSelect(llvm::SelectInst *selectInst);

  /// @brief Create a vector version of the given cmp instruction.
  llvm::CmpInst *vectorizeCmp(llvm::CmpInst *cmpInst);

  /// @brief If possibile create a vector version of the given store.
  /// If the store is not consecutive for consecutive tid in the vectorizing
  /// dimension then generate scalar stores.
  llvm::Value *vectorizeStore(llvm::StoreInst *storeInst);

  /// @brief If possibile create a vector version of the given load.
  /// If the store is not consecutive for consecutive tid in the vectorizing
  /// dimension then generate scalar loads.
  llvm::Value *vectorizeLoad(llvm::LoadInst *loadInst);

  /// @brief Create a vector version of the given cast instruction.
  llvm::CastInst *vectorizeCast(llvm::CastInst *castInst);

  /// @brief Creat multiple scalar replicas of the given instruction. If
  /// the instruction type is not void create a vector that contains the
  /// the values of the single scalar instructions.
  llvm::Value *replicateInst(llvm::Instruction *inst);

  /// @brief Get the vector version of each operand of the given instruction.
  ValueVector getWidenedOperands(llvm::Instruction *inst);

  /// @brief Given a pointer to a scalar data type make the pointer
  /// point to a vector data type. In particular pay attention to the
  /// address space.
  llvm::Type *getVectorPointerType(llvm::Type *scalarPointer);

  /// @brief Remove from the current function all the scalar instructions
  /// which have been vectorized.
  void removeScalarInsts();

  /// @brief Remove the vector place holders inserted during the first
  /// vectorization sweep.
  void removeVectorPlaceholders();

private:
  unsigned int direction;
  unsigned int width;
  DivRegionOption divRegionOption;

  PostDominatorTree *pdt;
  DominatorTree *dt;
  SingleDimDivAnalysis *sdda;
  LoopInfo *loopInfo;
  NDRange *ndr;
  ScalarEvolution *scalarEvolution;

  /// The kernel to be vectorize.
  llvm::Function *kernelFunction;

  /// Mapping between the old scalar values and the new vector values.
  V2VMap vectorMap;
  /// IR builder.
  llvm::IRBuilder<> *irBuilder;
  /// Vectorized instructions.
  InstSet toRemoveInsts;
  /// Phi nodes to fix.
  PhiVector vectorPhis;
  /// Undef map.
  V2VMap phMap;
};

#endif
