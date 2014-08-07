// The pass implemented in this file transforms the programs control flow
// graph into a form that's suitable for code generation on hardware that
// implements control flow by execution masking. This currently includes all
// AMD GPUs but may as well be useful for other types of hardware.

#include "thrud/Support/StructurizeCFG.h"

#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/RegionIterator.h"
#include "llvm/Analysis/RegionPass.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/PatternMatch.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"

using namespace llvm;
using namespace llvm::PatternMatch;

// The name for newly created blocks.

static const char *FlowBlockName = "Flow";

// -----------------------------------------------------------------------------
// Find the nearest common dominator for multiple BasicBlocks
//
// Helper class for StructurizeCFG
class NearestCommonDominator {

  DominatorTree *DT;

  DTN2UnsignedMap IndexMap;

  BasicBlock *Result;
  unsigned ResultIndex;
  bool ExplicitMentioned;

public:
  /// \brief Start a new query
  NearestCommonDominator(DominatorTree *DomTree) {
    DT = DomTree;
    Result = 0;
  }

  /// \brief Add BB to the resulting dominator
  void addBlock(BasicBlock *BB, bool Remember = true) {

    DomTreeNode *Node = DT->getNode(BB);

    if (Result == 0) {
      unsigned Numbering = 0;
      for (;Node;Node = Node->getIDom())
        IndexMap[Node] = ++Numbering;
      Result = BB;
      ResultIndex = 1;
      ExplicitMentioned = Remember;
      return;
    }

    for (;Node;Node = Node->getIDom())
      if (IndexMap.count(Node))
        break;
      else
        IndexMap[Node] = 0;

    assert(Node && "Dominator tree invalid!");

    unsigned Numbering = IndexMap[Node];
    if (Numbering > ResultIndex) {
      Result = Node->getBlock();
      ResultIndex = Numbering;
      ExplicitMentioned = Remember && (Result == BB);
    } else if (Numbering == ResultIndex) {
      ExplicitMentioned |= Remember;
    }
  }

  /// \brief Is "Result" one of the BBs added with "Remember" = True?
  bool wasResultExplicitMentioned() {
    return ExplicitMentioned;
  }

  /// \brief Get the query result
  BasicBlock *getResult() {
    return Result;
  }
};

// -----------------------------------------------------------------------------
StructurizeCFG::StructurizeCFG() : RegionPass(ID) {}

//------------------------------------------------------------------------------
void StructurizeCFG::getAnalysisUsage(AnalysisUsage &au) const {
  au.addRequired<DominatorTree>();
  au.addPreserved<DominatorTree>();
  RegionPass::getAnalysisUsage(au);
}

//------------------------------------------------------------------------------
bool StructurizeCFG::doInitialization(Region *R, RGPassManager &RGM) {
  LLVMContext &Context = R->getEntry()->getContext();

  Boolean = Type::getInt1Ty(Context);
  BoolTrue = ConstantInt::getTrue(Context);
  BoolFalse = ConstantInt::getFalse(Context);
  BoolUndef = UndefValue::get(Boolean);

  return false;
}

//------------------------------------------------------------------------------
/// \brief Build up the general order of nodes
void StructurizeCFG::orderNodes() {
  scc_iterator<Region *> I = scc_begin(ParentRegion),
                         E = scc_end(ParentRegion);
  for (Order.clear(); I != E; ++I) {
    std::vector<RegionNode *> &Nodes = *I;
    Order.append(Nodes.begin(), Nodes.end());
  }
}

