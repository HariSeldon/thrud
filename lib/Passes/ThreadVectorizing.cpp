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

void ThreadVectorizing::getAnalysisUsage(llvm::AnalysisUsage &au) const {
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
bool ThreadVectorizing::doInitialization(llvm::Module &module) {
  // Initialize the IR builder with the context from the current module.
  irBuilder = new llvm::IRBuilder<>(module.getContext());
  return false;
}

// -----------------------------------------------------------------------------
bool ThreadVectorizing::doFinalization(llvm::Module &function) {
  delete irBuilder;
  return false;
}

//------------------------------------------------------------------------------
bool ThreadVectorizing::runOnFunction(llvm::Function &function) {
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
bool ThreadVectorizing::performVectorization(llvm::Function &function) {
  kernelFunction = static_cast<llvm::Function *>(&function);

  widenTids();
  vectorizeFunction();
  removeVectorPlaceholders();
  removeScalarInsts();

  return true;
}

//------------------------------------------------------------------------------
void ThreadVectorizing::setInsertPoint(llvm::Instruction *inst) {
  llvm::BasicBlock *block = inst->getParent();
  irBuilder->SetInsertPoint(block, inst);
  llvm::BasicBlock::iterator current_insert_point = irBuilder->GetInsertPoint();
  ++current_insert_point;
  irBuilder->SetInsertPoint(block, current_insert_point);
}

//------------------------------------------------------------------------------
llvm::Value *ThreadVectorizing::widenValue(llvm::Value *value) {
  // Support types.
  llvm::LLVMContext &context = value->getContext();
  llvm::Type *vector_type = llvm::VectorType::get(value->getType(), width);
  llvm::Type *integer_32 = llvm::IntegerType::getInt32Ty(context);

  // Support values.
  llvm::Constant *zero = llvm::ConstantInt::get(integer_32, 0);
  llvm::Value *zero_vector = llvm::ConstantAggregateZero::get(
      llvm::VectorType::get(integer_32, width));
  llvm::Value *undefined_value = llvm::UndefValue::get(vector_type);

  // The widening of the value will be placed in the block that
  // immediatelly dominates all the used of the value.
  // Only if the value is not an instruction.
  llvm::IRBuilderBase::InsertPoint originalIp = irBuilder->saveIP();
  llvm::Instruction *inst = llvm::dyn_cast<llvm::Instruction>(value);
  if (NULL == inst) {
    // The widening of the value will be placed in the block that
    // immediatelly dominates all the uses of the value.

    // If we got here there must be at least one use.
    llvm::User *first_user = value->use_back();
    llvm::BasicBlock *dominator =
        llvm::cast<llvm::Instruction>(first_user)->getParent();

    for (llvm::Value::use_iterator iter = value->use_begin(),
                                   iterEnd = value->use_end();
         iter != iterEnd; ++iter) {
      llvm::User *user = *iter;

      if (!isa<llvm::Instruction>(user)) {
        continue;
      }

      llvm::BasicBlock *block =
          llvm::cast<llvm::Instruction>(user)->getParent();

      // If the user is not in the current kernel skip it.
      if (block == NULL || block->getParent() != kernelFunction ||
          block->getParent() != dominator->getParent()) {
        continue;
      }

      assert(block->getParent() == dominator->getParent() && "Wrong function");

      dominator = dt->findNearestCommonDominator(dominator, block);
    }

    //    llvm::IRBuilderBase::InsertPoint originalIp = irBuilder->saveIP();
    irBuilder->SetInsertPoint(dominator->getFirstNonPHI());
  }

  // Create an undefined vector contaning in the first position the
  // original value.
  llvm::Value *single_element_array =
      irBuilder->CreateInsertElement(undefined_value, value, zero, "inserted");

  // Replicate the original value to all the positions of the vector.
  llvm::Value *widenedVector = irBuilder->CreateShuffleVector(
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

  // Replicate insts.
  for (InstVector::iterator iter = insts.begin(), iterEnd = insts.end();
       iter != iterEnd; ++iter) {
    llvm::Instruction *inst = *iter;
    setInsertPoint(inst);
    llvm::Value *vectorResult = vectorizeInst(inst);
    if (NULL != vectorResult) {
      vectorMap[inst] = vectorResult;
      toRemoveInsts.insert(inst);
    }
  }

  // Replicate regions.
  std::for_each(
      regions.begin(), regions.end(),
      std::bind1st(std::mem_fun(&ThreadVectorizing::replicateRegion), this));

  fixPhiNodes();

//  removeScalarInsts();
}

//------------------------------------------------------------------------------
llvm::Value *ThreadVectorizing::vectorizeInst(llvm::Instruction *inst) {
  unsigned int inst_opcode = inst->getOpcode();
  switch (inst_opcode) {
  case llvm::Instruction::Br: {
    assert(false && "Branches should never be vectorized");
    return NULL;
  }

  case llvm::Instruction::PHI: {
    llvm::PHINode *phi_node_inst = llvm::dyn_cast<llvm::PHINode>(inst);
    return vectorizePhiNode(phi_node_inst);
  }

  case llvm::Instruction::Add:
  case llvm::Instruction::FAdd:
  case llvm::Instruction::Sub:
  case llvm::Instruction::FSub:
  case llvm::Instruction::Mul:
  case llvm::Instruction::FMul:
  case llvm::Instruction::UDiv:
  case llvm::Instruction::SDiv:
  case llvm::Instruction::FDiv:
  case llvm::Instruction::URem:
  case llvm::Instruction::SRem:
  case llvm::Instruction::FRem:
  case llvm::Instruction::Shl:
  case llvm::Instruction::LShr:
  case llvm::Instruction::AShr:
  case llvm::Instruction::And:
  case llvm::Instruction::Or:
  case llvm::Instruction::Xor: {
    llvm::BinaryOperator *binary_operator =
        llvm::dyn_cast<llvm::BinaryOperator>(inst);
    return vectorizeBinaryOperator(binary_operator);
  }

  case llvm::Instruction::Select: {
    llvm::SelectInst *select_inst = llvm::dyn_cast<llvm::SelectInst>(inst);
    return vectorizeSelect(select_inst);
  }

  case llvm::Instruction::ICmp:
  case llvm::Instruction::FCmp: {
    llvm::CmpInst *cmp_inst = llvm::dyn_cast<llvm::CmpInst>(inst);
    return vectorizeCmp(cmp_inst);
  }

  case llvm::Instruction::Store: {
    llvm::StoreInst *storeInst = llvm::dyn_cast<llvm::StoreInst>(inst);
    return vectorizeStore(storeInst);
  }

  case llvm::Instruction::Load: {
    llvm::LoadInst *loadInst = llvm::dyn_cast<llvm::LoadInst>(inst);
    return vectorizeLoad(loadInst);
  }

  case llvm::Instruction::ZExt:
  case llvm::Instruction::SExt:
  case llvm::Instruction::FPToUI:
  case llvm::Instruction::FPToSI:
  case llvm::Instruction::FPExt:
  case llvm::Instruction::PtrToInt:
  case llvm::Instruction::IntToPtr:
  case llvm::Instruction::SIToFP:
  case llvm::Instruction::UIToFP:
  case llvm::Instruction::Trunc:
  case llvm::Instruction::FPTrunc:
  case llvm::Instruction::BitCast: {
    llvm::CastInst *cast_inst = llvm::dyn_cast<llvm::CastInst>(inst);
    return vectorizeCast(cast_inst);
  }

  // E.g. GetElementPtr should always be scalarized.
  default: { return replicateInst(inst); }
  }
}

llvm::Value *ThreadVectorizing::getVectorValue(llvm::Value *scalar) {
  assert(!scalar->getType()->isVectorTy() && "Can't widen a vector");

  // If the value is contained in the vector map then return it.
  if (true == vectorMap.count(scalar)) {
    return vectorMap[scalar];
  } else {
    // If the value to widen is a varying instruction that is not in the
    // map then it is going to be widended in the future.
    // To prevent dependency cycles create a placeholder and place it
    // in a map.
    if (llvm::Instruction *inst = llvm::dyn_cast<llvm::Instruction>(scalar)) {
      if (true == sdda->isDivergent(inst)) {
        if (true == phMap.count(scalar)) {
          return phMap[scalar];
        }

        // Widen the scalar value generating the place holder.
        llvm::Value *widenedValue = widenValue(scalar);
        phMap[scalar] = widenedValue;

        return widenedValue;
      }
    }
  }

  // Widen the scalar value.
  llvm::Value *widenedValue = widenValue(scalar);
  vectorMap[scalar] = widenedValue;
  return widenedValue;
}

llvm::PHINode *ThreadVectorizing::vectorizePhiNode(llvm::PHINode *phiNode) {
  // Create the type for the new phi node.
  llvm::Type *phiType = phiNode->getType();
  llvm::Type *vectorPhiType = llvm::VectorType::get(phiType, width);

  // Insert a temporary phi node.
  llvm::PHINode *vectorPhi =
      llvm::PHINode::Create(vectorPhiType, phiNode->getNumIncomingValues());
  irBuilder->Insert(vectorPhi);
  vectorPhi->setName(phiNode->getName() + llvm::Twine(".vector"));

  vectorPhis.push_back(phiNode);

  return vectorPhi;
}

void ThreadVectorizing::fixPhiNodes() {
  for (PhiVector::iterator iter = vectorPhis.begin(),
                           iterEnd = vectorPhis.end();
       iter != iterEnd; ++iter) {
    llvm::PHINode *scalar_phi = *iter;
    llvm::PHINode *vector_phi =
        llvm::dyn_cast<llvm::PHINode>(vectorMap[scalar_phi]);
    assert(NULL != vector_phi && "Phi node mapped to a non-phi");

    unsigned int operands_number = scalar_phi->getNumIncomingValues();
    for (unsigned int operand_index = 0; operand_index < operands_number;
         ++operand_index) {
      llvm::Value *scalar_value = scalar_phi->getIncomingValue(operand_index);
      vector_phi->addIncoming(getVectorValue(scalar_value),
                              scalar_phi->getIncomingBlock(operand_index));
    }
  }
}

llvm::BinaryOperator *ThreadVectorizing::vectorizeBinaryOperator(
    llvm::BinaryOperator *binary_operator) {
  // Get the operands of binary operator.
  llvm::Value *first_operand = binary_operator->getOperand(0);
  llvm::Value *second_operand = binary_operator->getOperand(1);

  // Get the operator of the binary operator.
  llvm::Instruction::BinaryOps op_code = binary_operator->getOpcode();

  // Get the vector version of the operands.
  first_operand = getVectorValue(first_operand);
  second_operand = getVectorValue(second_operand);

  // Create the vector instruction.
  llvm::BinaryOperator *vector_operator =
      llvm::BinaryOperator::Create(op_code, first_operand, second_operand);

  irBuilder->Insert(vector_operator)->setName(binary_operator->getName());

  return vector_operator;
}

// -----------------------------------------------------------------------------
llvm::Value *ThreadVectorizing::vectorizeLoad(llvm::LoadInst *loadInst) {
  // Get the load pointer.
  llvm::Value *load_pointer = loadInst->getPointerOperand();

  llvm::GetElementPtrInst *gep =
      llvm::dyn_cast<llvm::GetElementPtrInst>(load_pointer);

  // If the store is not consecutive along the vectorizing dimension then
  // it has to be replicated.
  if (false == subscriptAnalysis->isConsecutive(gep, (int)(VectorizingDirectionCL))) {
    return replicateInst(loadInst);
  }

  unsigned int operands_number = gep->getNumOperands();
  llvm::Value *last_operand = gep->getOperand(operands_number - 1);
  last_operand = getVectorValue(last_operand);

  last_operand = irBuilder->CreateExtractElement(
      last_operand, irBuilder->getInt32(0), "extracted");

  // Create the new gep.
  llvm::GetElementPtrInst *new_gep =
      llvm::cast<llvm::GetElementPtrInst>(gep->clone());
  new_gep->setOperand(operands_number - 1, last_operand);
  load_pointer = irBuilder->Insert(new_gep);
  load_pointer->setName("load_ptr");
  //insert_sorted<llvm::Instruction>(toRemoveInsts, gep);
  toRemoveInsts.insert(gep);

  llvm::Type *vector_load_type =
      getVectorPointerType(loadInst->getPointerOperand()->getType());

  load_pointer =
      irBuilder->CreateBitCast(load_pointer, vector_load_type, "bitcast");

  // Insert the vector load instruction into the function.
  llvm::LoadInst *vector_load = new llvm::LoadInst(load_pointer);
  vector_load->setAlignment(loadInst->getAlignment());
  irBuilder->Insert(vector_load)->setName("vector_load");

  return vector_load;
}

// -----------------------------------------------------------------------------
llvm::Value *ThreadVectorizing::vectorizeStore(llvm::StoreInst *storeInst) {
  // Get the store pointer.
  llvm::Value *store_pointer = storeInst->getPointerOperand();
  llvm::GetElementPtrInst *gep =
      llvm::dyn_cast<llvm::GetElementPtrInst>(store_pointer);

  // If the store is not consecutive along the vectorizing dimension then
  // it has to be replicated.
  if (false == subscriptAnalysis->isConsecutive(gep, (int)(VectorizingDirectionCL))) {
    return replicateInst(storeInst);
  }

  unsigned int operands_number = gep->getNumOperands();
  llvm::Value *last_operand = gep->getOperand(operands_number - 1);
  last_operand = getVectorValue(last_operand);

  last_operand = irBuilder->CreateExtractElement(
      last_operand, irBuilder->getInt32(0), "extracted");

  // Create the new gep.
  llvm::GetElementPtrInst *new_gep =
      llvm::cast<llvm::GetElementPtrInst>(gep->clone());
  new_gep->setOperand(operands_number - 1, last_operand);
  new_gep = irBuilder->Insert(new_gep);
  new_gep->setName("store_ptr");
  store_pointer = new_gep;
  //insert_sorted<llvm::Instruction>(toRemoveInsts, gep);
  toRemoveInsts.insert(gep);

  // Get the store type.
  llvm::Type *scalar_store_type = new_gep->getPointerOperandType();

  // Create the vector store pointer type.
  llvm::Type *vector_store_type = getVectorPointerType(scalar_store_type);

  store_pointer =
      irBuilder->CreateBitCast(store_pointer, vector_store_type, "bitcast");

  // Get the value to store to memory.
  llvm::Value *to_store_value =
 getVectorValue(storeInst->getValueOperand());

  // Insert the vector store instruction into the function.
  llvm::StoreInst *vector_store =
      irBuilder->CreateStore(to_store_value, store_pointer, "vector_store");
  vector_store->setAlignment(storeInst->getAlignment());
  vector_store->setVolatile(storeInst->isVolatile());

  return vector_store;
}

// -----------------------------------------------------------------------------
llvm::SelectInst *
ThreadVectorizing::vectorizeSelect(llvm::SelectInst *select_inst) {
  llvm::Value *condition = select_inst->getOperand(0);

  bool varying_condition = false;
  if (llvm::Instruction *condition_inst =
          llvm::dyn_cast<llvm::Instruction>(condition)) {
    varying_condition = sdda->isDivergent(condition_inst);
  }

  if (true == varying_condition) {
    condition = getVectorValue(condition);
  }

  llvm::Value *first_operand = select_inst->getOperand(1);
  llvm::Value *second_operand = select_inst->getOperand(2);

  first_operand = getVectorValue(first_operand);
  second_operand = getVectorValue(second_operand);

  llvm::SelectInst *result = llvm::SelectInst::Create(
      condition, first_operand, second_operand, select_inst->getName());

  irBuilder->Insert(result)->setName(select_inst->getName());

  return result;
}

// -----------------------------------------------------------------------------
llvm::CmpInst *ThreadVectorizing::vectorizeCmp(llvm::CmpInst *cmp_inst) {
  // Get the compare operands.
  llvm::Value *first_operand = cmp_inst->getOperand(0);
  llvm::Value *second_operand = cmp_inst->getOperand(1);

  // Get the vector version of the operands.
  first_operand = getVectorValue(first_operand);
  second_operand = getVectorValue(second_operand);

  llvm::CmpInst *result = NULL;

  // Generate the vector version of the instruction.
  if (llvm::Instruction::FCmp == cmp_inst->getOpcode()) {
    result = new llvm::FCmpInst(cmp_inst->getPredicate(), first_operand,
                                second_operand);
  } else {
    result = new llvm::ICmpInst(cmp_inst->getPredicate(), first_operand,
                                second_operand);
  }

  irBuilder->Insert(result)->setName(cmp_inst->getName());
  return result;
}

// -----------------------------------------------------------------------------
llvm::CastInst *ThreadVectorizing::vectorizeCast(llvm::CastInst *cast_inst) {
  // Get the cast operand.
  llvm::Value *operand = cast_inst->getOperand(0);

  // Get the vector version of the operand.
  operand = getVectorValue(operand);

  llvm::Type *dest_type =
      llvm::VectorType::get(cast_inst->getType()->getScalarType(), width);

  // Generate the vector version of the instruction.
  llvm::CastInst *result =
      llvm::CastInst::Create(cast_inst->getOpcode(), operand, dest_type);
  irBuilder->Insert(result)->setName(cast_inst->getName());

  return result;
}

// -----------------------------------------------------------------------------
llvm::Value *ThreadVectorizing::replicateInst(llvm::Instruction *inst) {
  assert(false == inst->getType()->isVectorTy() &&
         "Cannot replicate instructions of vector type");

  //  llvm::LLVMContext &context = inst->getContext();
  ValueVector operands = getWidenedOperands(inst);

  bool is_inst_void = inst->getType()->isVoidTy();
  llvm::Value *result = NULL;

  if (false == is_inst_void) {
    result =
        llvm::UndefValue::get(llvm::VectorType::get(inst->getType(), width));
  }

  for (unsigned int vector_index = 0; vector_index < width; ++vector_index) {
    llvm::Instruction *cloned = inst->clone();

    // For each operand extract the scalar from the vector.
    for (unsigned int operand_index = 0, operand_end = inst->getNumOperands();
         operand_index != operand_end; ++operand_index) {
      llvm::Value *operand = operands[operand_index];

      // Param is a vector. Need to extract the first value.
      if (true == operand->getType()->isVectorTy()) {
        operand = irBuilder->CreateExtractElement(
            operand, irBuilder->getInt32(vector_index), "extracted");
      }

      // Set the operand.
      cloned->setOperand(operand_index, operand);
    }

    llvm::Twine cloned_name = "";

    if (true == inst->hasName()) {
      cloned_name = inst->getName() + ".replicated" + llvm::Twine(vector_index);
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
ValueVector ThreadVectorizing::getWidenedOperands(llvm::Instruction *inst) {
  // Generate the list of operands for the replicated instructions.
  ValueVector operands;
  operands.reserve(inst->getNumOperands());

  // Find all of the vector operands.
  for (unsigned int operand_index = 0, operand_end = inst->getNumOperands();
       operand_index != operand_end; ++operand_index) {

    llvm::Value *operand = inst->getOperand(operand_index);

    if (true == vectorMap.count(operand)) {
      operands.push_back(vectorMap[operand]);
    } else {
      operands.push_back(operand);
    }
  }

  return operands;
}

// -----------------------------------------------------------------------------
llvm::Type *
ThreadVectorizing::getVectorPointerType(llvm::Type *scalar_pointer_type) {
  // The input type must be a pointer type.
  llvm::PointerType *pointer_type =
      llvm::dyn_cast<llvm::PointerType>(scalar_pointer_type);
  assert(NULL != pointer_type &&
         "Cannot create a vector pointer without a pointer type");

  // Get the address space.
  unsigned int address_space = pointer_type->getAddressSpace();
  // Get the pointee type.
  llvm::Type *scalar_type = pointer_type->getElementType();

  // Create the type of the new vector store.
  llvm::Type *vector_store_type = llvm::VectorType::get(scalar_type, width);

  // Create the pointer type.
  llvm::PointerType *vector_pointer_type =
      llvm::PointerType::get(vector_store_type, address_space);

  return vector_pointer_type;
}

// -----------------------------------------------------------------------------
void ThreadVectorizing::removeScalarInsts() {
  // Go through the list of instructions to be removed.
  for (InstSet::iterator iter = toRemoveInsts.begin(),
                         iterEnd = toRemoveInsts.end();
       iter != iterEnd; ++iter) {
    llvm::Instruction *inst = *iter;
    // Remove the current instruction from the function.
    if (false == inst->getType()->isVoidTy()) {
      inst->replaceAllUsesWith(llvm::Constant::getNullValue(inst->getType()));
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
    llvm::Value *scalar_value = iter->first;
    // Get the vector placeholder.
    llvm::Value *placeholder_value = iter->second;
    // Get the vector value corresponding to the scalar value.
    llvm::Value *vector_value = vectorMap[scalar_value];
    // Replace the placeholder with the vector value.
    placeholder_value->replaceAllUsesWith(vector_value);
  }
}

// -----------------------------------------------------------------------------
void ThreadVectorizing::replicateRegion(DivergentRegion *region) {
  assert(dt->dominates(region->getHeader(), region->getExiting()) &&
         "Header does not dominates Exiting");
  assert(pdt->dominates(region->getExiting(), region->getHeader()) &&
         "Exiting does not post dominate Header");

  switch (divRegionOption) {
  case FullReplication: {
    replicateRegionClassic(region);
    break;
  }
  case TrueBranchMerging: {
    replicateRegionTrueMerging(region);
    break;
  }
  case FalseBranchMerging: {
    replicateRegionFalseMerging(region);
    break;
  }
  case FullMerging: {
    replicateRegionFullMerging(region);
    break;
  }
  }
}

//------------------------------------------------------------------------------
void ThreadVectorizing::replicateRegionClassic(DivergentRegion *region) {
  replicateRegionImpl(region);
}

//------------------------------------------------------------------------------
void ThreadVectorizing::replicateRegionImpl(DivergentRegion *region) {
  BasicBlock *pred = getPredecessor(region, loopInfo);
  BasicBlock *topInsertionPoint = region->getExiting();
  BasicBlock *bottomInsertionPoint = getExit(*region);

  ValueVector result;
  InstVector &aliveInsts = region->getAlive();
  result.reserve(aliveInsts.size());
  for (InstVector::iterator iter = aliveInsts.begin(),
                            iterEnd = aliveInsts.end();
       iter != iterEnd; ++iter) {
    result.push_back(llvm::UndefValue::get(
        llvm::VectorType::get((*iter)->getType(), width)));
  }

  // Replicate the region.
  for (unsigned int index = 0; index < width - 1; ++index) {
    Map valueMap;
    DivergentRegion *newRegion =
        region->clone(".cf" + Twine(index + 2), dt, pdt, valueMap);
    //applyCoarseningMap(*newRegion, index);

    // Connect the region to the CFG.
    changeBlockTarget(topInsertionPoint, newRegion->getHeader());
    changeBlockTarget(newRegion->getExiting(), bottomInsertionPoint);

    // Update the phi nodes of the newly inserted header.
    remapBlocksInPHIs(newRegion->getHeader(), pred, topInsertionPoint);
    // Update the phi nodes in the exit block.
    remapBlocksInPHIs(bottomInsertionPoint, topInsertionPoint,
                      newRegion->getExiting());

    topInsertionPoint = newRegion->getExiting();
    bottomInsertionPoint = getExit(*newRegion);

    // Set the ir just before the branch out of the region
    irBuilder->SetInsertPoint(newRegion->getExiting()->getTerminator());

    for (unsigned int aliveIndex = 0; aliveIndex < aliveInsts.size();
         ++aliveIndex) {
      Value *newValue = valueMap[aliveInsts[aliveIndex]];
      result[aliveIndex] = irBuilder->CreateInsertElement(
          result[aliveIndex], newValue, irBuilder->getInt32(index), "inserted");
    }

    delete newRegion;
  }

  for (unsigned int aliveIndex = 0; aliveIndex < aliveInsts.size();
       ++aliveIndex) {
    vectorMap[aliveInsts[aliveIndex]] = result[aliveIndex];
  }
}

void ThreadVectorizing::replicateRegionFullMerging(DivergentRegion *region) {}
void ThreadVectorizing::replicateRegionFalseMerging(DivergentRegion *region) {
  replicateRegionMerging(region, 1);
}
void ThreadVectorizing::replicateRegionTrueMerging(DivergentRegion *region) {
  replicateRegionMerging(region, 0);
}
void ThreadVectorizing::replicateRegionMerging(DivergentRegion *region,
                                               unsigned int branchIndex) {}

//------------------------------------------------------------------------------
char ThreadVectorizing::ID = 0;
static RegisterPass<ThreadVectorizing>
    X("tv", "OpenCL Thread Vectorizing Transformation Pass");
