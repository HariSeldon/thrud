#ifndef MLP_COMPUTATION
#define MLP_COMPUTATION

#include <utility>

#include "llvm/Analysis/PostDominators.h"

namespace llvm { class BasicBlock; }

using namespace llvm;

float getMLP(BasicBlock *block, DominatorTree *DT, PostDominatorTree *PDT);

#endif