//------------------------------------------------------------------------------
/// \brief Determine the end of the loops
void StructurizeCFG::analyzeLoops(RegionNode *N) {

  if (N->isSubRegion()) {
    // Test for exit as back edge
    BasicBlock *Exit = N->getNodeAs<Region>()->getExit();
    if (Visited.count(Exit))
      Loops[Exit] = N->getEntry();

  } else {
    // Test for sucessors as back edge
    BasicBlock *BB = N->getNodeAs<BasicBlock>();
    BranchInst *Term = cast<BranchInst>(BB->getTerminator());

    for (unsigned i = 0, e = Term->getNumSuccessors(); i != e; ++i) {
      BasicBlock *Succ = Term->getSuccessor(i);

      if (Visited.count(Succ))
        Loops[Succ] = BB;
    }
  }
}

/// \brief Invert the given condition
Value *StructurizeCFG::invert(Value *Condition) {

  // First: Check if it's a constant
  if (Condition == BoolTrue)
    return BoolFalse;

  if (Condition == BoolFalse)
    return BoolTrue;

  if (Condition == BoolUndef)
    return BoolUndef;

  // Second: If the condition is already inverted, return the original value
  if (match(Condition, m_Not(m_Value(Condition))))
    return Condition;

  // Third: Check all the users for an invert
  BasicBlock *Parent = cast<Instruction>(Condition)->getParent();
  for (Value::use_iterator I = Condition->use_begin(),
       E = Condition->use_end(); I != E; ++I) {

    Instruction *User = dyn_cast<Instruction>(*I);
    if (!User || User->getParent() != Parent)
      continue;

    if (match(*I, m_Not(m_Specific(Condition))))
      return *I;
  }

  // Last option: Create a new instruction
  return BinaryOperator::CreateNot(Condition, "", Parent->getTerminator());
}

/// \brief Build the condition for one edge
Value *StructurizeCFG::buildCondition(BranchInst *Term, unsigned Idx,
                                            bool Invert) {
  Value *Cond = Invert ? BoolFalse : BoolTrue;
  if (Term->isConditional()) {
    Cond = Term->getCondition();

    if (Idx != Invert)
      Cond = invert(Cond);
  }
  return Cond;
}

/// \brief Analyze the predecessors of each block and build up predicates
void StructurizeCFG::gatherPredicates(RegionNode *N) {

  RegionInfo *RI = ParentRegion->getRegionInfo();
  BasicBlock *BB = N->getEntry();
  BBPredicates &Pred = Predicates[BB];
  BBPredicates &LPred = LoopPreds[BB];

  for (pred_iterator PI = pred_begin(BB), PE = pred_end(BB);
       PI != PE; ++PI) {

    // Ignore it if it's a branch from outside into our region entry
    if (!ParentRegion->contains(*PI))
      continue;

    Region *R = RI->getRegionFor(*PI);
    if (R == ParentRegion) {

      // It's a top level block in our region
      BranchInst *Term = cast<BranchInst>((*PI)->getTerminator());
      for (unsigned i = 0, e = Term->getNumSuccessors(); i != e; ++i) {
        BasicBlock *Succ = Term->getSuccessor(i);
        if (Succ != BB)
          continue;

        if (Visited.count(*PI)) {
          // Normal forward edge
          if (Term->isConditional()) {
            // Try to treat it like an ELSE block
            BasicBlock *Other = Term->getSuccessor(!i);
            if (Visited.count(Other) && !Loops.count(Other) &&
                !Pred.count(Other) && !Pred.count(*PI)) {

              Pred[Other] = BoolFalse;
              Pred[*PI] = BoolTrue;
              continue;
            }
          }
          Pred[*PI] = buildCondition(Term, i, false);
 
        } else {
          // Back edge
          LPred[*PI] = buildCondition(Term, i, true);
        }
      }

    } else {

      // It's an exit from a sub region
      while(R->getParent() != ParentRegion)
        R = R->getParent();

      // Edge from inside a subregion to its entry, ignore it
      if (R == N)
        continue;

      BasicBlock *Entry = R->getEntry();
      if (Visited.count(Entry))
        Pred[Entry] = BoolTrue;
      else
        LPred[Entry] = BoolFalse;
    }
  }
}

