#! /bin/bash

CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis
AXTOR=axtor
LIB_THRUD=$HOME/root/lib/libThrud.so

INPUT_FILE=$1
KERNEL_NAME=$2
COARSENING_DIRECTION=$3
COARSENING_FACTOR=$4
COARSENING_STRIDE=$5
DIV_REGION=$6

OCLDEF=$HOME/src/thrud/tools/scripts/ocldef_intel.h
OPTIMIZATION=-O0

$CLANG -x cl \
       -target nvptx \
       -include ${OCLDEF} \
       ${OPTIMIZATION} \
       ${INPUT_FILE} \
       -S -emit-llvm -fno-builtin -o - |
$OPT ${TMP_LL_FILE} \
    -mem2reg -instnamer \
    -load $LIB_THRUD \
    -be -tc \
    -coarsening-factor ${COARSENING_FACTOR} \
    -coarsening-direction ${COARSENING_DIRECTION} \
    -coarsening-stride ${COARSENING_STRIDE} \
    -div-region-mgt=${DIV_REGION} -o - |
${LLVM_DIS} -o /dev/null
