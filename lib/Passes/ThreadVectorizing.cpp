#define DEBUG_TYPE "ThreadVectorizing"

#include "thrud/ThreadVectorizing/ThreadVectorizing.h"

#include "thrud/DivergenceAnalysis/DivergenceAnalysis.h"

#include "thrud/Support/NDRange.h"
#include "thrud/Support/NDRangeSpace.h"
#include "thrud/Support/OCLEnv.h"
#include "thrud/Support/SubscriptAnalysis.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"

#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Transforms/Utils/Cloning.h"

// STL header files.
#include <algorithm>
#include <functional>

#define THREAD_VECTORIZER_PASS_NAME "ThreadVectorizing"

// Support functions.
// -----------------------------------------------------------------------------
// Get the id of the called intrinsic.
Intrinsic::ID getIntrinsicIDForCall(CallInst *callInst);

// -----------------------------------------------------------------------------
// Command line options.
cl::opt<unsigned int>
    VectorizingDirectionCL("vectorizing-direction", cl::init(0), cl::Hidden,
                           cl::desc("Thr vectorizing direction"));
cl::opt<unsigned int> VectorizingWidthCL("vectorizing-width", cl::init(1),
                                         cl::Hidden,
                                         cl::desc("The vectorizing width"));
extern cl::opt<std::string> KernelNameCL;
extern cl::opt<ThreadVectorizing::DivRegionOption> DivRegionOptionCL;

ThreadVectorizing::ThreadVectorizing()
    : FunctionPass(ID), ndrSpace(1024, 1024, 1024, 1024, 1024, 1024) {}

void ThreadVectorizing::getAnalysisUsage(AnalysisUsage &au) const {
  au.addRequired<LoopInfo>();
  au.addRequired<ScalarEvolution>();
  au.addRequired<SingleDimDivAnalysis>();
  au.addRequired<PostDominatorTree>();
  au.addRequired<DominatorTree>();
  au.addRequired<NDRange>();
}

// -----------------------------------------------------------------------------
const char *ThreadVectorizing::getPassName() const {
  return THREAD_VECTORIZER_PASS_NAME;
}

// -----------------------------------------------------------------------------
bool ThreadVectorizing::doInitialization(Module &module) {
  // Initialize the IR builder with the context from the current module.
  irBuilder = new IRBuilder<>(module.getContext());
  return false;
}

// -----------------------------------------------------------------------------
bool ThreadVectorizing::doFinalization(Module &function) {
  delete irBuilder;
  return false;
}

//------------------------------------------------------------------------------
bool ThreadVectorizing::runOnFunction(Function &function) {
  // Apply the pass to kernels only.
  if (!isKernel((const Function *)&function))
    return false;

  // Apply the pass to the selected kernel only.
  std::string functionName = function.getName();
  if (KernelNameCL != "" && functionName != KernelNameCL)
    return false;

  direction = VectorizingDirectionCL;
  width = VectorizingWidthCL;
  divRegionOption = DivRegionOptionCL;

  // Collect analysis information.
  loopInfo = &getAnalysis<LoopInfo>();
  pdt = &getAnalysis<PostDominatorTree>();
  dt = &getAnalysis<DominatorTree>();
  sdda = &getAnalysis<SingleDimDivAnalysis>();
  ndr = &getAnalysis<NDRange>();
  scalarEvolution = &getAnalysis<ScalarEvolution>();

  ocl = new OCLEnv(function, ndr, ndrSpace);
  Warp warp(0, 0, 0, 0, ndrSpace);
  subscriptAnalysis = new SubscriptAnalysis(scalarEvolution, ocl, warp);

  init();

  return performVectorization(function);
}

//------------------------------------------------------------------------------
void ThreadVectorizing::init() {
  vectorMap.clear();
  toRemoveInsts.clear();
  vectorPhis.clear();
  phMap.clear();
  irBuilder->ClearInsertionPoint();
  kernelFunction = NULL;
}

