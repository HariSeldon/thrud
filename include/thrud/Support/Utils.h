#ifndef UTILS_H
#define UTILS_H

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/DivergentRegion.h"
#include "thrud/Support/RegionBounds.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Argument.h"
#include "llvm/ADT/ValueMap.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

#include <deque>
#include <set>
#include <vector>

using namespace llvm;

#define LOCAL_AS 3

extern const char *BARRIER;

// Loop management.
bool IsInLoop(const Instruction *inst, LoopInfo *loopInfo);
bool IsInLoop(const BasicBlock *block, LoopInfo *loopInfo);

// OpenCL management.
bool IsKernel(const Function *F);

void safeIncrement(std::map<std::string, unsigned int> &inputMap,
                   std::string key);

// Map management.
// Apply the given map to the given instruction.
void applyMap(Instruction *Inst, Map &map);
void applyMap(BasicBlock *block, Map &map);
void applyMapToPHIs(BasicBlock *block, Map &map);
void applyMapToPhiBlocks(PHINode *Phi, Map &map);
void applyMap(Instruction *Inst, CoarseningMap &map, unsigned int CF);
void applyMap(InstVector &insts, Map &map, InstVector &result);

void renameValueWithFactor(Value *value, StringRef oldName, unsigned int index);

// Prints to stderr the given map. For debug only.
void dump(const Map &map);
void dumpV2V(const V2VMap &map);

// Replate all the usages of O with N.
void replaceUses(Value *O, Value *N);
void BuildExitingPhiMap(BasicBlock *OldExiting, BasicBlock *NewExiting,
                        Map &map);
void remapBlocksInPHIs(BasicBlock *Target, BasicBlock *OldBlock,
                       BasicBlock *NewBlock);
void InitializeMap(Map &map, const InstVector &TIds, const InstVector &NewTIds,
                   unsigned int CI, unsigned int CF);

// Instruction management.
BranchVector FindBranches(Function &F);
template <class InstructionType>
std::vector<InstructionType *> getInsts(Function &F);
unsigned int GetOperandPosition(User *U, Value *value);
void getPHIs(BasicBlock *block, PhiVector &Phis);

// Function and module management.
ValueVector GetPointerArgs(Function *F);
InstVector FindArgDepInst(Function *F);
bool IsUsedInFunction(const Function *F, const GlobalVariable *GV);

// Verify if the given value is a constant integer and equals the given number.
bool IsByPointer(const Argument *A);

// Container management.
// Check if the given element is present in the given container.
template <class T>
bool isPresent(const T *value, const std::vector<T *> &vector);
template <class T>
bool isPresent(const T *value, const std::vector<const T *> &vector);
template <class T> bool isPresent(const T *value, const std::set<T *> &vector);
template <class T>
bool isPresent(const T *value, const std::set<const T *> &vector);
template <class T>
bool isPresent(const T *value, const std::deque<T*> &deque);

// TODO: remove these and replace with std!!!
InstVector intersection(const InstVector &A, const InstVector &B);

bool isPresent(const Instruction *inst, const BlockVector &value);
bool isPresent(const Instruction *inst, std::vector<BlockVector *> &value);

BasicBlock *findImmediatePostDom(BasicBlock *block, const PostDominatorTree *pdt);

// Block management.
void changeBlockTarget(BasicBlock *block, BasicBlock *newTarget, unsigned int branchIndex = 0);

// Region analysis.
BlockVector InsertChildren(BasicBlock *block, BlockSet &Set);

// Cloning support.
void CloneDominatorInfo(BasicBlock *block, Map &map, DominatorTree *dt);

// Domination.
//bool IsTopLevel(const Instruction *inst, const PostDominatorTree *pdt);
//BranchVector FindTopLevelBranches(BranchSet Branches,
//                                  const PostDominatorTree *pdt);
BranchVector FindOutermostBranches(BranchSet Branches, const DominatorTree *dt,
                                   const PostDominatorTree *pdt);
BranchInst *FindOutermostBranch(BranchSet &blocks, const DominatorTree *dt);

bool IsInRegion(BasicBlock *Top, BasicBlock *Bottom, BasicBlock *block,
                const DominatorTree *dt, const PostDominatorTree *pdt);
bool isDominated(const Instruction *inst, BranchSet &blocks, const DominatorTree *dt);
bool isDominated(const Instruction *inst, BranchVector &blocks,
                 const DominatorTree *dt);
bool isDominated(const BasicBlock *block, const BlockVector &blocks,
                 const DominatorTree *dt);
bool dominates(const BasicBlock *block, const BranchVector &blocks,
               const DominatorTree *dt);
bool dominatesAll(const BasicBlock *block, const BlockVector &blocks,
                  const DominatorTree *dt);
bool postdominatesAll(const BasicBlock *block, const BlockVector &blocks,
                      const PostDominatorTree *pdt);

ValueVector ToValueVector(InstVector &Insts);

template <class type> void dumpSet(const std::set<type *> &toDump);
template <class type> void dumpVector(const std::vector<type *> &toDump);

bool IsGreaterThan(CmpInst::Predicate Pred);
bool IsEquals(CmpInst::Predicate Pred);
bool IsStrictBranch(const BranchInst *Branch);

// Divergence Utils.

Function *getOpenCLFunctionByName(std::string calleeName, Function *caller);

//------------------------------------------------------------------------------
bool isBarrier(Instruction *inst);
bool isMathFunction(Instruction *inst);
bool isMathName(std::string fName);
bool isLocalMemoryAccess(Instruction *inst);
bool isLocalMemoryStore(Instruction *inst);
bool isLocalMemoryLoad(Instruction *inst);
bool IsIntCast(Instruction *inst);

//------------------------------------------------------------------------------
bool isUsedOutsideOfDefiningBlock(const Instruction *inst);
Instruction *findFirstUser(Instruction *inst);
Instruction *findLastUser(Instruction *inst);
InstVector findUsers(llvm::Value *value);

#endif
