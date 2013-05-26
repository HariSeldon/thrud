#include "llvm/Support/CommandLine.h"

using namespace llvm;

cl::opt<int>
CoarseningFactor("coarsening-factor", cl::init(2), cl::Hidden,
                 cl::desc("The coarsening factor"));

cl::opt<int> 
CoarseningDirection("coarsening-direction", cl::init(-1), cl::Hidden, 
                    cl::desc("The coarsening direction"));

cl::opt<int>
Stride("coarsening-stride", cl::init(1), cl::Hidden,
                    cl::desc("The coarsening stride"));

cl::opt<std::string>
KernelName("kernel-name", cl::init(""), cl::Hidden,
            cl::desc("Name of the kernel to coarsen"));

cl::opt<std::string>
kernelName("count-kernel-name", cl::init(""), cl::Hidden,
           cl::desc("Name of the kernel to analyze"));