//------------------------------------------------------------------------------
bool ThreadVectorizing::performVectorization(Function &function) {
  kernelFunction = static_cast<Function *>(&function);

  function.getParent()->dump();

  widenTids();
  vectorizeFunction();
  removeVectorPlaceholders();
  removeScalarInsts();

  function.dump();

  return true;
}

//------------------------------------------------------------------------------
void ThreadVectorizing::setInsertPoint(Instruction *inst) {
  BasicBlock *block = inst->getParent();
  irBuilder->SetInsertPoint(block, inst);
  BasicBlock::iterator current_insert_point = irBuilder->GetInsertPoint();
  ++current_insert_point;
  irBuilder->SetInsertPoint(block, current_insert_point);
}

//------------------------------------------------------------------------------
Value *ThreadVectorizing::widenValue(Value *value) {
  // Support types.
  LLVMContext &context = value->getContext();
  Type *vector_type = VectorType::get(value->getType(), width);
  Type *integer_32 = IntegerType::getInt32Ty(context);

  // Support values.
  Constant *zero = ConstantInt::get(integer_32, 0);
  Value *zero_vector =
      ConstantAggregateZero::get(VectorType::get(integer_32, width));
  Value *undefined_value = UndefValue::get(vector_type);

  // The widening of the value will be placed in the block that
  // immediatelly dominates all the used of the value.
  // Only if the value is not an instruction.
  IRBuilderBase::InsertPoint originalIp = irBuilder->saveIP();
  Instruction *inst = dyn_cast<Instruction>(value);
  if (NULL == inst) {
    // The widening of the value will be placed in the block that
    // immediatelly dominates all the uses of the value.

    // If we got here there must be at least one use.
    User *first_user = value->use_back();
    BasicBlock *dominator = cast<Instruction>(first_user)->getParent();

    for (Value::use_iterator iter = value->use_begin(),
                             iterEnd = value->use_end();
         iter != iterEnd; ++iter) {
      User *user = *iter;

      if (!isa<Instruction>(user)) {
        continue;
      }

      BasicBlock *block = cast<Instruction>(user)->getParent();

      // If the user is not in the current kernel skip it.
      if (block == NULL || block->getParent() != kernelFunction ||
          block->getParent() != dominator->getParent()) {
        continue;
      }

      assert(block->getParent() == dominator->getParent() && "Wrong function");

      dominator = dt->findNearestCommonDominator(dominator, block);
    }

    //    IRBuilderBase::InsertPoint originalIp = irBuilder->saveIP();
    irBuilder->SetInsertPoint(dominator->getFirstNonPHI());
  }

  // Create an undefined vector contaning in the first position the
  // original value.
  Value *single_element_array =
      irBuilder->CreateInsertElement(undefined_value, value, zero, "inserted");

  // Replicate the original value to all the positions of the vector.
  Value *widenedVector = irBuilder->CreateShuffleVector(
      single_element_array, undefined_value, zero_vector, "widened");

  if (NULL == inst) {
    // Restore the previous insertion point.
    irBuilder->restoreIP(originalIp);
  }

  return widenedVector;
}

//------------------------------------------------------------------------------
void ThreadVectorizing::vectorizeFunction() {
  InstVector &insts = sdda->getOutermostDivInsts();
  RegionVector &regions = sdda->getOutermostDivRegions();

  errs() << "To vectorize:\n";
  dumpVector(insts);

  // Replicate insts.
  for (InstVector::iterator iter = insts.begin(), iterEnd = insts.end();
       iter != iterEnd; ++iter) {
    Instruction *inst = *iter;
    setInsertPoint(inst);
    Value *vectorResult = vectorizeInst(inst);
    if (NULL != vectorResult) {
      vectorMap[inst] = vectorResult;
      toRemoveInsts.insert(inst);
    }
  }

  for (RegionVector::iterator iter = regions.begin(), iterEnd = regions.end(); 
       iter != iterEnd; ++iter) {
    (*iter)->dump();
  }

  // Replicate regions.
  std::for_each(
      regions.begin(), regions.end(),
      std::bind1st(std::mem_fun(&ThreadVectorizing::replicateRegion), this));

  fixPhiNodes();
}

