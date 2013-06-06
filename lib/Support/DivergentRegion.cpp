#include "thrud/Support/DivergentRegion.h"

#include "thrud/Support/RegionBounds.h"
#include "thrud/Support/Utils.h"

#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Support/raw_ostream.h"

//------------------------------------------------------------------------------
DivergentRegion::DivergentRegion(BasicBlock *Header, BasicBlock *Exiting) {
  Bounds = new RegionBounds(Header, Exiting);
  this->Condition = DivergentRegion::ND;
}

//------------------------------------------------------------------------------
BasicBlock *DivergentRegion::getHeader() {
  return Bounds->getHeader();
}

//------------------------------------------------------------------------------
BasicBlock *DivergentRegion::getExiting() {
  return Bounds->getExiting();
}

//------------------------------------------------------------------------------
void DivergentRegion::setHeader(BasicBlock *Header) {
  this->Bounds->setHeader(Header);
}

//------------------------------------------------------------------------------
void DivergentRegion::setExiting(BasicBlock *Exiting) {
  this->Bounds->setExiting(Exiting);
}

//------------------------------------------------------------------------------
RegionBounds *DivergentRegion::getBounds() {
  return Bounds;
}

//------------------------------------------------------------------------------
BlockVector *DivergentRegion::getBlocks() {
  return Blocks;
}

//------------------------------------------------------------------------------
void DivergentRegion::FillRegion(DominatorTree *DT, PostDominatorTree *PDT) {
  Blocks = ListBlocks(Bounds);

  // If H is dominated by a block in the region than recompute the bounds.
  if (IsDominated(Bounds->getHeader(), *Blocks, DT)) { 
    Bounds = FindBounds(*Blocks, DT, PDT); 
  }
}

//------------------------------------------------------------------------------
void DivergentRegion::UpdateRegion() {
  Blocks = ListBlocks(Bounds);
}

//------------------------------------------------------------------------------
void DivergentRegion::dump() {
  errs() << "Bounds: " << getHeader()->getName() << 
           " / " << getExiting()->getName() << "\n";
  errs() << "Blocks: \n"; 
  for (BlockVector::iterator I = Blocks->begin(), E = Blocks->end(); 
       I != E; ++I) {
    errs() << (*I)->getName() << "\n";
  }
  errs() << "----------\nCondition: ";
  switch(Condition) {
    case DivergentRegion::LB:
      errs() << "LOWER BOUND\n";
      break;
    case DivergentRegion::UB:
      errs() << "UPPER BOUND\n";
      break;
    case DivergentRegion::EQ:
      errs() << "EQUALS\n";
      break;
    case DivergentRegion::DATA:
      errs() << "DATA\n";
      break;
    case DivergentRegion::ND:
      errs() << "ND\n";
      break;
  }
}

//------------------------------------------------------------------------------
bool DivergentRegion::Contains(const Instruction *I) {
  const BasicBlock *BB = I->getParent();
  return Contains(BB);
}

//------------------------------------------------------------------------------
bool DivergentRegion::ContainsInternally(const Instruction *I) {
  const BasicBlock *BB = I->getParent();
  return ContainsInternally(BB);
}

//------------------------------------------------------------------------------
bool DivergentRegion::Contains(const BasicBlock *BB) {
  BlockVector::iterator Begin = Blocks->begin();
  BlockVector::iterator End = Blocks->end();
  BlockVector::iterator Result = std::find(Begin, End, BB);
  return Result != End;
}

//------------------------------------------------------------------------------
bool DivergentRegion::ContainsInternally(const BasicBlock *BB) {
  
  for (BlockVector::iterator I = Blocks->begin(), 
                             E = Blocks->end();
                             I != E; ++I) {
    BasicBlock *block = (*I);

    if(block == Bounds->getHeader() || block == Bounds->getExiting())
      continue;  
    
    if(block == BB)
      return true; 
  }
  return false;
}

//------------------------------------------------------------------------------
// Checks if the header of the region dominates all the blocks in the region.
bool DivergentRegion::PerformHeaderCheck(DominatorTree *DT) {
  return DominatesAll(Bounds->getHeader(), *Blocks, DT);
}

//------------------------------------------------------------------------------
bool DivergentRegion::IsStrict() {
  return Condition == DivergentRegion::EQ;
}

//------------------------------------------------------------------------------
void DivergentRegion::setCondition(DivergentRegion::BoundCheck Condition) {
  this->Condition = Condition;
}

//------------------------------------------------------------------------------
DivergentRegion::BoundCheck DivergentRegion::getCondition() {
  return Condition;
}

//------------------------------------------------------------------------------
void DivergentRegion::Analyze(ScalarEvolution *SE, LoopInfo *LI,
                              ValueVector &TIds, ValueVector &Inputs) {
  BasicBlock *Header = getHeader();
  if(BranchInst *Branch = dyn_cast<BranchInst>(Header->getTerminator())) {
    if(DependsOn(Branch, Inputs)) {
      setCondition(DivergentRegion::DATA);
      return;
    }
    Value *Cond = Branch->getCondition();
    if(CmpInst* Cmp = dyn_cast<CmpInst>(Cond))
      setCondition(AnalyzeCmp(SE, LI, Cmp, TIds));
    else
      setCondition(DivergentRegion::ND);
  } else {
    setCondition(DivergentRegion::ND);
  }
}

//------------------------------------------------------------------------------
DivergentRegion::BoundCheck 
  DivergentRegion::AnalyzeCmp(ScalarEvolution *SE, LoopInfo *LI,
                              CmpInst *Cmp, ValueVector &TIds) {
  if(IsEquals(Cmp->getPredicate()))
    return DivergentRegion::EQ;

  Value *TIdOp = GetTIdOperand(Cmp, TIds);
  if(TIdOp == NULL)
    return DivergentRegion::ND;

  // Get the operand position.
  unsigned int position = GetOperandPosition(Cmp, TIdOp);
  bool isFirst = (position == 0);
  // Get the comparison sign.
  bool GT = IsGreaterThan(Cmp->getPredicate());

  if(Loop *L = LI->getLoopFor(getHeader())) {
    if(PHINode *Phi = dyn_cast<PHINode>(TIdOp)) { 
      BasicBlock *Latch = L->getLoopLatch();
      TIdOp = Phi->getIncomingValueForBlock(Latch); 
    }
  }
  
  // Check is the value is SCEVable.
  if(!SE->isSCEVable(TIdOp->getType()))
    return DivergentRegion::ND;    

  // Get the TID subscript sign.
  SmallPtrSet<const SCEV*, 8> Processed;
  const SCEV *Scev = SE->getSCEV(TIdOp);
  unsigned int result = AnalyzeSubscript(SE, Scev, TIds, Processed);
  if (result == 0)
    return DivergentRegion::ND;
  bool IsTIdPositive = (result == 1);

  // Compare all of the previous.
  unsigned int sum = isFirst + GT + IsTIdPositive;
  
  if(sum == 0)
    return DivergentRegion::UB; 

  if(sum % 2 == 0)
    return DivergentRegion::UB;
  else
    return DivergentRegion::LB;
}

//------------------------------------------------------------------------------
// This could be done using "accumulate".
unsigned int DivergentRegion::size() {
  unsigned int result = 0;

  for (BlockVector::iterator I = Blocks->begin(), 
                             E = Blocks->end(); 
                             I != E; ++I) {
    BasicBlock *block = (*I);
    if(block == Bounds->getHeader() || block == Bounds->getExiting())
      continue;

    result += block->size();
  }

  return result;
}