/// \brief Collect various loop and predicate infos
void StructurizeCFG::collectInfos() {

  // Reset predicate
  Predicates.clear();

  // and loop infos
  Loops.clear();
  LoopPreds.clear();

  // Reset the visited nodes
  Visited.clear();

  for (RNVector::reverse_iterator OI = Order.rbegin(), OE = Order.rend();
       OI != OE; ++OI) {

    // Analyze all the conditions leading to a node
    gatherPredicates(*OI);

    // Remember that we've seen this node
    Visited.insert((*OI)->getEntry());

    // Find the last back edges
    analyzeLoops(*OI);
  }
}

/// \brief Insert the missing branch conditions
void StructurizeCFG::insertConditions(bool Loops) {
  BranchVector &Conds = Loops ? LoopConds : Conditions;
  Value *Default = Loops ? BoolTrue : BoolFalse;
  SSAUpdater PhiInserter;

  for (BranchVector::iterator I = Conds.begin(),
       E = Conds.end(); I != E; ++I) {

    BranchInst *Term = *I;
    assert(Term->isConditional());

    BasicBlock *Parent = Term->getParent();
    BasicBlock *SuccTrue = Term->getSuccessor(0);
    BasicBlock *SuccFalse = Term->getSuccessor(1);

    PhiInserter.Initialize(Boolean, "");
    PhiInserter.AddAvailableValue(&Func->getEntryBlock(), Default);
    PhiInserter.AddAvailableValue(Loops ? SuccFalse : Parent, Default);

    BBPredicates &Preds = Loops ? LoopPreds[SuccFalse] : Predicates[SuccTrue];

    NearestCommonDominator Dominator(DT);
    Dominator.addBlock(Parent, false);

    Value *ParentValue = 0;
    for (BBPredicates::iterator PI = Preds.begin(), PE = Preds.end();
         PI != PE; ++PI) {

      if (PI->first == Parent) {
        ParentValue = PI->second;
        break;
      }
      PhiInserter.AddAvailableValue(PI->first, PI->second);
      Dominator.addBlock(PI->first);
    }

    if (ParentValue) {
      Term->setCondition(ParentValue);
    } else {
      if (!Dominator.wasResultExplicitMentioned())
        PhiInserter.AddAvailableValue(Dominator.getResult(), Default);

      Term->setCondition(PhiInserter.GetValueInMiddleOfBlock(Parent));
    }
  }
}

/// \brief Remove all PHI values coming from "From" into "To" and remember
/// them in DeletedPhis
void StructurizeCFG::delPhiValues(BasicBlock *From, BasicBlock *To) {
  PhiMap &Map = DeletedPhis[To];
  for (BasicBlock::iterator I = To->begin(), E = To->end();
       I != E && isa<PHINode>(*I);) {

    PHINode &Phi = cast<PHINode>(*I++);
    while (Phi.getBasicBlockIndex(From) != -1) {
      Value *Deleted = Phi.removeIncomingValue(From, false);
      Map[&Phi].push_back(std::make_pair(From, Deleted));
    }
  }
}

/// \brief Add a dummy PHI value as soon as we knew the new predecessor
void StructurizeCFG::addPhiValues(BasicBlock *From, BasicBlock *To) {
  for (BasicBlock::iterator I = To->begin(), E = To->end();
       I != E && isa<PHINode>(*I);) {

    PHINode &Phi = cast<PHINode>(*I++);
    Value *Undef = UndefValue::get(Phi.getType());
    Phi.addIncoming(Undef, From);
  }
  AddedPhis[To].push_back(From);
}

