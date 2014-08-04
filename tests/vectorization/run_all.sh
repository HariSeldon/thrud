#! /bin/bash


CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis
AXTOR=axtor
LIB_THRUD=$HOME/root/lib/libThrud.so
TMP_FILE=/tmp/tc_tmp${RANDOM}.cl

OCLDEF=$HOME/src/thrud/tools/scripts/opencl_spir.h
OPTIMIZATION=-O0

function runTest {
  INPUT_FILE=$1
  KERNEL_NAME=$2
  VECTOR_WIDTH=$3
  VECTOR_DIRECTION=$4

  RED='\e[0;31m'
  GREEN='\e[0;32m'
  BLANK='\e[0m'

  OUTPUT_STRING="runTest $INPUT_FILE $KERNEL_NAME $VECTOR_WIDTH $VECTOR_DIRECTION"

  $CLANG -x cl \
         -target spir \
         -include ${OCLDEF} \
         ${OPTIMIZATION} \
         ${INPUT_FILE} \
         -S -emit-llvm -fno-builtin -o - | \
  $OPT -mem2reg -instnamer \
       -load $LIB_THRUD -be -tv \
       -vectorizing-width ${VECTOR_WIDTH} \
       -vectorizing-direction ${VECTOR_DIRECTION} \
       -div-region-mgt classic \
       -kernel-name ${KERNEL_NAME} \
       -o /dev/null 2> /dev/null
  if [ $? == 0 ] 
  then 
    echo -e "${GREEN}runTest $OUTPUT_STRING Ok!${BLANK}"
  else
    echo -e "${RED}runTest $OUTPUT_STRING Error${BLANK}"
  fi 
}

# List all test cases.

runTest kernels/memset.cl memset1 4 0 
runTest kernels/memset.cl memset2 4 0 
runTest kernels/memset.cl memset2 4 1
runTest kernels/bruno_examples.cl vectorTest 4 0
runTest kernels/bruno_examples.cl vectorTest 4 1
runTest kernels/bruno_examples.cl vectorTest2 4 0
runTest kernels/bruno_examples.cl vectorTest2 4 1
runTest kernels/mm.cl mm 4 0
runTest kernels/mm.cl mm 4 1
runTest kernels/mt.cl mt 4 0
runTest kernels/mt.cl mt 4 1
