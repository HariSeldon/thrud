#include "thrud/Support/OpenCLEnvironment.h"

#include "llvm/IR/Type.h"

#include "llvm/IR/Function.h"

OpenCLEnvironment::OpenCLEnvironment(NDRange *ndRange) : ndRange(ndRange) {}

void OpenCLEnvironment::setup(Function &function) {
  // Go through the function arguements and setup the map.  
  for(Function::arg_iterator iter = function.arg_begin(), iterEnd = function.arg_end(); iter != iterEnd; ++iter) {
    llvm::Value *argument = iter;
    llvm::Type *type = argument->getType();
    // Only set the value of the argument if it is an integer.
    if(type->isIntegerTy()) {
      // FIXME!
      argumentMap.insert(std::pair<llvm::Value*, int>(argument, 1024));
    }
  } 
}

const NDRange *OpenCLEnvironment::getNDRange() const {
  return ndRange;  
}

std::map<llvm::Value*, int>& OpenCLEnvironment::getMap() {
  return argumentMap;
}
