#ifndef THREAD_COARSENING_H
#define THREAD_COARSENING_H

#include "thrud/DivergenceAnalysis/SingleDimDivAnalysis.h"

#include "thrud/Support/DataTypes.h"

#include "llvm/Pass.h"

#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/RegionInfo.h"

using namespace llvm;

namespace llvm { class BasicBlock; }

class ThreadCoarsening : public FunctionPass {
  void operator=(const ThreadCoarsening &);   // Do not implement.
  ThreadCoarsening(const ThreadCoarsening &); // Do not implement.

public:
  static char ID;
  ThreadCoarsening();

  virtual bool runOnFunction(Function &F);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;

private:
  void DuplicateInsts(InstVector &Insts, InstPairs &IP, Map &map,
                      unsigned int CI);
  void RenameInstructionWithIndex(Instruction *I, StringRef oldName,
                                  unsigned int index);

  InstVector createOffsetInsts(Value *tId, unsigned int CoarseningFactor,
                               unsigned int index);

  InstVector ScaleSizeAndIds(unsigned int CD, unsigned int CF, unsigned int ST,
                             InstVector &InstsTid);
  void InsertSizeScale(unsigned int CD, unsigned int CF, unsigned int ST);
  InstVector InsertIdOffset(unsigned int CD, unsigned int CF, unsigned int ST,
                            InstVector &InstsTid);

  void InsertReplicatedInst(InstPairs &IP, Map &map);

  void PerformDuplication();

  void BuildPhiNodeMap(BasicBlock *OldBlock, BasicBlock *NewBlock, Map &map);

private:
  PostDominatorTree *PDT;
  DominatorTree *DT;
  RegionInfo *RI;
  SingleDimDivAnalysis *SDDA;
  LoopInfo *LI;
};

#endif
