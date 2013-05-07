#! /bin/bash

CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis

INPUT_FILE=$1
KERNEL_NAME=$2
COARSENING_DIRECTION=$3

OCLDEF=/home/s1158370/src/myclash/axtor_scripts/ocldef.h
OPTIMIZATION=-O3

$CLANG -x cl \
       -target nvptx \
       -include ${OCLDEF} \
       ${INPUT_FILE} \
       -S -emit-llvm -fno-builtin -o - |
$OPT -mem2reg \
     -inline -inline-threshold=10000 \
     -O3 \
     -o - |
$LLVM_DIS -o -  
