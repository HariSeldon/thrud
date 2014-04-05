#! /bin/bash

CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis

INPUT_FILE=$1
KERNEL_NAME=$2

OCLDEF=$HOME/src/thrud/tools/scripts/opencl_spir.h
OPTIMIZATION=-O3

$CLANG -x cl \
       -O0 \
       -target spir \
       -include ${OCLDEF} \
       ${INPUT_FILE} \
       -S -emit-llvm -fno-builtin -o - |
$OPT -instnamer \
     -mem2reg \
     -o - |
$LLVM_DIS -o -  
