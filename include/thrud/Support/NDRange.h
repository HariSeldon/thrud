#ifndef NDRANGE_H
#define NDRANGE_H

#include "thrud/Support/Utils.h"

#include "llvm/Pass.h"

using namespace llvm;

namespace llvm {
class Function;
}

class NDRange : public FunctionPass {
  void operator=(const NDRange &);
  NDRange(const NDRange &);
    
public:
  static char ID;
  NDRange();

  virtual bool runOnFunction(Function &function);
  virtual void getAnalysisUsage(AnalysisUsage &au) const;

public:
  InstVector getTids();
  InstVector getSizes();
  InstVector getTids(unsigned int direction);
  InstVector getSizes(unsigned int direction);

  bool isTid(Instruction *inst);
  bool isTidInDirection(Instruction *inst, unsigned int direction);
  std::string getType(Instruction *inst) const;
  unsigned int getDirection(Instruction *inst) const;

  bool isGlobal(Instruction *inst) const;
  bool isLocal(Instruction *inst) const;
  bool isGlobalSize(Instruction *inst) const;
  bool isLocalSize(Instruction *inst) const;
  bool isGroupId(Instruction *inst) const;
  bool isGroupsNum(Instruction *inst) const;
  
  bool isGlobal(Instruction *inst, unsigned int direction) const;
  bool isLocal(Instruction *inst, unsigned int direction) const;
  bool isGlobalSize(Instruction *inst, unsigned int direction) const;
  bool isLocalSize(Instruction *inst, unsigned int direction) const;
  bool isGroupId(Instruction *inst, unsigned int direction) const;
  bool isGroupsNum(Instruction *inst, unsigned int dimension) const;

  void dump();

public:
  static std::string GET_GLOBAL_ID;
  static std::string GET_LOCAL_ID;
  static std::string GET_GLOBAL_SIZE;
  static std::string GET_LOCAL_SIZE;
  static std::string GET_GROUP_ID;
  static std::string GET_GROUPS_NUMBER;
  static unsigned int DIRECTION_NUMBER;

private:
  void init();
  bool isPresentInDirection(Instruction *inst,
                            const std::string &functionName,
                            unsigned int direction) const;
  void findOpenCLFunctionCallsByNameAllDirs(std::string calleeName, Function *caller);


private:
  std::vector<std::map<std::string, InstVector> > oclInsts;
};

// Non-member functions.
void findOpenCLFunctionCallsByName(std::string calleeName, Function *caller,
                                   unsigned int dimension, InstVector &target);
void findOpenCLFunctionCalls(Function *callee, Function *caller,
                             unsigned int dimension, InstVector &target);

#endif
