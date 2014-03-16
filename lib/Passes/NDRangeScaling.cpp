#include "thrud/ThreadCoarsening/ThreadCoarsening.h"

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/Utils.h"

//------------------------------------------------------------------------------
void ThreadCoarsening::scaleNDRange() {
  InstVector InstTids;
  scaleSizes();
  scaleIds();
}

//------------------------------------------------------------------------------
void ThreadCoarsening::scaleSizes() {
  InstVector sizeInsts = ndr->getSizes(direction);
  for (InstVector::iterator iter = sizeInsts.begin(), iterEnd = sizeInsts.end();
       iter != iterEnd; ++iter) {
    // Scale size.
    Instruction *inst = *iter;
    Instruction *mul = getMulInst(inst, factor);
    mul->insertAfter(inst);
    // Replace uses of the old size with the scaled one.
    replaceUses(inst, mul);
  }
}

//------------------------------------------------------------------------------
void ThreadCoarsening::scaleIds() {
  unsigned int logST = log2(stride);
  unsigned int cfst = factor * stride;
  unsigned int st1 = stride - 1;

  InstVector tids = ndr->getTids(direction);
  for (InstVector::iterator instIter = tids.begin(), instEnd = tids.end();
       instIter != instEnd; ++instIter) {
    Instruction *inst = *instIter;

    Instruction *shift = getShiftInst(inst, logST);
    shift->insertAfter(inst);
    Instruction *mul = getMulInst(shift, cfst);
    mul->insertAfter(shift);
    Instruction *andOp = getAndInst(inst, st1);
    andOp->insertAfter(mul);
    Instruction *base = getAddInst(andOp, mul);
    base->insertAfter(andOp);

    //    Instruction* Mul = getMulInst(*I, CF);
    //    Mul->insertAfter(*I);
    cMap.insert(std::pair<Instruction*, InstVector>(inst, InstVector()));
    InstVector &current = cMap[inst];
    current.reserve(factor - 1);

    Instruction *bookmark = base;
    for (unsigned int index = 1; index < factor; ++index) {
      Instruction *add = getAddInst(base, (index - 1) * stride);
      add->insertAfter(bookmark);
      current.push_back(add);
      bookmark = add; 
    }
  }
}