/// \brief Add the real PHI value as soon as everything is set up
void StructurizeCFG::setPhiValues() {

  SSAUpdater Updater;
  for (BB2BBVecMap::iterator AI = AddedPhis.begin(), AE = AddedPhis.end();
       AI != AE; ++AI) {

    BasicBlock *To = AI->first;
    BBVector &From = AI->second;

    if (!DeletedPhis.count(To))
      continue;

    PhiMap &Map = DeletedPhis[To];
    for (PhiMap::iterator PI = Map.begin(), PE = Map.end();
         PI != PE; ++PI) {

      PHINode *Phi = PI->first;
      Value *Undef = UndefValue::get(Phi->getType());
      Updater.Initialize(Phi->getType(), "");
      Updater.AddAvailableValue(&Func->getEntryBlock(), Undef);
      Updater.AddAvailableValue(To, Undef);

      NearestCommonDominator Dominator(DT);
      Dominator.addBlock(To, false);
      for (BBValueVector::iterator VI = PI->second.begin(),
           VE = PI->second.end(); VI != VE; ++VI) {

        Updater.AddAvailableValue(VI->first, VI->second);
        Dominator.addBlock(VI->first);
      }

      if (!Dominator.wasResultExplicitMentioned())
        Updater.AddAvailableValue(Dominator.getResult(), Undef);

      for (BBVector::iterator FI = From.begin(), FE = From.end();
           FI != FE; ++FI) {

        int Idx = Phi->getBasicBlockIndex(*FI);
        assert(Idx != -1);
        Phi->setIncomingValue(Idx, Updater.GetValueAtEndOfBlock(*FI));
      }
    }

    DeletedPhis.erase(To);
  }
  assert(DeletedPhis.empty());
}

/// \brief Remove phi values from all successors and then remove the terminator.
void StructurizeCFG::killTerminator(BasicBlock *BB) {
  TerminatorInst *Term = BB->getTerminator();
  if (!Term)
    return;

  for (succ_iterator SI = succ_begin(BB), SE = succ_end(BB);
       SI != SE; ++SI) {

    delPhiValues(BB, *SI);
  }

  Term->eraseFromParent();
}

/// \brief Let node exit(s) point to NewExit
void StructurizeCFG::changeExit(RegionNode *Node, BasicBlock *NewExit,
                                      bool IncludeDominator) {

  if (Node->isSubRegion()) {
    Region *SubRegion = Node->getNodeAs<Region>();
    BasicBlock *OldExit = SubRegion->getExit();
    BasicBlock *Dominator = 0;

    // Find all the edges from the sub region to the exit
    for (pred_iterator I = pred_begin(OldExit), E = pred_end(OldExit);
         I != E;) {

      BasicBlock *BB = *I++;
      if (!SubRegion->contains(BB))
        continue;

      // Modify the edges to point to the new exit
      delPhiValues(BB, OldExit);
      BB->getTerminator()->replaceUsesOfWith(OldExit, NewExit);
      addPhiValues(BB, NewExit);

      // Find the new dominator (if requested)
      if (IncludeDominator) {
        if (!Dominator)
          Dominator = BB;
        else
          Dominator = DT->findNearestCommonDominator(Dominator, BB);
      }
    }

    // Change the dominator (if requested)
    if (Dominator)
      DT->changeImmediateDominator(NewExit, Dominator);

    // Update the region info
    SubRegion->replaceExit(NewExit);

  } else {
    BasicBlock *BB = Node->getNodeAs<BasicBlock>();
    killTerminator(BB);
    BranchInst::Create(NewExit, BB);
    addPhiValues(BB, NewExit);
    if (IncludeDominator)
      DT->changeImmediateDominator(NewExit, BB);
  }
}

/// \brief Create a new flow node and update dominator tree and region info
BasicBlock *StructurizeCFG::getNextFlow(BasicBlock *Dominator) {
  LLVMContext &Context = Func->getContext();
  BasicBlock *Insert = Order.empty() ? ParentRegion->getExit() :
                       Order.back()->getEntry();
  BasicBlock *Flow = BasicBlock::Create(Context, FlowBlockName,
                                        Func, Insert);
  DT->addNewBlock(Flow, Dominator);
  ParentRegion->getRegionInfo()->setRegionFor(Flow, ParentRegion);
  return Flow;
}

