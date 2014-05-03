#include "thrud/Support/Utils.h"

#include "thrud/Support/DivergentRegion.h"
#include "thrud/Support/RegionBounds.h"
#include "thrud/Support/OCLEnv.h"

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
bool isInLoop(const Instruction *inst, LoopInfo *loopInfo) {
  const BasicBlock *block = inst->getParent();
  return loopInfo->getLoopFor(block) != NULL;
}

//------------------------------------------------------------------------------
bool isInLoop(const BasicBlock *block, LoopInfo *loopInfo) {
  return loopInfo->getLoopFor(block) != NULL;
}

//------------------------------------------------------------------------------
bool isKernel(const Function *function) {
  const Module *module = function->getParent();
  const llvm::NamedMDNode *kernelsMD =
      module->getNamedMetadata("opencl.kernels");

  if (!kernelsMD)
    return false;

  for (int index = 0, end = kernelsMD->getNumOperands(); index != end;
       ++index) {
    const llvm::MDNode &kernelMD = *kernelsMD->getOperand(index);
    if (kernelMD.getOperand(0) == function)
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
void applyMap(InstVector &insts, Map &map, InstVector &result) {
  result.clear();
  result.reserve(insts.size());

  for (InstVector::iterator iter = insts.begin(), iterEnd = insts.end();
       iter != iterEnd; ++iter) {
    Value *newValue = map[*iter];
    if (newValue != NULL) {
      if (Instruction *inst = dyn_cast<Instruction>(newValue)) {
        result.push_back(inst);
      }
    }
  }
}

//------------------------------------------------------------------------------
void dump(const Map &map) {
  errs() << "==== Map ====\n";
  for (Map::const_iterator I = map.begin(), E = map.end(); I != E; ++I)
    errs() << I->first->getName() << " -> " << I->second->getName() << "\n";
  errs() << "=============\n";
}

//------------------------------------------------------------------------------
void dumpV2V(const V2VMap &map) {
  errs() << "==== Map ====\n";
  for (V2VMap::const_iterator I = map.begin(), E = map.end(); I != E; ++I)
    errs() << I->first->getName() << " -> " << I->second->getName() << "\n";
  errs() << "=============\n";
}

//------------------------------------------------------------------------------
void replaceUses(Value *oldValue, Value *newValue) {
  std::vector<User *> uses;
  std::copy(oldValue->use_begin(), oldValue->use_end(),
            std::back_inserter(uses));

  for (std::vector<User *>::iterator I = uses.begin(), E = uses.end(); I != E;
       ++I) {
    if (*I != newValue)
      (*I)->replaceUsesOfWith(oldValue, newValue);
  }
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
ConstantInt *GetConstantInt(unsigned int value, unsigned int width,
                            LLVMContext &C) {
  IntegerType *Integer = IntegerType::get(C, width);
  return ConstantInt::get(Integer, value);
}

//------------------------------------------------------------------------------
bool IsByPointer(const Argument *A) {
  const Type *Ty = A->getType();
  return (Ty->isPointerTy() && !A->hasByValAttr());
}

//------------------------------------------------------------------------------
BasicBlock *findImmediatePostDom(BasicBlock *block,
                                 const PostDominatorTree *pdt) {
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

template <> void dumpSet(const BlockSet &toDump) {
  for (BlockSet::iterator iter = toDump.begin(), iterEnd = toDump.end();
       iter != iterEnd; ++iter) {
    errs() << (*iter)->getName() << " ";
  }
  errs() << "\n";
}

template void dumpSet(const InstSet &toDump);
template void dumpSet(const std::set<BranchInst *> &toDump);
template void dumpSet(const BlockSet &toDump);

//------------------------------------------------------------------------------
template <class type> void dumpVector(const std::vector<type *> &toDump) {
  errs() << "Size: " << toDump.size() << "\n";
  for (typename std::vector<type *>::const_iterator I = toDump.begin(),
                                                    E = toDump.end();
       I != E; ++I) {
    (*I)->dump();
  }
}

// Template specialization for BasicBlock.
template <> void dumpVector(const BlockVector &toDump) {
  errs() << "Size: " << toDump.size() << "\n";
  for (BlockVector::const_iterator I = toDump.begin(), E = toDump.end(); I != E;
       ++I) {
    errs() << (*I)->getName() << " -- ";
  }
  errs() << "\n";
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
BranchInst *FindOutermostBranch(BranchSet &Bs, const DominatorTree *DT) {
  for (BranchSet::iterator I = Bs.begin(), E = Bs.end(); I != E; ++I) {
    BranchInst *B = *I;
    if (!isDominated(B, Bs, DT))
      return B;
  }
  return NULL;
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
bool isDominated(const BasicBlock *BB, const BlockVector &Bs,
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
void changeBlockTarget(BasicBlock *block, BasicBlock *newTarget,
                       unsigned int branchIndex) {
  TerminatorInst *terminator = block->getTerminator();
  assert(terminator->getNumSuccessors() &&
         "The target can be change only if it is unique");
  terminator->setSuccessor(branchIndex, newTarget);
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
void getPHIs(BasicBlock *BB, PhiVector &Phis) {
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
void remapBlocksInPHIs(BasicBlock *block, BasicBlock *oldBlock,
                       BasicBlock *newBlock) {
  Map phiMap;
  phiMap[oldBlock] = newBlock;
  applyMapToPHIs(block, phiMap);
}

//------------------------------------------------------------------------------
Function *getOpenCLFunctionByName(std::string calleeName, Function *caller) {
  Module &module = *caller->getParent();
  Function *callee = module.getFunction(calleeName);

  if (callee == NULL)
    return NULL;

  assert(callee->arg_size() == 1 && "Wrong OpenCL function");
  return callee;
}

//------------------------------------------------------------------------------
// Region and Branch Analysis.
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
    return (S->getPointerAddressSpace() == OCLEnv::LOCAL_AS);
  }
  return false;
}

//------------------------------------------------------------------------------
bool isLocalMemoryLoad(Instruction *I) {
  if (LoadInst *L = dyn_cast<LoadInst>(I)) {
    return (L->getPointerAddressSpace() == OCLEnv::LOCAL_AS);
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
void safeIncrement(std::map<std::string, int> &map, std::string key) {
  std::map<std::string, int>::iterator iter = map.find(key);
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
  int maxDistance = 0;

  BasicBlock::iterator begin(I);
  for (InstVector::iterator iter = users.begin(), end = users.end();
       iter != end; ++iter) {
    Instruction *inst = *iter;
    BasicBlock::iterator blockIter(inst);
    int currentDist = std::distance(begin, blockIter);
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
  int minDistance = I->getParent()->size();

  BasicBlock::iterator begin(I);
  for (InstVector::iterator iter = users.begin(), end = users.end();
       iter != end; ++iter) {
    Instruction *inst = *iter;
    BasicBlock::iterator blockIter(inst);
    int currentDist = std::distance(begin, blockIter);
    if (currentDist < minDistance) {
      minDistance = currentDist;
      firstUser = inst;
    }
  }

  return firstUser;
}

//------------------------------------------------------------------------------
bool IsIntCast(Instruction *I) {
  if (CallInst *call = dyn_cast<CallInst>(I)) {
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

// isPresent.
//------------------------------------------------------------------------------
template <class T> bool isPresent(const T *V, const std::vector<T *> &Vs) {
  typename std::vector<T *>::const_iterator R =
      std::find(Vs.begin(), Vs.end(), V);
  return R != Vs.end();
}

template bool isPresent(const Instruction *V, const InstVector &Vs);
template bool isPresent(const Value *V, const ValueVector &Vs);

template <class T>
bool isPresent(const T *V, const std::vector<const T *> &Vs) {
  typename std::vector<const T *>::const_iterator R =
      std::find(Vs.begin(), Vs.end(), V);
  return R != Vs.end();
}

template bool isPresent(const Instruction *V, const ConstInstVector &Vs);
template bool isPresent(const Value *V, const ConstValueVector &Vs);

template <class T> bool isPresent(const T *V, const std::set<T *> &Vs) {
  typename std::set<T *>::iterator R = std::find(Vs.begin(), Vs.end(), V);
  return R != Vs.end();
}

template bool isPresent(const Instruction *V, const InstSet &Vs);

template <class T> bool isPresent(const T *V, const std::set<const T *> &Vs) {
  typename std::set<const T *>::const_iterator R =
      std::find(Vs.begin(), Vs.end(), V);
  return R != Vs.end();
}

template bool isPresent(const Instruction *V, const ConstInstSet &Vs);

template <class T> bool isPresent(const T *value, const std::deque<T *> &d) {
  typename std::deque<T *>::const_iterator iter =
      std::find(d.begin(), d.end(), value);
  return iter != d.end();
}

template bool isPresent(const BasicBlock *value, const BlockDeque &deque);

bool isPresent(const Instruction *I, const BlockVector &V) {
  const BasicBlock *BB = I->getParent();
  return isPresent<BasicBlock>(BB, V);
}

bool isPresent(const Instruction *I, std::vector<BlockVector *> &V) {
  for (std::vector<BlockVector *>::iterator Iter = V.begin(), E = V.end();
       Iter != E; ++Iter)
    if (isPresent(I, **Iter))
      return true;
  return false;
}
