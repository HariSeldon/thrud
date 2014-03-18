#include "thrud/Support/NDRange.h"

std::string NDRange::GET_GLOBAL_ID = "get_global_id";
std::string NDRange::GET_LOCAL_ID = "get_local_id";
std::string NDRange::GET_GLOBAL_SIZE = "get_global_size";
std::string NDRange::GET_LOCAL_SIZE = "get_local_size";
std::string NDRange::GET_GROUP_ID = "get_group_id";
std::string NDRange::GET_GROUPS_NUMBER = "get_num_groups";
unsigned int NDRange::DIRECTION_NUMBER = 3;

NDRange::NDRange() : FunctionPass(ID) {}

void NDRange::getAnalysisUsage(AnalysisUsage &au) const {
  au.setPreservesAll();
}

bool NDRange::runOnFunction(Function &function) {
  Function *functionPtr = (Function *)&function;
  init();
  findOpenCLFunctionCallsByNameAllDirs(GET_GLOBAL_ID, functionPtr);
  findOpenCLFunctionCallsByNameAllDirs(GET_LOCAL_ID, functionPtr);
  findOpenCLFunctionCallsByNameAllDirs(GET_GLOBAL_SIZE, functionPtr);
  findOpenCLFunctionCallsByNameAllDirs(GET_LOCAL_SIZE, functionPtr);
  findOpenCLFunctionCallsByNameAllDirs(GET_GROUP_ID, functionPtr);
  findOpenCLFunctionCallsByNameAllDirs(GET_GROUPS_NUMBER, functionPtr);
  return false;
}

// -----------------------------------------------------------------------------
InstVector NDRange::getTids() {
  InstVector result;
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    std::map<std::string, InstVector> &dirInsts = oclInsts[direction];
    InstVector globalIds = dirInsts[GET_GLOBAL_ID];
    InstVector localIds = dirInsts[GET_LOCAL_ID];
    result.insert(result.end(), globalIds.begin(), globalIds.end());
    result.insert(result.end(), localIds.begin(), localIds.end());
  }
  return result;
}

InstVector NDRange::getSizes() {
  InstVector result;
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    std::map<std::string, InstVector> &dirInsts = oclInsts[direction];
    InstVector globalSizes = dirInsts[GET_GLOBAL_SIZE];
    InstVector localSizes = dirInsts[GET_LOCAL_SIZE];
    result.insert(result.end(), globalSizes.begin(), globalSizes.end());
    result.insert(result.end(), localSizes.begin(), localSizes.end());
  }
  return result;
}

InstVector NDRange::getTids(unsigned int direction) {
  InstVector result;
  std::map<std::string, InstVector> &dirInsts = oclInsts[direction];
  InstVector globalIds = dirInsts[GET_GLOBAL_ID];
  InstVector localIds = dirInsts[GET_LOCAL_ID];
  result.insert(result.end(), globalIds.begin(), globalIds.end());
  result.insert(result.end(), localIds.begin(), localIds.end());
  return result;
}

InstVector NDRange::getSizes(unsigned int direction) {
  InstVector result;
  std::map<std::string, InstVector> &dirInsts = oclInsts[direction];
  InstVector globalSizes = dirInsts[GET_GLOBAL_SIZE];
  InstVector localSizes = dirInsts[GET_LOCAL_SIZE];
  result.insert(result.end(), globalSizes.begin(), globalSizes.end());
  result.insert(result.end(), localSizes.begin(), localSizes.end());
  return result;
}

bool NDRange::isTid(Instruction *inst) {
  bool result = false;
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    result |= isTidInDirection(inst, direction);
  }
  return result;
}

bool NDRange::isTidInDirection(Instruction *inst, unsigned int direction) {
  // No bound checking is needed.
  std::map<std::string, InstVector> &dirInsts = oclInsts[direction];
  bool isLocalId = isPresent(inst, dirInsts[GET_GLOBAL_ID]);
  bool isGlobalId = isPresent(inst, dirInsts[GET_LOCAL_ID]);
  bool isGroupId = isPresent(inst, dirInsts[GET_GROUP_ID]);
  return isLocalId || isGlobalId || isGroupId;
}

std::string NDRange::getType(Instruction *inst) {
  if (isGlobal(inst))
    return GET_GLOBAL_ID;
  if (isLocal(inst))
    return GET_LOCAL_ID;
  if (isGroupId(inst))
    return GET_GROUP_ID;
  if (isGlobalSize(inst))
    return GET_GLOBAL_SIZE;
  if (isLocalSize(inst))
    return GET_LOCAL_SIZE;
  if (isGroupsNum(inst))
    return GET_GROUPS_NUMBER;
  return "";
}

unsigned int NDRange::getDirection(Instruction *inst) {
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    bool result =
        isGlobal(inst, direction) || isLocal(inst, direction) ||
        isGlobalSize(inst, direction) || isLocalSize(inst, direction) ||
        isGroupId(inst, direction) || isGroupsNum(inst, direction);
    if (result == true)
      return direction;
  }
  return -1;
}

bool NDRange::isGlobal(Instruction *inst) {
  bool result = false;
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    result |= isGlobal(inst, direction);
  }
  return result;
}

bool NDRange::isLocal(Instruction *inst) {
  bool result = false;
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    result |= isLocal(inst, direction);
  }
  return result;
}