//------------------------------------------------------------------------------
Value *ThreadVectorizing::vectorizeInst(Instruction *inst) {
  unsigned int inst_opcode = inst->getOpcode();
  switch (inst_opcode) {
  case Instruction::Br: {
    assert(false && "Branches should never be vectorized");
    return NULL;
  }

  case Instruction::PHI: {
    PHINode *phi_node_inst = dyn_cast<PHINode>(inst);
    return vectorizePhiNode(phi_node_inst);
  }

  case Instruction::Add:
  case Instruction::FAdd:
  case Instruction::Sub:
  case Instruction::FSub:
  case Instruction::Mul:
  case Instruction::FMul:
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::FDiv:
  case Instruction::URem:
  case Instruction::SRem:
  case Instruction::FRem:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor: {
    BinaryOperator *binary_operator = dyn_cast<BinaryOperator>(inst);
    return vectorizeBinaryOperator(binary_operator);
  }

  case Instruction::Select: {
    SelectInst *select_inst = dyn_cast<SelectInst>(inst);
    return vectorizeSelect(select_inst);
  }

  case Instruction::ICmp:
  case Instruction::FCmp: {
    CmpInst *cmp_inst = dyn_cast<CmpInst>(inst);
    return vectorizeCmp(cmp_inst);
  }

  case Instruction::Store: {
    StoreInst *storeInst = dyn_cast<StoreInst>(inst);
    return vectorizeStore(storeInst);
  }

  case Instruction::Load: {
    LoadInst *loadInst = dyn_cast<LoadInst>(inst);
    return vectorizeLoad(loadInst);
  }

  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::FPExt:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::SIToFP:
  case Instruction::UIToFP:
  case Instruction::Trunc:
  case Instruction::FPTrunc:
  case Instruction::BitCast: {
    CastInst *castInst = dyn_cast<CastInst>(inst);
    return vectorizeCast(castInst);
  }

  case Instruction::Call: {
    assert(!isa<DbgInfoIntrinsic>(inst) && "dbg intrisics are not supported");

    CallInst *callInst = dyn_cast<CallInst>(inst);
    return vectorizeCall(callInst);
  }

  // E.g. GetElementPtr should always be scalarized.
  default: { return replicateInst(inst); }
  }
}

Value *ThreadVectorizing::getVectorValue(Value *scalar) {
  assert(!scalar->getType()->isVectorTy() && "Can't widen a vector");

  // If the value is contained in the vector map then return it.
  if (true == vectorMap.count(scalar)) {
    return vectorMap[scalar];
  } else {
    // If the value to widen is a varying instruction that is not in the
    // map then it is going to be widended in the future.
    // To prevent dependency cycles create a placeholder and place it
    // in a map.
    if (Instruction *inst = dyn_cast<Instruction>(scalar)) {
      if (true == sdda->isDivergent(inst)) {
        if (true == phMap.count(scalar)) {
          return phMap[scalar];
        }

        // Widen the scalar value generating the place holder.
        Value *widenedValue = widenValue(scalar);
        phMap[scalar] = widenedValue;

        return widenedValue;
      }
    }
  }

  // Widen the scalar value.
  Value *widenedValue = widenValue(scalar);
  vectorMap[scalar] = widenedValue;
  return widenedValue;
}

