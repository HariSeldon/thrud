#include "thrud/Support/Utils.h"

#include "thrud/Support/DivergentRegion.h"
#include "thrud/Support/RegionBounds.h"

#include "llvm/ADT/STLExtras.h"

#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/PostDominators.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

#include "llvm/Support/InstIterator.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <algorithm>

// OpenCL function names.
const char *BARRIER = "barrier";

//------------------------------------------------------------------------------
bool IsInLoop(const Instruction *I, LoopInfo *LI) {
  const BasicBlock *P = I->getParent();
  return LI->getLoopFor(P) != NULL;
}

//------------------------------------------------------------------------------
bool IsInLoop(const BasicBlock *BB, LoopInfo *LI) {
  return LI->getLoopFor(BB) != NULL;
}

//------------------------------------------------------------------------------
bool IsKernel(const Function *F) {
  const Module *M = F->getParent();
  const llvm::NamedMDNode *KernsMD = M->getNamedMetadata("opencl.kernels");

  if (!KernsMD)
    return false;

  for (unsigned I = 0, E = KernsMD->getNumOperands(); I != E; ++I) {
    const llvm::MDNode &KernMD = *KernsMD->getOperand(I);
    if (KernMD.getOperand(0) == F)
      return true;
  }

  return false;
}

//------------------------------------------------------------------------------
void applyMapToPhiBlocks(PHINode *Phi, Map &map) {
  // FIXME:
  for (unsigned int index = 0; index < Phi->getNumIncomingValues(); ++index) {
    BasicBlock *OldBlock = Phi->getIncomingBlock(index);
    Map::const_iterator It = map.find(OldBlock);

    if (It != map.end()) {
      // I am not proud of this.
      BasicBlock *NewBlock =
          const_cast<BasicBlock *>(cast<BasicBlock>(It->second));
      Phi->setIncomingBlock(index, NewBlock);
    }
  }
}

//------------------------------------------------------------------------------
void applyMap(Instruction *Inst, CoarseningMap &map, unsigned int CF) {
  for (unsigned op = 0, opE = Inst->getNumOperands(); op != opE; ++op) {
    Instruction *Op = dyn_cast<Instruction>(Inst->getOperand(op));
    CoarseningMap::iterator It = map.find(Op);

    if (It != map.end()) {
      InstVector &V = It->second;
      Value *NewValue = V.at(CF);
      Inst->setOperand(op, NewValue);
    }
  }

//  if (PHINode *Phi = dyn_cast<PHINode>(Inst))
//    applyMapToPhiBlocks(Phi, map);
}

//------------------------------------------------------------------------------
void applyMap(Instruction *Inst, Map &map) {
  for (unsigned op = 0, opE = Inst->getNumOperands(); op != opE; ++op) {
    Value *Op = Inst->getOperand(op);

    Map::const_iterator It = map.find(Op);
    if (It != map.end())
      Inst->setOperand(op, It->second);
  }

  if (PHINode *Phi = dyn_cast<PHINode>(Inst))
    applyMapToPhiBlocks(Phi, map);
}

//------------------------------------------------------------------------------
void applyMapToPHIs(BasicBlock *BB, Map &map) {
  for (BasicBlock::iterator I = BB->begin(); isa<PHINode>(I); ++I)
    applyMap(I, map);
}

//------------------------------------------------------------------------------
void applyMap(BasicBlock *BB, Map &map) {
  for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I)
    applyMap(I, map);
}

//------------------------------------------------------------------------------
void printMap(const Map &map) {
  errs() << "==== Map ====\n";
  for (Map::const_iterator I = map.begin(), E = map.end(); I != E; ++I)
    errs() << I->first->getName() << " -> " << I->second->getName() << "\n";
  errs() << "=============\n";
}

//------------------------------------------------------------------------------
void replaceUses(Value *O, Value *N) {
  std::vector<User *> Uses;
  for (Value::use_iterator I = O->use_begin(), E = O->use_end(); I != E; ++I)
    Uses.push_back(*I);
  for (std::vector<User *>::iterator I = Uses.begin(), E = Uses.end(); I != E;
       ++I) {
    if (*I != N)
      (*I)->replaceUsesOfWith(O, N);
  }
}

//------------------------------------------------------------------------------
Function *GetFunctionOfInst(Instruction *I) {
  return I->getParent()->getParent();
}

//------------------------------------------------------------------------------
const Function *GetFunctionOfInst(const Instruction *I) {
  return I->getParent()->getParent();
}

//------------------------------------------------------------------------------
BranchVector FindBranches(Function &F) {
  std::vector<BranchInst *> Result;

  for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I)
    if (BranchInst *branchInst = dyn_cast<BranchInst>(&*I))
      if (branchInst->isConditional() && branchInst->getNumSuccessors() > 1)
        Result.push_back(branchInst);

  return Result;
}