bool NDRange::isGlobalSize(Instruction *inst) {
  bool result = false;
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    result |= isGlobalSize(inst, direction);
  }
  return result;
}

bool NDRange::isLocalSize(Instruction *inst) {
  bool result = false;
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    result |= isLocalSize(inst, direction);
  }
  return result;
}

bool NDRange::isGroupId(Instruction *inst) {
  bool result = false;
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    result |= isGroupId(inst, direction);
  }
  return result;
}

bool NDRange::isGroupsNum(Instruction *inst) {
  bool result = false;
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    result |= isGroupsNum(inst, direction);
  }
  return result;
}

bool NDRange::isGlobal(Instruction *inst, unsigned int direction) {
  return isPresentInDirection(inst, GET_GLOBAL_ID, direction);
}

bool NDRange::isLocal(Instruction *inst, unsigned int direction) {
  return isPresentInDirection(inst, GET_LOCAL_ID, direction);
}

bool NDRange::isGlobalSize(Instruction *inst, unsigned int direction) {
  return isPresentInDirection(inst, GET_GLOBAL_SIZE, direction);
}

bool NDRange::isLocalSize(Instruction *inst, unsigned int direction) {
  return isPresentInDirection(inst, GET_LOCAL_SIZE, direction);
}

bool NDRange::isGroupId(Instruction *inst, unsigned int direction) {
  return isPresentInDirection(inst, GET_GROUP_ID, direction);
}

bool NDRange::isGroupsNum(Instruction *inst, unsigned int direction) {
  return isPresentInDirection(inst, GET_GROUPS_NUMBER, direction);
}

void NDRange::dump() {
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    std::map<std::string, InstVector> &dirInsts = oclInsts[direction];
    llvm::errs() << "Direction: " << direction << " ========= \n";
    llvm::errs() << GET_GLOBAL_ID << "\n";
    dumpVector(dirInsts[GET_GLOBAL_ID]);
    llvm::errs() << GET_LOCAL_ID << "\n";
    dumpVector(dirInsts[GET_LOCAL_ID]);
    llvm::errs() << GET_GROUP_ID << "\n";
    dumpVector(dirInsts[GET_GROUP_ID]);
    llvm::errs() << GET_GLOBAL_SIZE << "\n";
    dumpVector(dirInsts[GET_GLOBAL_SIZE]);
    llvm::errs() << GET_LOCAL_SIZE << "\n";
    dumpVector(dirInsts[GET_LOCAL_SIZE]);
    llvm::errs() << GET_GROUPS_NUMBER << "\n";
    dumpVector(dirInsts[GET_GROUPS_NUMBER]);
  }
}

// -----------------------------------------------------------------------------
void NDRange::init() {
  // A vector per dimension.
  oclInsts.reserve(DIRECTION_NUMBER);
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    oclInsts.push_back(std::map<std::string, InstVector>());
  }
  // Init the maps in every direction.
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    std::map<std::string, InstVector> &dirInsts = oclInsts[direction];
    dirInsts[GET_GLOBAL_ID] = InstVector();
    dirInsts[GET_LOCAL_ID] = InstVector();
    dirInsts[GET_GLOBAL_SIZE] = InstVector();
    dirInsts[GET_LOCAL_SIZE] = InstVector();
    dirInsts[GET_GROUP_ID] = InstVector();
    dirInsts[GET_GROUPS_NUMBER] = InstVector();
  }
}

bool NDRange::isPresentInDirection(llvm::Instruction *inst,
                                   const std::string &functionName,
                                   unsigned int direction) {
  std::map<std::string, InstVector> &dirInsts = oclInsts[direction];
  const InstVector &insts = dirInsts[functionName];
  return isPresent(inst, insts);
}

void NDRange::findOpenCLFunctionCallsByNameAllDirs(std::string calleeName,
                                                   Function *caller) {
  for (unsigned int direction = 0; direction < DIRECTION_NUMBER; ++direction) {
    std::map<std::string, InstVector> &dirInsts = oclInsts[direction];
    InstVector &insts = dirInsts[calleeName];
    findOpenCLFunctionCallsByName(calleeName, caller, direction, insts);
  }
}

// -----------------------------------------------------------------------------
void findOpenCLFunctionCallsByName(std::string calleeName, Function *caller,
                                   unsigned int direction, InstVector &target) {
  // Get function value.
  Function *callee = getOpenCLFunctionByName(calleeName, caller);
  if (callee == NULL)
    return;
  // Find calls to the function.
  findOpenCLFunctionCalls(callee, caller, direction, target);
}

// -----------------------------------------------------------------------------
void findOpenCLFunctionCalls(Function *callee, Function *caller,
                             unsigned int direction, InstVector &target) {
  // Iterate over the uses of the function.
  for (Value::use_iterator iter = callee->use_begin(), end = callee->use_end();
       iter != end; ++iter) {
    if (CallInst *inst = dyn_cast<CallInst>(*iter))
      if (caller == GetFunctionOfInst(inst))
        if (const ConstantInt *ci =
                dyn_cast<ConstantInt>(inst->getArgOperand(0))) {
          if (ci->equalsInt(direction))
            target.push_back(inst);
        }
  }
}

char NDRange::ID = 0;
static RegisterPass<NDRange> X("ndr", "Build NDRange data structure)");
