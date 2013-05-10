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

#include <set>
#include <vector>

using namespace llvm;

#define LOCAL_AS 3

// OpenCL management.
bool IsKernel(const Function *F);

void safeIncrement(std::map<std::string, unsigned int> &inputMap, std::string key);

// Map management.
// Apply the given map to the given instruction.
void ApplyMap(Instruction *Inst, const Map &map);
void ApplyMap(BasicBlock *BB, const Map &map);
void ApplyMapToPHIs(BasicBlock *BB, Map &map);
void ApplyMapToPhiBlocks(PHINode *Phi, const Map &map);
// Prints to stderr the given map. For debug only.
void printMap (const Map &map);

// Replate all the usages of O with N.
void SubstituteUsages(Value *O, Value *N);
void BuildExitingPhiMap(BasicBlock *OldExiting, BasicBlock *NewExiting, 
                        Map &map);
void RemapBlocksInPHIs(BasicBlock* Target,
                       BasicBlock *OldBlock, BasicBlock *NewBlock);
void InitializeMap(Map &map, const InstVector &TIds, const InstVector &NewTIds,
                   unsigned int CI, unsigned int CF);

// Instruction management.
Function *GetInstFunction(Instruction *I);
const Function *GetInstFunction(const Instruction *I);
BranchVector FindBranches(Function &F);
template <class InstructionType> 
std::vector<InstructionType*> getInsts(Function &F);
unsigned int GetOperandPosition(User *U, Value *V);
void GetPHIs(BasicBlock *BB, PHIVector &Phis);

// Function and module management.
ValueVector GetMemoryValues(Function *F);
ValueVector GetPointerArgs(Function *F);
InstVector FindArgDepInst(Function *F);
ValueVector GetLocalBuffers(Function *M);
bool IsLocalBuffer(const Function *F, const GlobalVariable *GV);
bool IsUsedInFunction(const Function *F, const GlobalVariable *GV);

// Type management.
// Build a constant from the given integer. 
unsigned int GetIntWidth(Value* V);
ConstantInt *GetConstantInt(unsigned int value, unsigned int width, 
                            LLVMContext &C);
int GetInteger(const ConstantInt *CI);
// Verify if the given value is a constant integer and equals the given number.
bool IsByPointer(const Argument *A);

// Container management.
// Check if the given element is present in the given container. 
template <class T> bool IsPresent(const T *value, 
                                  const std::vector<T*> &vector);
template <class T> bool IsPresent(const T *value, 
                                  const std::vector<const T*> &vector);
template <class T> bool IsPresent(const T *value, 
                                  const std::set<T*> &vector);
template <class T> bool IsPresent(const T *value, 
                                  const std::set<const T*> &vector);

// TODO: remove these and replace with std!!!
InstVector intersection(const InstVector &A, const InstVector &B);
InstVector difference(const InstVector &A, const InstVector &B);

template <class T>
std::vector<T*> intersection(const std::vector<T*> &A,
                             const std::vector<T*> &B);

template <class T>
std::vector<T*> difference(const std::vector<T*> &A, 
                           const std::vector<T*> &B);

bool IsPresent(const Instruction *I, const BlockVector &V);
bool IsPresent(const Instruction *I, std::vector<BlockVector*> &V);

BasicBlock *FindImmediatePostDom(BasicBlock *BB, const PostDominatorTree *PDT);

// Block management.
void ChangeBlockTarget(BasicBlock *BB, BasicBlock *NewTarget);

// Region analysis.
BlockVector *ListBlocks(RegionBounds *Bounds);
unsigned int getInstructionNumberInRegion(DivergentRegion* R);
void ListBlocksImpl(const BasicBlock *End, BasicBlock *BB, BlockSet &Set);
BlockVector InsertChildren(BasicBlock *BB, BlockSet &Set);
BlockVector BuildPredList(RegionVector &Regions, LoopInfo *LI);
std::vector<RegionBounds*> BuildInsertionPoints(RegionVector &Regions);

// Cloning support.
void CloneDominatorInfo(BasicBlock *BB, Map &map, DominatorTree* DT);
RegionBounds CloneRegion(RegionBounds *Bounds, const Twine& suffix,
                         DominatorTree* DT, Map& map, const Map& ToApply);

// Domination.
//bool IsTopLevel(const Instruction *I, const PostDominatorTree *PDT);
//BranchVector FindTopLevelBranches(BranchSet Branches,
//                                  const PostDominatorTree *PDT);
BranchVector FindOutermostBranches(BranchSet Branches,
                                   const DominatorTree *DT,
                                   const PostDominatorTree *PDT);
BranchInst *FindOutermostBranch(BranchSet &Bs, const DominatorTree *DT);

bool IsInRegion(BasicBlock *Top, BasicBlock *Bottom,
                BasicBlock *BB,
                const DominatorTree *DT, const PostDominatorTree *PDT);
bool IsDominated(const Instruction *I, BranchSet &Bs, const DominatorTree *DT);
bool IsDominated(const Instruction *I, BranchVector &Bs, 
                 const DominatorTree *DT);