//------------------------------------------------------------------------------
template <class InstructionType>
std::vector<InstructionType *> getInsts(Function &F) {
  std::vector<InstructionType *> Result;

  for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I)
    if (InstructionType *SpecInst = dyn_cast<InstructionType>(&*I))
      Result.push_back(SpecInst);

  return Result;
}

//------------------------------------------------------------------------------
unsigned int GetOperandPosition(User *U, Value *V) {
  for (unsigned int index = 0; index < U->getNumOperands(); ++index)
    if (V == U->getOperand(index))
      return index;
  assert(0 && "Value not used by User");
}

//------------------------------------------------------------------------------
unsigned int GetIntWidth(Value *V) {
  Type *Ty = V->getType();
  IntegerType *IntTy = dyn_cast<IntegerType>(Ty);
  assert(IntTy && "Value type is not integer");
  return IntTy->getBitWidth();
}

//------------------------------------------------------------------------------
ConstantInt *GetConstantInt(unsigned int value, unsigned int width,
                            LLVMContext &C) {
  IntegerType *Integer = IntegerType::get(C, width);
  return ConstantInt::get(Integer, value);
}

//------------------------------------------------------------------------------
int GetInteger(const ConstantInt *CI) { return CI->getSExtValue(); }

//------------------------------------------------------------------------------
bool IsByPointer(const Argument *A) {
  const Type *Ty = A->getType();
  return (Ty->isPointerTy() && !A->hasByValAttr());
}

//------------------------------------------------------------------------------
template <class T> bool isPresent(const T *V, const std::vector<T *> &Vs) {
  typename std::vector<T *>::const_iterator R =
      std::find(Vs.begin(), Vs.end(), V);
  return R != Vs.end();
}

template bool isPresent(const Instruction *V, const InstVector &Vs);
template bool isPresent(const Value *V, const ValueVector &Vs);

//------------------------------------------------------------------------------
template <class T>
bool isPresent(const T *V, const std::vector<const T *> &Vs) {
  typename std::vector<const T *>::const_iterator R =
      std::find(Vs.begin(), Vs.end(), V);
  return R != Vs.end();
}

template bool isPresent(const Instruction *V, const ConstInstVector &Vs);
template bool isPresent(const Value *V, const ConstValueVector &Vs);

//------------------------------------------------------------------------------
template <class T> bool isPresent(const T *V, const std::set<T *> &Vs) {
  typename std::set<T *>::iterator R = std::find(Vs.begin(), Vs.end(), V);
  return R != Vs.end();
}

template bool isPresent(const Instruction *V, const InstSet &Vs);

//------------------------------------------------------------------------------
template <class T> bool isPresent(const T *V, const std::set<const T *> &Vs) {
  typename std::set<const T *>::const_iterator R =
      std::find(Vs.begin(), Vs.end(), V);
  return R != Vs.end();
}

template bool isPresent(const Instruction *V, const ConstInstSet &Vs);

//------------------------------------------------------------------------------
bool isPresent(const Instruction *I, const BlockVector &V) {
  const BasicBlock *BB = I->getParent();
  return isPresent<BasicBlock>(BB, V);
}

//------------------------------------------------------------------------------
bool isPresent(const Instruction *I, std::vector<BlockVector *> &V) {
  for (std::vector<BlockVector *>::iterator Iter = V.begin(), E = V.end();
       Iter != E; ++Iter)
    if (isPresent(I, **Iter))
      return true;
  return false;
}

//------------------------------------------------------------------------------
template <class T>
std::vector<T *> intersection(const std::vector<T *> &A,
                              const std::vector<T *> &B) {
  std::vector<T *> Result;
  for (typename std::vector<T *>::const_iterator IA = A.begin(), EA = A.end();
       IA != EA; ++IA)
    for (typename std::vector<T *>::const_iterator IB = B.begin(), EB = B.end();
         IB != EB; ++IB)
      if (*IA == *IB)
        Result.push_back(*IA);

  return Result;
}

template std::vector<Instruction *>
    intersection(const std::vector<Instruction *> &A,
                 const std::vector<Instruction *> &B);

template std::vector<BranchInst *>
    intersection(const std::vector<BranchInst *> &A,
                 const std::vector<BranchInst *> &B);

//------------------------------------------------------------------------------
template <class T>
std::vector<T *> difference(const std::vector<T *> &A,
                            const std::vector<T *> &B) {
  std::vector<T *> Result;
  for (typename std::vector<T *>::const_iterator IA = A.begin(), EA = A.end();
       IA != EA; ++IA)
    if (!isPresent(*IA, B))
      Result.push_back(*IA);
  return Result;
}

template std::vector<Instruction *>
    difference(const std::vector<Instruction *> &A,
               const std::vector<Instruction *> &B);

template std::vector<BranchInst *>
    difference(const std::vector<BranchInst *> &A,
               const std::vector<BranchInst *> &B);

//------------------------------------------------------------------------------
BasicBlock *findImmediatePostDom(BasicBlock *block, const PostDominatorTree *pdt) {
  return pdt->getNode(block)->getIDom()->getBlock();
}