PHINode *ThreadVectorizing::vectorizePhiNode(PHINode *phiNode) {
  // Create the type for the new phi node.
  Type *phiType = phiNode->getType();
  Type *vectorPhiType = VectorType::get(phiType, width);

  // Insert a temporary phi node.
  PHINode *vectorPhi =
      PHINode::Create(vectorPhiType, phiNode->getNumIncomingValues());
  irBuilder->Insert(vectorPhi);
  vectorPhi->setName(phiNode->getName() + Twine(".vector"));

  vectorPhis.push_back(phiNode);

  return vectorPhi;
}

void ThreadVectorizing::fixPhiNodes() {
  for (PhiVector::iterator iter = vectorPhis.begin(),
                           iterEnd = vectorPhis.end();
       iter != iterEnd; ++iter) {
    PHINode *scalar_phi = *iter;
    PHINode *vector_phi = dyn_cast<PHINode>(vectorMap[scalar_phi]);
    assert(NULL != vector_phi && "Phi node mapped to a non-phi");

    unsigned int operands_number = scalar_phi->getNumIncomingValues();
    for (unsigned int operand_index = 0; operand_index < operands_number;
         ++operand_index) {
      Value *scalar_value = scalar_phi->getIncomingValue(operand_index);
      vector_phi->addIncoming(getVectorValue(scalar_value),
                              scalar_phi->getIncomingBlock(operand_index));
    }
  }
}

BinaryOperator *
ThreadVectorizing::vectorizeBinaryOperator(BinaryOperator *binary_operator) {
  // Get the operands of binary operator.
  Value *first_operand = binary_operator->getOperand(0);
  Value *second_operand = binary_operator->getOperand(1);

  // Get the operator of the binary operator.
  Instruction::BinaryOps op_code = binary_operator->getOpcode();

  // Get the vector version of the operands.
  first_operand = getVectorValue(first_operand);
  second_operand = getVectorValue(second_operand);

  // Create the vector instruction.
  BinaryOperator *vector_operator =
      BinaryOperator::Create(op_code, first_operand, second_operand);

  irBuilder->Insert(vector_operator)->setName(binary_operator->getName());

  return vector_operator;
}

// -----------------------------------------------------------------------------
Value *ThreadVectorizing::vectorizeLoad(LoadInst *loadInst) {
  // Get the load pointer.
  Value *load_pointer = loadInst->getPointerOperand();

  GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(load_pointer);

  // If the store is not consecutive along the vectorizing dimension then
  // it has to be replicated.
  if (false ==
      subscriptAnalysis->isConsecutive(gep, (int)(VectorizingDirectionCL))) {
    return replicateInst(loadInst);
  }

  unsigned int operands_number = gep->getNumOperands();
  Value *last_operand = gep->getOperand(operands_number - 1);
  last_operand = getVectorValue(last_operand);

  last_operand = irBuilder->CreateExtractElement(
      last_operand, irBuilder->getInt32(0), "extracted");

  // Create the new gep.
  GetElementPtrInst *new_gep = cast<GetElementPtrInst>(gep->clone());
  new_gep->setOperand(operands_number - 1, last_operand);
  load_pointer = irBuilder->Insert(new_gep);
  load_pointer->setName("load_ptr");
  //insert_sorted<Instruction>(toRemoveInsts, gep);
  toRemoveInsts.insert(gep);

  Type *vector_load_type =
      getVectorPointerType(loadInst->getPointerOperand()->getType());

  load_pointer =
      irBuilder->CreateBitCast(load_pointer, vector_load_type, "bitcast");

  // Insert the vector load instruction into the function.
  LoadInst *vector_load = new LoadInst(load_pointer);
  vector_load->setAlignment(loadInst->getAlignment());
  irBuilder->Insert(vector_load)->setName("vector_load");

  return vector_load;
}

