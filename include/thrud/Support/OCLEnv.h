#ifndef OPENCL_ENVIRONMENT_H
#define OPENCL_ENVIRONMENT_H

#include "thrud/Support/NDRange.h"

#include "thrud/Support/NDRangeSpace.h"

#include <map>

namespace llvm {
  class Function;
}

using namespace llvm;

class OCLEnv {

public:
  static const int WARP_SIZE;
  static const int CACHELINE_SIZE;
  static const int UNKNOWN_MEMORY_LOCATION;
  static unsigned const int LOCAL_AS;

public:
  OCLEnv(Function &function, const NDRange *ndRange, const NDRangeSpace &ndRangeSpace);
 
public:
  const NDRange* getNDRange() const;
  const NDRangeSpace& getNDRangeSpace() const;
  int resolveValue(llvm::Value*) const; 

private:
  void setup(Function &function);

private:
  const NDRange *ndRange;
  NDRangeSpace ndRangeSpace;
  std::map<llvm::Value*, int> argumentMap;

};

#endif