//------------------------------------------------------------------------------
template <class type> void dumpSet(const std::set<type *> &toDump) {
  for (typename std::set<type *>::const_iterator I = toDump.begin(),
                                                 E = toDump.end();
       I != E; ++I) {
    (*I)->dump();
  }
}

template void dumpSet(const std::set<Instruction *> &toDump);
template void dumpSet(const std::set<BranchInst *> &toDump);

//------------------------------------------------------------------------------
template <class type> void dumpVector(const std::vector<type *> &toDump) {
  for (typename std::vector<type *>::const_iterator I = toDump.begin(),
                                                    E = toDump.end();
       I != E; ++I) {
    (*I)->dump();
  }
}

// Template specialization for BasicBlock.
template <> void dumpVector(const BlockVector &toDump) {
  for (std::vector<BasicBlock *>::const_iterator I = toDump.begin(),
                                                 E = toDump.end();
       I != E; ++I) {
    llvm::errs() << "  " << (*I)->getName() << "\n";
  }
}

template void dumpVector(const std::vector<Instruction *> &toDump);
template void dumpVector(const std::vector<BranchInst *> &toDump);
template void dumpVector(const std::vector<DivergentRegion *> &toDump);
template void dumpVector(const std::vector<PHINode *> &toDump);
template void dumpVector(const std::vector<Value *> &toDump);

//-----------------------------------------------------------------------------
// This is black magic. Don't touch it.
void CloneDominatorInfo(BasicBlock *BB, Map &map, DominatorTree *DT) {
  assert(DT && "DominatorTree is not available");
  Map::iterator BI = map.find(BB);
  assert(BI != map.end() && "BasicBlock clone is missing");
  BasicBlock *NewBB = cast<BasicBlock>(BI->second);

  // NewBB already got dominator info.
  if (DT->getNode(NewBB))
    return;

  assert(DT->getNode(BB) && "BasicBlock does not have dominator info");
  // Entry block is not expected here. Infinite loops are not to cloned.
  assert(DT->getNode(BB)->getIDom() &&
         "BasicBlock does not have immediate dominator");
  BasicBlock *BBDom = DT->getNode(BB)->getIDom()->getBlock();

  // NewBB's dominator is either BB's dominator or BB's dominator's clone.
  BasicBlock *NewBBDom = BBDom;
  Map::iterator BBDomI = map.find(BBDom);
  if (BBDomI != map.end()) {
    NewBBDom = cast<BasicBlock>(BBDomI->second);
    if (!DT->getNode(NewBBDom))
      CloneDominatorInfo(BBDom, map, DT);
  }
  DT->addNewBlock(NewBB, NewBBDom);
}

//------------------------------------------------------------------------------
// FIXME: this can be implemented with set differences.
void Remove(BranchSet &Branches, BranchSet &ToDelete) {
  if (Branches.size() == ToDelete.size()) {
    Branches.clear();
    return;
  }

  if (ToDelete.size() == 0)
    return;

  for (BranchSet::iterator I = ToDelete.begin(), E = ToDelete.end(); I != E;
       ++I)
    Branches.erase(*I);
}

//------------------------------------------------------------------------------
void Remove(BranchSet &Branches, BranchInst *ToDelete) {
  if (Branches.size() == 0)
    return;
  Branches.erase(ToDelete);
}

//------------------------------------------------------------------------------
BranchVector FindOutermostBranches(BranchSet Branches, const DominatorTree *DT,
                                   const PostDominatorTree *PDT) {

  BranchVector Result;
  while (BranchInst *Top = FindOutermostBranch(Branches, DT)) {
    Result.push_back(Top);
    Remove(Branches, Top);
    BasicBlock *TopBlock = Top->getParent();
    BasicBlock *Exiting = findImmediatePostDom(TopBlock, PDT);
    BranchSet ToRemove;
    for (BranchSet::iterator I = Branches.begin(), E = Branches.end(); I != E;
         ++I) {
      BranchInst *Branch = *I;
      BasicBlock *BB = Branch->getParent();
      if (BB != TopBlock && BB != Exiting &&
          IsInRegion(TopBlock, Exiting, BB, DT, PDT))
        ToRemove.insert(Branch);
    }
    Remove(Branches, ToRemove);
  }
  return Result;
}

//------------------------------------------------------------------------------
BranchInst *FindOutermostBranch(BranchSet &Bs, const DominatorTree *DT) {
  for (BranchSet::iterator I = Bs.begin(), E = Bs.end(); I != E; ++I) {
    BranchInst *B = *I;
    if (!isDominated(B, Bs, DT))
      return B;
  }
  return NULL;
}

//------------------------------------------------------------------------------
bool IsInRegion(BasicBlock *Top, BasicBlock *Bottom, BasicBlock *BB,
                const DominatorTree *DT, const PostDominatorTree *PDT) {
  return DT->dominates(Top, BB) && PDT->dominates(Bottom, BB);
}

