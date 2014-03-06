#! /bin/bash

CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis

INPUT_FILE=$1
KERNEL_NAME=$2

OCLDEF=$HOME/src/thrud/tools/scripts/ocldef_intel.h
OPTIMIZATION=-O3

$CLANG -x cl \
       -target nvptx \
       -include ${OCLDEF} \
       -O3 \
       ${INPUT_FILE} \
       -S -emit-llvm -fno-builtin -o - |
$OPT -instnamer \
     -mem2reg \
     -loop-unroll -unroll-threshold=1000 \
     -inline -inline-threshold=10000 \
     -O3 \
     -o - |
$LLVM_DIS -o -  
