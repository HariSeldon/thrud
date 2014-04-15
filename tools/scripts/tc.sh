#! /bin/bash

CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis
AXTOR=axtor
LIB_THRUD=$HOME/root/lib/libThrud.so
OCL_DEF=opencl_spir.h
TARGET=spir

INPUT_FILE=$1
OPTIMIZATION=-O0

$CLANG -x cl \
       -target $TARGET \
       -include $OCL_DEF \
       ${OPTIMIZATION} \
       ${INPUT_FILE} \
       -S -emit-llvm -fno-builtin -o - | \
$OPT -mem2reg -instnamer \
     -load $LIB_THRUD -be -tc \
     -coarsening-factor 2 \
     -coarsening-direction 0 \
     -coarsening-stride 1 \
     -div-region-mgt=merge-true \
     -o - | \
${LLVM_DIS} -o -  

#${OPT} -dot-cfg-only