//------------------------------------------------------------------------------
bool isDominated(const Instruction *I, BranchVector &Bs,
                 const DominatorTree *DT) {
  bool isDominated = false;
  const BasicBlock *BI = I->getParent();
  for (BranchVector::const_iterator Iter = Bs.begin(), E = Bs.end(); Iter != E;
       ++Iter) {
    if (I == *Iter)
      continue;
    const BasicBlock *BII = (*Iter)->getParent();
    isDominated |= DT->dominates(BII, BI);
  }
  return isDominated;
}

//------------------------------------------------------------------------------
bool isDominated(const Instruction *I, BranchSet &Bs, const DominatorTree *DT) {
  bool isDominated = false;
  const BasicBlock *BI = I->getParent();
  for (BranchSet::const_iterator Iter = Bs.begin(), E = Bs.end(); Iter != E;
       ++Iter) {
    if (I == *Iter)
      continue;
    const BasicBlock *BII = (*Iter)->getParent();
    isDominated |= DT->dominates(BII, BI);
  }
  return isDominated;
}

//------------------------------------------------------------------------------
bool derderisDominated(const BasicBlock *BB, const BlockVector &Bs,
                 const DominatorTree *DT) {
  bool isDominated = false;
  for (BlockVector::const_iterator I = Bs.begin(), E = Bs.end(); I != E; ++I) {
    BasicBlock *CurrentB = *I;
    if (BB == CurrentB)
      continue;
    isDominated |= DT->dominates(CurrentB, BB);
  }
  return isDominated;
}

//------------------------------------------------------------------------------
bool dominatesAll(const BasicBlock *BB, const BlockVector &Blocks,
                  const DominatorTree *DT) {
  bool DomAll = true;
  for (BlockVector::const_iterator I = Blocks.begin(), E = Blocks.end(); I != E;
       ++I)
    DomAll &= DT->dominates(BB, *I);
  return DomAll;
}

//------------------------------------------------------------------------------
bool PostdominatesAll(const BasicBlock *BB, const BlockVector &Blocks,
                      const PostDominatorTree *PDT) {
  bool DomAll = true;
  for (BlockVector::const_iterator I = Blocks.begin(), E = Blocks.end(); I != E;
       ++I)
    DomAll &= PDT->dominates(BB, *I);
  return DomAll;
}

//------------------------------------------------------------------------------
void changeBlockTarget(BasicBlock *BB, BasicBlock *NewTarget) {
  TerminatorInst *T = BB->getTerminator();
  assert(T->getNumSuccessors() &&
         "The target can be change only if it is unique");
  T->setSuccessor(0, NewTarget);
}

//------------------------------------------------------------------------------
bool DependsOn(const Value *V, const ValueVector &Rs) {
  ConstValueVector Trace;
  return DependsOnImpl(V, Rs, Trace);
}

//------------------------------------------------------------------------------
bool DependsOn(const Value *V, const Value *R) {
  ConstValueVector Trace;
  return DependsOnImpl(V, R, Trace);
}

//------------------------------------------------------------------------------
bool DependsOnImpl(const Value *V, const Value *R, ConstValueVector &Trace) {
  if (V == R)
    return true;

  if (const User *U = dyn_cast<User>(V)) {
    bool isDependent = false;

    for (User::const_op_iterator UI = U->op_begin(), UE = U->op_end(); UI != UE;
         ++UI) {
      if (const Instruction *I = dyn_cast<Instruction>(UI)) {
        if (!isPresent<Value>(I, Trace)) {
          Trace.push_back(I);
          isDependent |= DependsOnImpl(I, R, Trace);
        }
        // Manage the case in which the V is an argument of the current
        // function.
      } else {
        Value *UV = cast<Value>(UI);
        isDependent |= DependsOnImpl(UV, R, Trace);
      }
    }
    return isDependent;

  } else
    return false;
}

//------------------------------------------------------------------------------
bool DependsOnImpl(const Value *V, const ValueVector &Rs,
                   ConstValueVector &Trace) {
  if (isPresent<Value>(V, Rs))
    return true;

  if (const User *U = dyn_cast<User>(V)) {
    bool isDependent = false;

    for (User::const_op_iterator UI = U->op_begin(), UE = U->op_end(); UI != UE;
         ++UI) {
      if (Instruction *I = dyn_cast<Instruction>(UI)) {
        if (!isPresent<Value>(I, Trace)) {
          Trace.push_back(I);
          isDependent |= DependsOnImpl(I, Rs, Trace);
        }
      } else {
        // Manage the case in which the V is an argument of
        // the current function.
        Value *UV = cast<Value>(UI);
        isDependent |= DependsOnImpl(UV, Rs, Trace);
      }
    }
    return isDependent;

  } else
    return false;
}

//------------------------------------------------------------------------------
InstSet ListPredecessors(Instruction *I) {
  InstSet Result;
  ListPredecessorsImpl(I, Result);
  return Result;
}

