#ifndef DIVERGENT_REGION_H
#define DIVERGENT_REGION_H

#include "thrud/Support/DataTypes.h"
#include "thrud/Support/RegionBounds.h"

#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"

namespace llvm {
class CmpInst;
class LoopInfo;
class ScalarEvolution;
}

class DivergentRegion {
public:
  enum BoundCheck {
//    LB,
//    UB,
//    DATA,
    EQ,
    ND
  };

public:
  DivergentRegion(BasicBlock *header, BasicBlock *exiting);
  DivergentRegion(RegionBounds &bounds);

  // Getter and Setter.
  BasicBlock *getHeader();
  BasicBlock *getExiting();

  const BasicBlock *getHeader() const;
  const BasicBlock *getExiting() const;

  RegionBounds &getBounds();
  BlockVector &getBlocks();

  void setHeader(BasicBlock *Header);
  void setExiting(BasicBlock *Exiting);

  void setCondition(BoundCheck condition);
  BoundCheck getCondition() const;

  void analyze();
  bool IsStrict();

  // Region filling.
  void fillRegion(DominatorTree *dt, PostDominatorTree *pdt);
  void updateRegion();

  bool PerformHeaderCheck(DominatorTree *DT);
  
  unsigned int size();
  void dump();

private:
  RegionBounds bounds;
  BlockVector blocks;
  BoundCheck condition;

public:
  // Iterator class.
  //-----------------------------------------------------------------------------
  class iterator
      : public std::iterator<std::forward_iterator_tag, BasicBlock *> {
  public:
    iterator();
    iterator(const DivergentRegion &region);
    iterator(const iterator &original);

  private:
    BlockVector blocks;
    size_t currentBlock;

  public:
    // Pre-increment.
    iterator &operator++();
    // Post-increment.
    iterator operator++(int);
    BasicBlock *operator*() const;
    bool operator!=(const iterator &iter) const;
    bool operator==(const iterator &iter) const;

    static iterator end();

  private:
    void toNext();
  };

  DivergentRegion::iterator begin();
  DivergentRegion::iterator end();

  // Const iterator class.
  //-----------------------------------------------------------------------------
  class const_iterator
      : public std::iterator<std::forward_iterator_tag, BasicBlock *> {
  public:
    const_iterator();
    const_iterator(const DivergentRegion &region);
    const_iterator(const const_iterator &original);

  private:
    BlockVector blocks;
    size_t currentBlock;

  public:
    // Pre-increment.
    const_iterator &operator++();
    // Post-increment.
    const_iterator operator++(int);
    const BasicBlock *operator*() const;
    bool operator!=(const const_iterator &iter) const;
    bool operator==(const const_iterator &iter) const;

    static const_iterator end();

  private:
    void toNext();
  };

  DivergentRegion::const_iterator begin() const;
  DivergentRegion::const_iterator end() const;
};

typedef std::vector<DivergentRegion *> RegionVector;

// Non member functions.
// -----------------------------------------------------------------------------
RegionBounds *getExtingExit(DivergentRegion *region);
BasicBlock *getPredecessor(DivergentRegion *region, LoopInfo *loopInfo);
RegionBounds cloneRegion(RegionBounds &bounds, const Twine &suffix,
                         DominatorTree *dt);
bool contains(const DivergentRegion &region, const Instruction *inst);
bool containsInternally(const DivergentRegion &region, const Instruction *inst);
bool contains(const DivergentRegion &region, const BasicBlock *block);
bool containsInternally(const DivergentRegion &region, const BasicBlock *block);

#endif
