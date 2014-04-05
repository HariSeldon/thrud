#! /bin/bash

CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis
AXTOR=axtor
LIB_THRUD=$HOME/root/lib/libThrud.so
OCL_DEF=opencl_spir.h
#OCL_DEF=ocldef_intel.h
TARGET=spir
#TARGET=spir

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
     -coarsening-factor 32 \
     -coarsening-direction 0 \
     -coarsening-stride $2 \
     -div-region-mgt=classic \
     -o - | \
$PT -instnamer -mem2reg -inline \
    -inline-threshold=10000  \
    -load ~/root/lib/libThrud.so \
    -opencl-instcount -count-kernel-name mt \
    -coarsening-direction 0 -o /dev/null