//------------------------------------------------------------------------------
void ListPredecessorsImpl(Instruction *I, InstSet &Result) {
  if (User *U = dyn_cast<User>(I))
    for (User::op_iterator UI = U->op_begin(), UE = U->op_end(); UI != UE; ++UI)
      if (Instruction *Inst = dyn_cast<Instruction>(UI))
        if (Result.insert(Inst).second)
          ListPredecessorsImpl(Inst, Result);
}

//------------------------------------------------------------------------------
InstVector ForwardCodeSlicing(InstVector &Insts) {
  InstSet Set = InstSet(Insts.begin(), Insts.end());
  //InstSet Result = InstSet(TIds.begin(), TIds.end());
  InstSet Result;
  ForwardCodeSlicingImpl(Result, Set);
  return InstVector(Result.begin(), Result.end());
}

//------------------------------------------------------------------------------
void ForwardCodeSlicingImpl(InstSet &Insts, InstSet NewInsts) {
  if (NewInsts.empty())
    return;

  InstSet TmpInsts;
  for (InstSet::iterator I = NewInsts.begin(), E = NewInsts.end(); I != E;
       ++I) {
    Instruction *Inst = *I;
    for (Instruction::use_iterator useIter = Inst->use_begin(),
                                   useEnd = Inst->use_end();
         useIter != useEnd; ++useIter) {
      if (Instruction *useInst = dyn_cast<Instruction>(*useIter)) {
        std::pair<InstSet::iterator, bool> result = Insts.insert(useInst);
        if (result.second)
          TmpInsts.insert(useInst);
      }
    }
  }
  ForwardCodeSlicingImpl(Insts, TmpInsts);
}

//------------------------------------------------------------------------------
ValueVector ToValueVector(InstVector &Insts) {
  ValueVector Result;

  for (InstVector::iterator I = Insts.begin(), E = Insts.end(); I != E; ++I)
    Result.push_back(*I);

  return Result;
}

//------------------------------------------------------------------------------
bool IsUsedInFunction(const Function *F, const GlobalVariable *GV) {
  for (Value::const_use_iterator I = GV->use_begin(), E = GV->use_end(); I != E;
       ++I) {
    if (const Instruction *Inst = dyn_cast<Instruction>(*I))
      return (Inst->getParent()->getParent() == F);
  }
  return false;
}

//------------------------------------------------------------------------------
ValueVector GetPointerArgs(Function *F) {
  ValueVector Result;
  for (Function::arg_iterator AI = F->arg_begin(), AE = F->arg_end(); AI != AE;
       ++AI)
    if (IsByPointer(AI))
      Result.push_back(AI);
  return Result;
}

//------------------------------------------------------------------------------
InstVector FindArgDepInst(Function *F) {
  InstVector Result;
  ValueVector Args = GetPointerArgs(F);

  for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    Instruction *Inst = &*I;
    if (DependsOn(Inst, Args))
      Result.push_back(Inst);
  }

  return Result;
}

//------------------------------------------------------------------------------
bool IsStrictBranch(const BranchInst *Branch) {
  Value *Cond = Branch->getCondition();
  if (CmpInst *Cmp = dyn_cast<CmpInst>(Cond))
    return IsEquals(Cmp->getPredicate());
  return false;
}

//------------------------------------------------------------------------------
bool IsGreaterThan(CmpInst::Predicate Pred) {
  return Pred == CmpInst::ICMP_UGT || Pred == CmpInst::ICMP_UGE ||
         Pred == CmpInst::ICMP_SGT || Pred == CmpInst::ICMP_SGE;
}

//------------------------------------------------------------------------------
bool IsEquals(CmpInst::Predicate Pred) { return Pred == CmpInst::ICMP_EQ; }

//------------------------------------------------------------------------------
void GetPHIs(BasicBlock *BB, PHIVector &Phis) {
  PHINode *Phi = NULL;
  for (BasicBlock::iterator I = BB->begin(); (Phi = dyn_cast<PHINode>(I));
       ++I) {
    Phis.push_back(Phi);
  }
}

//------------------------------------------------------------------------------
// Build a mapping between the old phis of an exiting block and the new ones.
// This is needed because instructions outside the region need to see the new
// values.
void BuildExitingPhiMap(BasicBlock *OldExiting, BasicBlock *NewExiting,
                        Map &map) {
  assert(OldExiting->size() == NewExiting->size());
  for (BasicBlock::iterator IO = OldExiting->begin(), IN = NewExiting->begin();
       isa<PHINode>(IO) && isa<PHINode>(IN);) {
    map[IO] = IN;
    ++IO;
    ++IN;
  }
}

//------------------------------------------------------------------------------
void remapBlocksInPHIs(BasicBlock *Target, BasicBlock *OldBlock,
                       BasicBlock *NewBlock) {
  Map PhiMap;
  PhiMap[OldBlock] = NewBlock;
  applyMapToPHIs(Target, PhiMap);
}