/// \brief Create a new or reuse the previous node as flow node
BasicBlock *StructurizeCFG::needPrefix(bool NeedEmpty) {

  BasicBlock *Entry = PrevNode->getEntry();

  if (!PrevNode->isSubRegion()) {
    killTerminator(Entry);
    if (!NeedEmpty || Entry->getFirstInsertionPt() == Entry->end())
      return Entry;

  } 

  // create a new flow node
  BasicBlock *Flow = getNextFlow(Entry);

  // and wire it up
  changeExit(PrevNode, Flow, true);
  PrevNode = ParentRegion->getBBNode(Flow);
  return Flow;
}

/// \brief Returns the region exit if possible, otherwise just a new flow node
BasicBlock *StructurizeCFG::needPostfix(BasicBlock *Flow,
                                              bool ExitUseAllowed) {

  if (Order.empty() && ExitUseAllowed) {
    BasicBlock *Exit = ParentRegion->getExit();
    DT->changeImmediateDominator(Exit, Flow);
    addPhiValues(Flow, Exit);
    return Exit;
  }
  return getNextFlow(Flow);
}

/// \brief Set the previous node
void StructurizeCFG::setPrevNode(BasicBlock *BB) {
  PrevNode =  ParentRegion->contains(BB) ? ParentRegion->getBBNode(BB) : 0;
}

/// \brief Does BB dominate all the predicates of Node ?
bool StructurizeCFG::dominatesPredicates(BasicBlock *BB, RegionNode *Node) {
  BBPredicates &Preds = Predicates[Node->getEntry()];
  for (BBPredicates::iterator PI = Preds.begin(), PE = Preds.end();
       PI != PE; ++PI) {

    if (!DT->dominates(BB, PI->first))
      return false;
  }
  return true;
}

/// \brief Can we predict that this node will always be called?
bool StructurizeCFG::isPredictableTrue(RegionNode *Node) {

  BBPredicates &Preds = Predicates[Node->getEntry()];
  bool Dominated = false;

  // Regionentry is always true
  if (PrevNode == 0)
    return true;

  for (BBPredicates::iterator I = Preds.begin(), E = Preds.end();
       I != E; ++I) {

    if (I->second != BoolTrue)
      return false;

    if (!Dominated && DT->dominates(I->first, PrevNode->getEntry()))
      Dominated = true;
  }

  // TODO: The dominator check is too strict
  return Dominated;
}

/// Take one node from the order vector and wire it up
void StructurizeCFG::wireFlow(bool ExitUseAllowed,
                                    BasicBlock *LoopEnd) {

  RegionNode *Node = Order.pop_back_val();
  Visited.insert(Node->getEntry());

  if (isPredictableTrue(Node)) {
    // Just a linear flow
    if (PrevNode) {
      changeExit(PrevNode, Node->getEntry(), true);
    }
    PrevNode = Node;

  } else {
    // Insert extra prefix node (or reuse last one)
    BasicBlock *Flow = needPrefix(false);

    // Insert extra postfix node (or use exit instead)
    BasicBlock *Entry = Node->getEntry();
    BasicBlock *Next = needPostfix(Flow, ExitUseAllowed);

    // let it point to entry and next block
    Conditions.push_back(BranchInst::Create(Entry, Next, BoolUndef, Flow));
    addPhiValues(Flow, Entry);
    DT->changeImmediateDominator(Entry, Flow);

    PrevNode = Node;
    while (!Order.empty() && !Visited.count(LoopEnd) &&
           dominatesPredicates(Entry, Order.back())) {
      handleLoops(false, LoopEnd);
    }

    changeExit(PrevNode, Next, false);
    setPrevNode(Next);
  }
}