// -----------------------------------------------------------------------------
Value *ThreadVectorizing::vectorizeStore(StoreInst *storeInst) {
  // Get the store pointer.
  Value *store_pointer = storeInst->getPointerOperand();
  GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(store_pointer);

  // If the store is not consecutive along the vectorizing dimension then
  // it has to be replicated.
  if (false ==
      subscriptAnalysis->isConsecutive(gep, (int)(VectorizingDirectionCL))) {
    return replicateInst(storeInst);
  }

  unsigned int operands_number = gep->getNumOperands();
  Value *last_operand = gep->getOperand(operands_number - 1);
  last_operand = getVectorValue(last_operand);

  last_operand = irBuilder->CreateExtractElement(
      last_operand, irBuilder->getInt32(0), "extracted");

  // Create the new gep.
  GetElementPtrInst *new_gep = cast<GetElementPtrInst>(gep->clone());
  new_gep->setOperand(operands_number - 1, last_operand);
  new_gep = irBuilder->Insert(new_gep);
  new_gep->setName("store_ptr");
  store_pointer = new_gep;
  //insert_sorted<Instruction>(toRemoveInsts, gep);
  toRemoveInsts.insert(gep);

  // Get the store type.
  Type *scalar_store_type = new_gep->getPointerOperandType();

  // Create the vector store pointer type.
  Type *vector_store_type = getVectorPointerType(scalar_store_type);

  store_pointer =
      irBuilder->CreateBitCast(store_pointer, vector_store_type, "bitcast");

  // Get the value to store to memory.
  Value *to_store_value = getVectorValue(storeInst->getValueOperand());

  // Insert the vector store instruction into the function.
  StoreInst *vector_store =
      irBuilder->CreateStore(to_store_value, store_pointer, "vector_store");
  vector_store->setAlignment(storeInst->getAlignment());
  vector_store->setVolatile(storeInst->isVolatile());

  return vector_store;
}

// -----------------------------------------------------------------------------
SelectInst *ThreadVectorizing::vectorizeSelect(SelectInst *select_inst) {
  Value *condition = select_inst->getOperand(0);

  bool varying_condition = false;
  if (Instruction *condition_inst = dyn_cast<Instruction>(condition)) {
    varying_condition = sdda->isDivergent(condition_inst);
  }

  if (true == varying_condition) {
    condition = getVectorValue(condition);
  }

  Value *first_operand = select_inst->getOperand(1);
  Value *second_operand = select_inst->getOperand(2);

  first_operand = getVectorValue(first_operand);
  second_operand = getVectorValue(second_operand);

  SelectInst *result = SelectInst::Create(
      condition, first_operand, second_operand, select_inst->getName());

  irBuilder->Insert(result)->setName(select_inst->getName());

  return result;
}

// -----------------------------------------------------------------------------
CmpInst *ThreadVectorizing::vectorizeCmp(CmpInst *cmp_inst) {
  // Get the compare operands.
  Value *first_operand = cmp_inst->getOperand(0);
  Value *second_operand = cmp_inst->getOperand(1);

  // Get the vector version of the operands.
  first_operand = getVectorValue(first_operand);
  second_operand = getVectorValue(second_operand);

  CmpInst *result = NULL;

  // Generate the vector version of the instruction.
  if (Instruction::FCmp == cmp_inst->getOpcode()) {
    result =
        new FCmpInst(cmp_inst->getPredicate(), first_operand, second_operand);
  } else {
    result =
        new ICmpInst(cmp_inst->getPredicate(), first_operand, second_operand);
  }

  irBuilder->Insert(result)->setName(cmp_inst->getName());
  return result;
}

// -----------------------------------------------------------------------------
CastInst *ThreadVectorizing::vectorizeCast(CastInst *cast_inst) {
  // Get the cast operand.
  Value *operand = cast_inst->getOperand(0);

  // Get the vector version of the operand.
  operand = getVectorValue(operand);

  Type *dest_type =
      VectorType::get(cast_inst->getType()->getScalarType(), width);

  // Generate the vector version of the instruction.
  CastInst *result =
      CastInst::Create(cast_inst->getOpcode(), operand, dest_type);
  irBuilder->Insert(result)->setName(cast_inst->getName());

  return result;
}

