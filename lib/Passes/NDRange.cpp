#include "thrud/Support/NDRange.h"

std::string NDRange::GET_GLOBAL_ID = "get_global_id";
std::string NDRange::GET_LOCAL_ID = "get_local_id";
std::string NDRange::GET_GLOBAL_SIZE = "get_global_size";
std::string NDRange::GET_LOCAL_SIZE = "get_local_size";
std::string NDRange::GET_GROUP_ID = "get_group_id";
unsigned int NDRange::DIRECTION_NUMBER = 3;

NDRange::NDRange() : FunctionPass(ID) { Init(); }

void NDRange::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}

bool NDRange::runOnFunction(Function &Func) {
  Function *F = (Function *)&Func;
  FindOpenCLFunctionCallsByNameAllDirs(GET_GLOBAL_ID, F);
  FindOpenCLFunctionCallsByNameAllDirs(GET_LOCAL_ID, F);
  FindOpenCLFunctionCallsByNameAllDirs(GET_GLOBAL_SIZE, F);
  FindOpenCLFunctionCallsByNameAllDirs(GET_LOCAL_SIZE, F);
  FindOpenCLFunctionCallsByNameAllDirs(GET_GROUP_ID, F);
  return false;
}

// -----------------------------------------------------------------------------
InstVector NDRange::getTids() {
  InstVector result;
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    std::map<std::string, InstVector> &DirInsts = OCLInsts[direction];
    InstVector globalIds = DirInsts[GET_GLOBAL_ID];
    InstVector localIds = DirInsts[GET_LOCAL_ID];
    InstVector groupIds = DirInsts[GET_GROUP_ID];
    result.insert(result.end(), globalIds.begin(), globalIds.end());
    result.insert(result.end(), localIds.begin(), localIds.end());
    result.insert(result.end(), groupIds.begin(), groupIds.end());
  }
  return result;
}

InstVector NDRange::getSizes() {
  InstVector result;
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    std::map<std::string, InstVector> &DirInsts = OCLInsts[direction];
    InstVector globalSizes = DirInsts[GET_GLOBAL_SIZE];
    InstVector localSizes = DirInsts[GET_LOCAL_SIZE];
    result.insert(result.end(), globalSizes.begin(), globalSizes.end());
    result.insert(result.end(), localSizes.begin(), localSizes.end());
  }
  return result;
}

InstVector NDRange::getTids(unsigned int direction) {
  InstVector result;
  std::map<std::string, InstVector> &DirInsts = OCLInsts[direction];
  InstVector globalIds = DirInsts[GET_GLOBAL_ID];
  InstVector localIds = DirInsts[GET_LOCAL_ID];
  InstVector groupIds = DirInsts[GET_GROUP_ID];
  result.insert(result.end(), globalIds.begin(), globalIds.end());
  result.insert(result.end(), localIds.begin(), localIds.end());
  result.insert(result.end(), groupIds.begin(), groupIds.end());
  return result;
}

InstVector NDRange::getSizes(unsigned int direction) {
  InstVector result;
  std::map<std::string, InstVector> &DirInsts = OCLInsts[direction];
  InstVector globalSizes = DirInsts[GET_GLOBAL_SIZE];
  InstVector localSizes = DirInsts[GET_LOCAL_SIZE];
  result.insert(result.end(), globalSizes.begin(), globalSizes.end());
  result.insert(result.end(), localSizes.begin(), localSizes.end());
  return result;
}

bool NDRange::IsTid(Instruction *I) {
  bool result = false;
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    result |= IsTidInDirection(I, direction);
  }
  return result;
}

bool NDRange::IsTidInDirection(Instruction *I, unsigned int direction) {
  // No bound checking is needed.
  std::map<std::string, InstVector> &dirInsts = OCLInsts[direction];
  bool isLocalId = IsPresent(I, dirInsts[GET_GLOBAL_ID]);
  bool isGlobalId = IsPresent(I, dirInsts[GET_LOCAL_ID]);
  bool isGroupId = IsPresent(I, dirInsts[GET_GROUP_ID]);
  return isLocalId || isGlobalId || isGroupId;
}

bool NDRange::IsGlobal(Instruction *I) {
  bool result = false;
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    result |= IsGlobal(I, direction);
  }
  return result;
}

bool NDRange::IsLocal(Instruction *I) {
  bool result = false;
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    result |= IsLocal(I, direction);
  }
  return result;
}

bool NDRange::IsGlobalSize(Instruction *I) {
  bool result = false;
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    result |= IsGlobalSize(I, direction);
  }
  return result;
}

bool NDRange::IsLocalSize(Instruction *I) {
  bool result = false;
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    result |= IsLocalSize(I, direction);
  }
  return result;
}

bool NDRange::IsGroupId(Instruction *I) {
  bool result = false;
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    result |= IsGroupId(I, direction);
  }
  return result;
}