void StructurizeCFG::handleLoops(bool ExitUseAllowed,
                                       BasicBlock *LoopEnd) {
  RegionNode *Node = Order.back();
  BasicBlock *LoopStart = Node->getEntry();

  if (!Loops.count(LoopStart)) {
    wireFlow(ExitUseAllowed, LoopEnd);
    return;
  }

  if (!isPredictableTrue(Node))
    LoopStart = needPrefix(true);

  LoopEnd = Loops[Node->getEntry()];
  wireFlow(false, LoopEnd);
  while (!Visited.count(LoopEnd)) {
    handleLoops(false, LoopEnd);
  }

  // Create an extra loop end node
  LoopEnd = needPrefix(false);
  BasicBlock *Next = needPostfix(LoopEnd, ExitUseAllowed);
  LoopConds.push_back(BranchInst::Create(Next, LoopStart,
                                         BoolUndef, LoopEnd));
  addPhiValues(LoopEnd, LoopStart);
  setPrevNode(Next);
}

/// After this function control flow looks like it should be, but
/// branches and PHI nodes only have undefined conditions.
void StructurizeCFG::createFlow() {

  BasicBlock *Exit = ParentRegion->getExit();
  bool EntryDominatesExit = DT->dominates(ParentRegion->getEntry(), Exit);

  DeletedPhis.clear();
  AddedPhis.clear();
  Conditions.clear();
  LoopConds.clear();

  PrevNode = 0;
  Visited.clear();

  while (!Order.empty()) {
    handleLoops(EntryDominatesExit, 0);
  }

  if (PrevNode)
    changeExit(PrevNode, Exit, EntryDominatesExit);
  else
    assert(EntryDominatesExit);
}

/// Handle a rare case where the disintegrated nodes instructions
/// no longer dominate all their uses. Not sure if this is really nessasary
void StructurizeCFG::rebuildSSA() {
  SSAUpdater Updater;
  for (Region::block_iterator I = ParentRegion->block_begin(),
                              E = ParentRegion->block_end();
       I != E; ++I) {

    BasicBlock *BB = *I;
    for (BasicBlock::iterator II = BB->begin(), IE = BB->end();
         II != IE; ++II) {

      bool Initialized = false;
      for (Use *I = &II->use_begin().getUse(), *Next; I; I = Next) {

        Next = I->getNext();

        Instruction *User = cast<Instruction>(I->getUser());
        if (User->getParent() == BB) {
          continue;

        } else if (PHINode *UserPN = dyn_cast<PHINode>(User)) {
          if (UserPN->getIncomingBlock(*I) == BB)
            continue;
        }

        if (DT->dominates(II, User))
          continue;

        if (!Initialized) {
          Value *Undef = UndefValue::get(II->getType());
          Updater.Initialize(II->getType(), "");
          Updater.AddAvailableValue(&Func->getEntryBlock(), Undef);
          Updater.AddAvailableValue(BB, II);
          Initialized = true;
        }
        Updater.RewriteUseAfterInsertions(*I);
      }
    }
  }
}

/// \brief Run the transformation for each region found
bool StructurizeCFG::runOnRegion(Region *R, RGPassManager &RGM) {
  if (R->isTopLevelRegion())
    return false;

  Func = R->getEntry()->getParent();
  ParentRegion = R;

  DT = &getAnalysis<DominatorTree>();

  orderNodes();
  collectInfos();
  createFlow();
  insertConditions(false);
  insertConditions(true);
  setPhiValues();
  rebuildSSA();

  // Cleanup
  Order.clear();
  Visited.clear();
  DeletedPhis.clear();
  AddedPhis.clear();
  Predicates.clear();
  Conditions.clear();
  Loops.clear();
  LoopPreds.clear();
  LoopConds.clear();

  return true;
}

//------------------------------------------------------------------------------
char  StructurizeCFG::ID = 0;
static RegisterPass< StructurizeCFG> X("structurize-cfg", "Structurize CFG");
