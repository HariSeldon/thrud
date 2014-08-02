#! /bin/bash

CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis
AXTOR=axtor
LIB_THRUD=$HOME/root/lib/libThrud.so
TMP_FILE=/tmp/tc_tmp${RANDOM}.cl

INPUT_FILE=$1

OCLDEF=$HOME/src/thrud/tools/scripts/opencl_spir.h
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
     -vectorizing-direction 1 \
     -div-region-mgt=classic \
     -o - | \
${OPT} -O3 -o - | \
${LLVM_DIS} -o ${TMP_FILE}
${AXTOR} ${TMP_FILE} -m OCL -o ${OUTPUT_FILE}
rm ${TMP_FILE}

#$CLANG -x cl \
#       -target spir \
#       -include ${OCLDEF} \
#       ${OPTIMIZATION} \
#       ${INPUT_FILE} \
#       -S -emit-llvm -fno-builtin -o - | \
#$OPT -mem2reg -instnamer \
#     -load $LIB_THRUD -be -tv \
#     -vectorizing-width 4 \
#     -vectorizing-direction 1 \
#     -div-region-mgt=classic \
#     -o - | \
#${LLVM_DIS} -o -