//------------------------------------------------------------------------------
// Build a mapping: TIds -> CF * TId + CI.
// Vector format:
// TIds: list of all the values output of get_local_id or get_loacal_id.
// NewTIds: list of all the tid updated values grouped by original value:
// 2 * tid1, 2 * tid1 + 1, 2 * tid1 + 2, ...
// 2 * tid2, 2 * tid2 + 1, 2 * tid2 + 2, ...
void InitializeMap(Map &map, const InstVector &TIds, const InstVector &NewTIds,
                   unsigned int CI, unsigned int CF) {
  unsigned int N = TIds.size();
  for (unsigned int ThreadIdIndex = 0; ThreadIdIndex < N; ++ThreadIdIndex)
    map[TIds[ThreadIdIndex]] = NewTIds[(ThreadIdIndex + 1) * CF - CI - 1];
}

//------------------------------------------------------------------------------
Function *GetOpenCLFunctionByName(std::string calleeName, Function *caller) {
  Module &module = *caller->getParent();
  Function *callee = module.getFunction(calleeName);

  if (callee == NULL)
    return NULL;

  assert(callee->arg_size() == 1 && "Wrong OpenCL function");
  return callee;
}

//------------------------------------------------------------------------------
// Divergence Utils.
//------------------------------------------------------------------------------
BranchVector GetThreadDepBranches(BranchVector &Bs, ValueVector TIds) {
  BranchVector Result;

  for (BranchVector::iterator I = Bs.begin(), E = Bs.end(); I != E; ++I) {
    BranchInst *BInst = *I;
    if (DependsOn(BInst, TIds))
      Result.push_back(BInst);
  }
  return Result;
}

//------------------------------------------------------------------------------
RegionVector GetDivergentRegions(BranchVector &BTId, DominatorTree *DT,
                                 PostDominatorTree *PDT, LoopInfo *LI) {
//  BranchSet AllBranches(BTId.begin(), BTId.end());
//
//  BranchVector Bs = FindOutermostBranches(AllBranches, DT, PDT);
//  std::vector<DivergentRegion *> DRs;
//
//  for (BranchVector::iterator I = Bs.begin(), E = Bs.end(); I != E; ++I) {
//    BasicBlock *BB = (*I)->getParent();
//    BasicBlock *PDom = findImmediatePostDom(BB, PDT);
//
//    if (LI->isLoopHeader(BB)) {
//      Loop *L = LI->getLoopFor(BB);
//      if (L == LI->getLoopFor(PDom))
//        PDom = L->getExitBlock();
//    }
//
//    DRs.push_back(new DivergentRegion(BB, PDom));
//  }
//
//  FillRegions(DRs, DT, PDT);
//
//  return DRs;
}

//------------------------------------------------------------------------------
InstVector GetInstToReplicate(InstVector &TIdInsts, InstVector &TIds,
                              InstVector &AllTIds) {
  InstSet Result(TIdInsts.begin(), TIdInsts.end());

  for (InstVector::iterator I = TIdInsts.begin(), E = TIdInsts.end(); I != E;
       ++I)
    ListPredecessorsImpl(*I, Result);

  InstVector Tmp(Result.begin(), Result.end());
  return difference<Instruction>(Tmp, AllTIds);
}

// Compiler optimizations.
//------------------------------------------------------------------------------
InstVector GetInstToReplicateOutsideRegionCores(InstVector &TIdInsts,
                                                InstVector &TIds,
                                                RegionVector &DRs,
                                                InstVector &AllTIds) {
  InstVector Insts = GetInstToReplicate(TIdInsts, TIds, AllTIds);
  InstSet InstsSet(Insts.begin(), Insts.end());
  InstSet ToRemove;

  for (InstVector::iterator I = Insts.begin(), E = Insts.end(); I != E; ++I) {
    for (std::vector<DivergentRegion *>::iterator regionIter = DRs.begin(),
                                                  regionEnd = DRs.end();
         regionIter != regionEnd; ++regionIter) {
      DivergentRegion *region = *regionIter;
      if(containsInternally(*region, *I))
        ToRemove.insert(*I);
    }
  }

  for (InstSet::iterator I = ToRemove.begin(), E = ToRemove.end(); I != E;
       ++I) {
    InstsSet.erase(*I);
  }

  InstVector Result(InstsSet.begin(), InstsSet.end());
  return Result;
}

//------------------------------------------------------------------------------
InstVector GetInstToReplicateOutsideRegions(InstVector &TIdInsts,
                                            InstVector &TIds, RegionVector &DRs,
                                            InstVector &AllTIds) {
  InstVector Insts = GetInstToReplicate(TIdInsts, TIds, AllTIds);
  InstSet InstsSet(Insts.begin(), Insts.end());
  InstSet ToRemove;

  for (InstVector::iterator I = Insts.begin(), E = Insts.end(); I != E; ++I) {
    for (std::vector<DivergentRegion *>::iterator regionIter = DRs.begin(),
                                                  regionEnd = DRs.end();
         regionIter != regionEnd; ++regionIter) {
      DivergentRegion *region = *regionIter;
      if(containsInternally(*region, *I))
        ToRemove.insert(*I);
    }
  }

  for (InstSet::iterator I = ToRemove.begin(), E = ToRemove.end(); I != E;
       ++I) {
    InstsSet.erase(*I);
  }

  InstVector Result(InstsSet.begin(), InstsSet.end());
  return Result;
}

