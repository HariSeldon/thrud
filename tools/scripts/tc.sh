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
       -target nvptx \
       -include ${OCLDEF} \
       ${OPTIMIZATION} \
       ${INPUT_FILE} \
       -S -emit-llvm -fno-builtin -o - | \
$OPT -mem2reg -instnamer \
     -load $LIB_THRUD -be -tc \
     -coarsening-factor 16 \
     -coarsening-direction 0 \
     -coarsening-stride 1 \
     -div-region-mgt=classic \
     -o - | \
#${LLVM_DIS} -o -  
${OPT} -dot-cfg-only
