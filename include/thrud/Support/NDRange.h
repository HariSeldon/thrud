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

  virtual bool runOnFunction(Function &F);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;

public:
  InstVector getTids();
  InstVector getSizes();
  InstVector getTids(unsigned int direction);
  InstVector getSizes(unsigned int direction);

  bool IsTid(Instruction *I);
  bool IsTidInDirection(Instruction *I, unsigned int direction);

  bool IsGlobal(llvm::Instruction *I);
  bool IsLocal(llvm::Instruction *I);
  bool IsGlobalSize(llvm::Instruction *I);
  bool IsLocalSize(llvm::Instruction *I);
  bool IsGroupId(Instruction *I);
  
  bool IsGlobal(llvm::Instruction *I, int direction);
  bool IsLocal(llvm::Instruction *I, int direction);
  bool IsGlobalSize(llvm::Instruction *I, int direction);
  bool IsLocalSize(llvm::Instruction *I, int direction);
  bool IsGroupId(Instruction *I, int direction);

  void dump();

public:
  static std::string GET_GLOBAL_ID;
  static std::string GET_LOCAL_ID;
  static std::string GET_GLOBAL_SIZE;
  static std::string GET_LOCAL_SIZE;
  static std::string GET_GROUP_ID;
  static unsigned int DIRECTION_NUMBER;

private:
  void Init();
  void ParseFunction(llvm::Function *F);
  bool IsPresentInDirection(llvm::Instruction *I,
                            const std::string &FuncName,
                            int Dir);
  void FindOpenCLFunctionCallsByNameAllDirs(std::string CalleeName, Function *Caller);


private:
  llvm::Function *kernel;
  std::vector<std::map<std::string, InstVector> > OCLInsts;
};

// Non-member functions.
void FindOpenCLFunctionCallsByName(std::string calleeName,
                                   Function *caller, int dimension, InstVector& Target);
void FindOpenCLFunctionCalls(Function *callee, Function *caller,
                                   int dimension, InstVector& Target);

#endif
