#! /bin/bash

CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis
AXTOR=axtor
LIB_THREAD_COARSENING=/home/s1158370/root/lib/libThreadCoarsening.so

INPUT_FILE=$1
COARSENING_DIRECTION=$2
COARSENING_FACTOR=$3
COARSENING_STRIDE=$4
OUTPUT_FILE=$5

OCLDEF=/home/s1158370/src/thrud/tools/scripts/ocldef_intel.h
OPTIMIZATION=-O0
TMP_FILE=/tmp/tc_tmp${RANDOM}.cl

$CLANG -x cl \
       -target nvptx \
       -include ${OCLDEF} \
       ${OPTIMIZATION} \
       ${INPUT_FILE} \
       -S -emit-llvm -fno-builtin -o - | \
$OPT -mem2reg \
     -instnamer -load $LIB_THREAD_COARSENING -be -tc \
     -coarsening-factor ${COARSENING_FACTOR} \
     -coarsening-direction ${COARSENING_DIRECTION} \
     -coarsening-stride ${COARSENING_STRIDE} -o - | \
${LLVM_DIS} -o ${TMP_FILE}
${AXTOR} ${TMP_FILE} -m OCL -o ${OUTPUT_FILE} 

rm ${TMP_FILE}
