#ifndef OPENCL_ENVIRONMENT_H
#define OPENCL_ENVIRONMENT_H

#include "thrud/Support/NDRange.h"

#include <map>

namespace llvm {
  class Function;
}

using namespace llvm;

class OpenCLEnvironment {
public:
  OpenCLEnvironment(NDRange *ndRange);
 
public:
  void setup(Function &function);
  const NDRange* getNDRange() const;
  std::map<llvm::Value*, int>& getMap(); 

private:
  NDRange* ndRange;
  std::map<llvm::Value*, int> argumentMap;

};

#endif