//------------------------------------------------------------------------------
// Region and Branch Analysis.
//------------------------------------------------------------------------------
// Find the operand that depends on the thread id.
// If more than one operand depends on the tid retun NULL.
Value *GetTIdOperand(CmpInst *Cmp, ValueVector &TIds) {
  Value *Result = NULL;
  for (CmpInst::op_iterator I = Cmp->op_begin(), E = Cmp->op_end(); I != E;
       ++I) {
    Value *V = I->get();
    if (DependsOn(V, TIds)) {
      if (Result == NULL)
        Result = V;
      else
        return NULL;
    }
  }
  return Result;
}

//------------------------------------------------------------------------------
Instruction *getMulInst(Value *V, unsigned int CoarseningFactor) {
  unsigned int width = GetIntWidth(V);
  ConstantInt *CF = GetConstantInt(CoarseningFactor, width, V->getContext());
  Instruction *Mul = BinaryOperator::Create(Instruction::Mul, V, CF);
  Mul->setName(V->getName() + ".." + Twine(CoarseningFactor));
  return Mul;
}

//------------------------------------------------------------------------------
Instruction *getAddInst(Value *V, unsigned int CoarseningIndex) {
  unsigned int width = GetIntWidth(V);
  ConstantInt *I = GetConstantInt(CoarseningIndex, width, V->getContext());
  Instruction *Add = BinaryOperator::Create(Instruction::Add, V, I);
  Add->setName(V->getName() + ".." + Twine(CoarseningIndex));
  return Add;
}

//------------------------------------------------------------------------------
Instruction *getAddInst(Value *V1, Value *V2) {
  Instruction *Add = BinaryOperator::Create(Instruction::Add, V1, V2);
  Add->setName(V1->getName() + "..Add");
  return Add;
}

//------------------------------------------------------------------------------
Instruction *getShiftInst(Value *V, unsigned int shift) {
  unsigned int width = GetIntWidth(V);
  ConstantInt *I = GetConstantInt(shift, width, V->getContext());
  Instruction *Shift = BinaryOperator::Create(Instruction::LShr, V, I);
  Shift->setName(Twine(V->getName()) + "..Shift");
  return Shift;
}

//------------------------------------------------------------------------------
Instruction *getAndInst(Value *V, unsigned int factor) {
  unsigned int width = GetIntWidth(V);
  ConstantInt *I = GetConstantInt(factor, width, V->getContext());
  Instruction *And = BinaryOperator::Create(Instruction::And, V, I);
  And->setName(Twine(V->getName()) + "..And");
  return And;
}

//------------------------------------------------------------------------------
bool isBarrier(Instruction *I) {
  if (CallInst *Inst = dyn_cast<CallInst>(I)) {
    Function *F = Inst->getCalledFunction();
    return F->getName() == "barrier";
  }
  return false;
}

//------------------------------------------------------------------------------
bool isLocalMemoryAccess(Instruction *I) {
  return isLocalMemoryStore(I) || isLocalMemoryLoad(I);
}

//------------------------------------------------------------------------------
bool isLocalMemoryStore(Instruction *I) {
  if (StoreInst *S = dyn_cast<StoreInst>(I)) {
    return (S->getPointerAddressSpace() == LOCAL_AS);
  }
  return false;
}

//------------------------------------------------------------------------------
bool isLocalMemoryLoad(Instruction *I) {
  if (LoadInst *L = dyn_cast<LoadInst>(I)) {
    return (L->getPointerAddressSpace() == LOCAL_AS);
  }
  return false;
}

//------------------------------------------------------------------------------
bool isMathFunction(Instruction *I) {
  if (CallInst *Inst = dyn_cast<CallInst>(I)) {
    Function *F = Inst->getCalledFunction();
    return isMathName(F->getName().str());
  }
  return false;
}