// -----------------------------------------------------------------------------
// Only supports builtin function calls.
CallInst *ThreadVectorizing::vectorizeCall(CallInst *callInst) {
  Module *module = callInst->getParent()->getParent()->getParent();
  Intrinsic::ID intId = getIntrinsicIDForCall(callInst);
  assert(intId && "Not an intrinsic call!");
  
  ValueVector vectorOperands = getWidenedCallOperands(callInst);
  
  Type *types[] = { VectorType::get(callInst->getType()->getScalarType(),
                                    width) };
  Function *function = Intrinsic::getDeclaration(module, intId, types);
  ArrayRef<Value *> args(vectorOperands.data(), vectorOperands.size());
  CallInst *result = irBuilder->CreateCall(function, args);
  return result;
}

// -----------------------------------------------------------------------------
Value *ThreadVectorizing::replicateInst(Instruction *inst) {
  assert(false == inst->getType()->isVectorTy() &&
         "Cannot replicate instructions of vector type");

  //  LLVMContext &context = inst->getContext();
  ValueVector operands = getWidenedOperands(inst);

  bool is_inst_void = inst->getType()->isVoidTy();
  Value *result = NULL;

  if (false == is_inst_void) {
    result = UndefValue::get(VectorType::get(inst->getType(), width));
  }

  for (unsigned int vector_index = 0; vector_index < width; ++vector_index) {
    Instruction *cloned = inst->clone();

    // For each operand extract the scalar from the vector.
    for (unsigned int operand_index = 0, operand_end = inst->getNumOperands();
         operand_index != operand_end; ++operand_index) {
      Value *operand = operands[operand_index];

      // Param is a vector. Need to extract the value.
      if (true == operand->getType()->isVectorTy()) {
        operand = irBuilder->CreateExtractElement(
            operand, irBuilder->getInt32(vector_index), "extracted");
      }

      // Set the operand.
      cloned->setOperand(operand_index, operand);
    }

    Twine cloned_name = "";

    if (true == inst->hasName()) {
      cloned_name = inst->getName() + ".replicated" + Twine(vector_index);
    }

    irBuilder->Insert(cloned)->setName(cloned_name);

    if (false == is_inst_void) {
      result = irBuilder->CreateInsertElement(
          result, cloned, irBuilder->getInt32(vector_index), "inserted");
    }
  }

  return result;
}

// -----------------------------------------------------------------------------
ValueVector ThreadVectorizing::getWidenedOperands(Instruction *inst) {
  // Generate the list of operands for the replicated instructions.
  ValueVector operands;
  operands.reserve(inst->getNumOperands());

  // Find all of the vector operands.
  for (unsigned int operand_index = 0, operand_end = inst->getNumOperands();
       operand_index != operand_end; ++operand_index) {

    Value *operand = inst->getOperand(operand_index);
    Value *vectorOperand = getVectorValue(operand);

    operands.push_back(vectorOperand);
  }

  return operands;
}

//------------------------------------------------------------------------------
ValueVector ThreadVectorizing::getWidenedCallOperands(CallInst *inst) {
  // Generate the list of operands for the replicated instructions.
  ValueVector operands;
  operands.reserve(inst->getNumOperands());

  // Find all of the vector operands.
  for (unsigned int operand_index = 0, operand_end = inst->getNumOperands();
       operand_index != operand_end - 1; ++operand_index) {

    Value *operand = inst->getOperand(operand_index);

    Value *vectorOperand = getVectorValue(operand);

    operands.push_back(vectorOperand);
  }

  return operands;

}