bool NDRange::IsGlobal(Instruction *I, int direction) {
  return IsPresentInDirection(I, GET_GLOBAL_ID, direction);
}

bool NDRange::IsLocal(Instruction *I, int direction) {
  return IsPresentInDirection(I, GET_LOCAL_ID, direction);
}

bool NDRange::IsGlobalSize(Instruction *I, int direction) {
  return IsPresentInDirection(I, GET_GLOBAL_SIZE, direction);
}

bool NDRange::IsLocalSize(Instruction *I, int direction) {
  return IsPresentInDirection(I, GET_LOCAL_SIZE, direction);
}

bool NDRange::IsGroupId(Instruction *I, int direction) {
  return IsPresentInDirection(I, GET_GROUP_ID, direction);
}

void NDRange::dump() {
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    std::map<std::string, InstVector> &DirInsts = OCLInsts[direction];
    llvm::errs() << "Direction: " << direction << " ========= \n";
    llvm::errs() << GET_GLOBAL_ID << "\n";
    dumpVector(DirInsts[GET_GLOBAL_ID]);
    llvm::errs() << GET_LOCAL_ID << "\n";
    dumpVector(DirInsts[GET_LOCAL_ID]);
    llvm::errs() << GET_GROUP_ID << "\n";
    dumpVector(DirInsts[GET_GROUP_ID]);
    llvm::errs() << GET_GLOBAL_SIZE << "\n";
    dumpVector(DirInsts[GET_GLOBAL_SIZE]);
    llvm::errs() << GET_LOCAL_SIZE << "\n";
    dumpVector(DirInsts[GET_LOCAL_SIZE]);
  }
}

// -----------------------------------------------------------------------------
void NDRange::Init() {
  // A vector per dimension.
  OCLInsts.reserve(DIRECTION_NUMBER);
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    OCLInsts[direction] = std::map<std::string, InstVector>();
  }
  // Init the maps in every direction.
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    std::map<std::string, InstVector> &DirInsts = OCLInsts[direction];
    DirInsts[GET_GLOBAL_ID] = InstVector();
    DirInsts[GET_LOCAL_ID] = InstVector();
    DirInsts[GET_GLOBAL_SIZE] = InstVector();
    DirInsts[GET_LOCAL_SIZE] = InstVector();
    DirInsts[GET_GROUP_ID] = InstVector();
  }
}

void NDRange::ParseFunction(llvm::Function *F) {
  FindOpenCLFunctionCallsByNameAllDirs(GET_GLOBAL_ID, F);
  FindOpenCLFunctionCallsByNameAllDirs(GET_LOCAL_ID, F);
  FindOpenCLFunctionCallsByNameAllDirs(GET_GLOBAL_SIZE, F);
  FindOpenCLFunctionCallsByNameAllDirs(GET_LOCAL_SIZE, F);
  FindOpenCLFunctionCallsByNameAllDirs(GET_GROUP_ID, F);
}

bool NDRange::IsPresentInDirection(llvm::Instruction *I,
                                   const std::string &FuncName, int Dir) {
  std::map<std::string, InstVector> &DirInsts = OCLInsts[Dir];
  InstVector &Insts = DirInsts[FuncName];
  return IsPresent(I, Insts);
}

void NDRange::FindOpenCLFunctionCallsByNameAllDirs(std::string CalleeName,
                                                   Function *Caller) {
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    std::map<std::string, InstVector> &DirInsts = OCLInsts[direction];
    InstVector &Insts = DirInsts[CalleeName];
    FindOpenCLFunctionCallsByName(CalleeName, Caller, direction, Insts);
  }
}

// -----------------------------------------------------------------------------
void FindOpenCLFunctionCallsByName(std::string calleeName, Function *caller,
                                   int direction, InstVector &Target) {
  // Get function value.
  Function *callee = GetOpenCLFunctionByName(calleeName, caller);
  if (callee == NULL)
    return;
  // Find calls to the function.
  FindOpenCLFunctionCalls(callee, caller, direction, Target);
}

// -----------------------------------------------------------------------------
void FindOpenCLFunctionCalls(Function *callee, Function *caller, int direction,
                             InstVector &Target) {
  // Iterate over the uses of the function.
  for (Value::use_iterator iter = callee->use_begin(), end = callee->use_end();
       iter != end; ++iter) {
    if (CallInst *inst = dyn_cast<CallInst>(*iter))
      if (caller == GetFunctionOfInst(inst))
        if (const ConstantInt *ci =
                dyn_cast<ConstantInt>(inst->getArgOperand(0))) {
          int ArgumentValue = GetInteger(ci);
          if (direction == -1 || ArgumentValue == direction)
            Target.push_back(inst);
        }
  }
}

char NDRange::ID = 0;
static RegisterPass<NDRange> X("ndr", "Build NDRange data structure)");