//------------------------------------------------------------------------------
bool isMathName(std::string fName) {
  bool begin = (fName[0] == '_' && fName[1] == 'Z');
  bool value = ((fName.find("sin") != std::string::npos) ||
                (fName.find("cos") != std::string::npos) ||
                (fName.find("exp") != std::string::npos) ||
                (fName.find("acos") != std::string::npos) ||
                (fName.find("asin") != std::string::npos) ||
                (fName.find("atan") != std::string::npos) ||
                (fName.find("tan") != std::string::npos) ||
                (fName.find("ceil") != std::string::npos) ||
                (fName.find("exp2") != std::string::npos) ||
                (fName.find("exp10") != std::string::npos) ||
                (fName.find("fabs") != std::string::npos) ||
                (fName.find("abs") != std::string::npos) ||
                (fName.find("fma") != std::string::npos) ||
                (fName.find("max") != std::string::npos) ||
                (fName.find("fmax") != std::string::npos) ||
                (fName.find("min") != std::string::npos) ||
                (fName.find("fmin") != std::string::npos) ||
                (fName.find("log") != std::string::npos) ||
                (fName.find("log2") != std::string::npos) ||
                (fName.find("mad") != std::string::npos) ||
                (fName.find("pow") != std::string::npos) ||
                (fName.find("pown") != std::string::npos) ||
                (fName.find("root") != std::string::npos) ||
                (fName.find("rootn") != std::string::npos) ||
                (fName.find("sqrt") != std::string::npos) ||
                (fName.find("trunc") != std::string::npos) ||
                (fName.find("rsqrt") != std::string::npos) ||
                (fName.find("rint") != std::string::npos) ||
                (fName.find("ceil") != std::string::npos) ||
                (fName.find("round") != std::string::npos) ||
                (fName.find("hypot") != std::string::npos) ||
                (fName.find("cross") != std::string::npos) ||
                (fName.find("mix") != std::string::npos) ||
                (fName.find("clamp") != std::string::npos) ||
                (fName.find("normalize") != std::string::npos) ||
                (fName.find("floor") != std::string::npos));
  return begin && value;
}

//------------------------------------------------------------------------------
void safeIncrement(std::map<std::string, unsigned int> &map, std::string key) {
  std::map<std::string, unsigned int>::iterator iter = map.find(key);
  if (iter == map.end())
    map[key] = 1;
  else
    map[key] += 1;
}

//------------------------------------------------------------------------------
bool isUsedOutsideOfDefiningBlock(const Instruction *I) {
  if (I->use_empty())
    return false;
  if (isa<PHINode>(I))
    return true;
  const BasicBlock *BB = I->getParent();
  for (Value::const_use_iterator UI = I->use_begin(), E = I->use_end(); UI != E;
       ++UI) {
    const User *U = *UI;
    if (cast<Instruction>(U)->getParent() != BB || isa<PHINode>(U))
      return true;
  }
  return false;
}

//------------------------------------------------------------------------------
// Build a vector with all the uses of the given value.
InstVector findUsers(llvm::Value *value) {
  InstVector result;
  for (Value::use_iterator use = value->use_begin(), end = value->use_end();
       use != end; ++use) {
    if (Instruction *inst = dyn_cast<Instruction>(*use)) {
      result.push_back(inst);
    }
  }

  return result;
}

//------------------------------------------------------------------------------
InstVector filterUsers(llvm::Instruction *used, InstVector &users) {
  BasicBlock *block = used->getParent();
  InstVector result;
  for (InstVector::iterator iter = users.begin(), end = users.end();
       iter != end; ++iter) {
    Instruction *inst = *iter;
    if (inst->getParent() == block)
      result.push_back(inst);
  }
  return result;
}

//------------------------------------------------------------------------------
// Find the last user of input instruction in its parent block.
// Return NULL if no use is found.
Instruction *findLastUser(Instruction *I) {
  InstVector users = findUsers(I);
  users = filterUsers(I, users);
  Instruction *lastUser = NULL;
  unsigned int maxDistance = 0;

  BasicBlock::iterator begin(I);
  for (InstVector::iterator iter = users.begin(), end = users.end();
       iter != end; ++iter) {
    Instruction *inst = *iter;
    BasicBlock::iterator blockIter(inst);
    unsigned int currentDist = std::distance(begin, blockIter);
    if (currentDist > maxDistance) {
      maxDistance = currentDist;
      lastUser = inst;
    }
  }

  return lastUser;
}

//------------------------------------------------------------------------------
// Find first user of input instruction in its parent block.
// Return NULL if no use is found.
Instruction *findFirstUser(Instruction *I) {
  InstVector users = findUsers(I);
  Instruction *firstUser = NULL;
  unsigned int minDistance = I->getParent()->size();

  BasicBlock::iterator begin(I);
  for (InstVector::iterator iter = users.begin(), end = users.end();
       iter != end; ++iter) {
    Instruction *inst = *iter;
    BasicBlock::iterator blockIter(inst);
    unsigned int currentDist = std::distance(begin, blockIter);
    if (currentDist < minDistance) {
      minDistance = currentDist;
      firstUser = inst;
    }
  }

  return firstUser;
}

//------------------------------------------------------------------------------
bool IsIntCast(Instruction *I) {
  if(CallInst *call = dyn_cast<CallInst>(I)) {
    Function *callee = call->getCalledFunction();
    std::string name = callee->getName();
    bool begin = (name[0] == '_' && name[1] == 'Z');
    bool value = ((name.find("as_uint") != std::string::npos) ||
                  (name.find("as_int") != std::string::npos));
    return begin && value;
  }
  return false;
}

//------------------------------------------------------------------------------
void renameValueWithFactor(Value *value, StringRef oldName, unsigned int index) {
  if (!oldName.empty())
    value->setName(oldName + "..cf" + Twine(index + 2));
}