// -----------------------------------------------------------------------------
Type *ThreadVectorizing::getVectorPointerType(Type *scalar_pointer_type) {
  // The input type must be a pointer type.
  PointerType *pointer_type = dyn_cast<PointerType>(scalar_pointer_type);
  assert(NULL != pointer_type &&
         "Cannot create a vector pointer without a pointer type");

  // Get the address space.
  unsigned int address_space = pointer_type->getAddressSpace();
  // Get the pointee type.
  Type *scalar_type = pointer_type->getElementType();

  // Create the type of the new vector store.
  Type *vector_store_type = VectorType::get(scalar_type, width);

  // Create the pointer type.
  PointerType *vector_pointer_type =
      PointerType::get(vector_store_type, address_space);

  return vector_pointer_type;
}

// -----------------------------------------------------------------------------
void ThreadVectorizing::removeScalarInsts() {
  // Do a first sweep to find the users of values to be removed.
  // Add these to the toRemoveInsts set.

  InstSet newToRemove(toRemoveInsts);
  InstSet tmp;

  do {
    tmp = newToRemove;
    newToRemove.clear();
    for (InstSet::iterator iter = tmp.begin(), iterEnd = tmp.end();
         iter != iterEnd; ++iter) {
      Instruction *inst = *iter;
      for (Value::use_iterator useIter = inst->use_begin(),
                               useIterEnd = inst->use_end();
           useIter != useIterEnd; ++useIter) {
        User *user = *useIter;
        if (Instruction *userInst = dyn_cast<Instruction>(user)) {
          if (toRemoveInsts.insert(userInst).second == true) {
            newToRemove.insert(userInst);
          }
        }
      }
    }
  } while (!newToRemove.empty());

  // Actually remove all the instructions.
  // Go through the list of instructions to be removed.
  for (InstSet::iterator iter = toRemoveInsts.begin(),
                         iterEnd = toRemoveInsts.end();
       iter != iterEnd; ++iter) {
    Instruction *inst = *iter;

    // Remove the current instruction from the function.
    if (false == inst->getType()->isVoidTy()) {
      inst->replaceAllUsesWith(Constant::getNullValue(inst->getType()));
    }
    inst->eraseFromParent();
  }
}

// -----------------------------------------------------------------------------
void ThreadVectorizing::removeVectorPlaceholders() {
  // Go through the placeholder map.
  for (V2VMap::iterator iter = phMap.begin(), iterEnd = phMap.end();
       iter != iterEnd; ++iter) {
    // Get the original scalar value.
    Value *scalar_value = iter->first;
    // Get the vector placeholder.
    Value *placeholder_value = iter->second;
    // Get the vector value corresponding to the scalar value.
    Value *vector_value = vectorMap[scalar_value];
    // Replace the placeholder with the vector value.
    placeholder_value->replaceAllUsesWith(vector_value);
  }
}

//------------------------------------------------------------------------------
char ThreadVectorizing::ID = 0;
static RegisterPass<ThreadVectorizing>
    X("tv", "OpenCL Thread Vectorizing Transformation Pass");

//##############################################################################
// Support functions.

//------------------------------------------------------------------------------
Intrinsic::ID getIntrinsicIDForCall(CallInst *callInst) {
  // If we have an intrinsic call, check if it is trivially vectorizable.
  if (IntrinsicInst *intrinsic = dyn_cast<IntrinsicInst>(callInst)) {
    switch (intrinsic->getIntrinsicID()) {
    case Intrinsic::sqrt:
    case Intrinsic::sin:
    case Intrinsic::cos:
    case Intrinsic::exp:
    case Intrinsic::exp2:
    case Intrinsic::log:
    case Intrinsic::log10:
    case Intrinsic::log2:
    case Intrinsic::fabs:
    case Intrinsic::floor:
    case Intrinsic::ceil:
    case Intrinsic::trunc:
    case Intrinsic::rint:
    case Intrinsic::nearbyint:
    case Intrinsic::pow:
    case Intrinsic::fma:
    case Intrinsic::fmuladd:
      return intrinsic->getIntrinsicID();
    default:
      return Intrinsic::not_intrinsic;
    }
  }

  return Intrinsic::not_intrinsic;
}
