#! /bin/bash

CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis
AXTOR=axtor
LIB_THRUD=$HOME/root/lib/libThrud.so

INPUT_FILE=$1

OCLDEF=$HOME/src/thrud/tools/scripts/ocldef_intel.h
OPTIMIZATION=-O0

$CLANG -x cl \
       -target spir \
       -include ${OCLDEF} \
       ${OPTIMIZATION} \
       ${INPUT_FILE} \
       -S -emit-llvm -fno-builtin -o - | \
$OPT -mem2reg -instnamer \
     -load $LIB_THRUD -be -tv \
     -vectorizing-width 4 \
     -vectorizing-direction 0 \
     -div-region-mgt=classic \
     -o - | \
#${LLVM_DIS} -o -  
${OPT} -dot-cfg