bool IsDominated(const BasicBlock *BB, const BlockVector &Bs,
                 const DominatorTree *DT);
bool Dominates(const BasicBlock *BB, const BranchVector &Bs, 
               const DominatorTree *DT);
bool DominatesAll(const BasicBlock *BB, 
                  const BlockVector &Blocks, 
                  const DominatorTree *DT);
bool PostDominatesAll(const BasicBlock *BB, 
                      const BlockVector &Blocks,
                      const PostDominatorTree *PDT);
RegionBounds *FindBounds(BlockVector &Blocks,
                      DominatorTree *DT, PostDominatorTree *PDT);

// Scalar Evolution analysis.
int AnalyzeSubscript(ScalarEvolution* SE, const SCEV *Scev, 
                     ValueVector &TIds,
                     SmallPtrSet<const SCEV*, 8> &Processed);
int AnalyzePHI(ScalarEvolution* SE, PHINode *V, ValueVector &TIds,
               SmallPtrSet<const SCEV*, 8> &Processed);
int AnalyzeAdd(ScalarEvolution* SE, const SCEVAddExpr *Scev, 
               ValueVector &TIds,
               SmallPtrSet<const SCEV*, 8> &Processed);
int AnalyzeMultiplication(const SCEVNAryExpr *Scev, ValueVector &TIds);
int AnalyzeFactors(const SCEVUnknown *U, const SCEVConstant *C,
                   ValueVector &TIds);

// Dependance analysis.
// Return true if V depends on any of the values in Rs. 
bool DependsOn(const Value *V, const ValueVector &Rs);

// Return true is V depends on R.
bool DependsOn(const Value *V, const Value *R);

// Recursive function that determines if V depends on R.
bool DependsOnImpl(const Value *V, const Value *R, ConstValueVector &Trace);

// Recursive function that determines if V depends on R.
bool DependsOnImpl(const Value *V, const ValueVector &Rs,
                   ConstValueVector &Trace);

// List the predecessors of the given instruction: apply backward code slicing.
InstSet ListPredecessors(Instruction *I);
void ListPredecessorsImpl(Instruction *I, InstSet &Result);

InstVector ForwardCodeSlicing(InstVector &TIds);
void ForwardCodeSlicingImpl(InstSet &Insts, InstSet NewInsts);

ValueVector ToValueVector(InstVector &Insts);

template <class type> void dumpSet(const std::set<type*> &toDump);
template <class type> void dumpVector(const std::vector<type*> &toDump);

bool IsGreaterThan(CmpInst::Predicate Pred);
bool IsEquals(CmpInst::Predicate Pred);
bool IsStrictBranch(const BranchInst *Branch);

// Divergence Utils.

// Find all the get_global_id or get_local_id function calls.
InstVector FindThreadIds(Function *F);
InstVector FindThreadIds(Function *F, int Dim);
// Find all get_global_size or get_local_size function calls.
InstVector FindSpaceSizes(Function *F);
InstVector FindSpaceSizes(Function *F, int Dim);
// Find all get_group_id function calls.
InstVector FindGroupIds(Function *F);
InstVector FindGroupIds(Function *F, int Dim);

// Find all the instructions which depend on the TId.
InstVector FindThreadDepInst(Function *F, ValueVector &TIds);

BranchVector GetThreadDepBranches(BranchVector &Bs, ValueVector TIds);

//------------------------------------------------------------------------------
// Divergent regions analysis.
std::vector<DivergentRegion*> GetDivergentRegions(BranchVector &BTId,
                                                  DominatorTree *DT,
                                                  PostDominatorTree *PDT,
                                                  LoopInfo *LI);
void FillRegions(std::vector<DivergentRegion*> &DRs,
                 DominatorTree *DT, PostDominatorTree *PDT);

InstVector GetInstToReplicate(InstVector &TIdInsts, InstVector &TIds,
                              InstVector &AllTIds);

InstVector GetInstToReplicateOutsideRegions(InstVector &TIdInsts,
                                            InstVector &TIds,
                                            RegionVector &DRs,
                                            InstVector &AllTIds);

Value *GetTIdOperand(CmpInst* Cmp, ValueVector &TIds);

//------------------------------------------------------------------------------
// Instruction creation.
Instruction *getMulInst(Value *V, unsigned int factor);
Instruction *getAddInst(Value *V, unsigned int addend);
Instruction *getAddInst(Value *V1, Value *V2);
Instruction *getShiftInst(Value *V, unsigned int shift);
Instruction *getAndInst(Value *V, unsigned int factor);

//------------------------------------------------------------------------------
bool isBarrier(Instruction *I);
bool isMathFunction(Instruction *I);
bool isMathName(std::string fName); 
bool isLocalMemoryAccess(Instruction *I);
bool isLocalMemoryStore(Instruction *I);
bool isLocalMemoryLoad(Instruction *I);

//------------------------------------------------------------------------------
bool isUsedOutsideOfDefiningBlock(const Instruction *I);
Instruction *findFirstUser(Instruction *I);
Instruction *findLastUser(Instruction *I);
InstVector findUsers(llvm::Value *value);
