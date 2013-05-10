#! /bin/bash

CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis
AXTOR=axtor

INPUT_FILE=$1
KERNEL_NAME=$2
COARSENING_DIRECTION=$3
COARSENING_FACTOR=$4
COARSENING_STRIDE=$5

OCLDEF=/home/s1158370/src/thrud/tools/scripts/ocldef_intel.h
OPTIMIZATION=-O0
TMP_FILE=/tmp/tc_tmp${RANDOM}.cl
OUTPUT_FILE=/tmp/tc_output${RANDOM}.cl

$CLANG -x cl \
       -target nvptx \
       -include ${OCLDEF} \
       ${OPTIMIZATION} \
       ${INPUT_FILE} \
       -S -emit-llvm -fno-builtin -o - | \
$OPT -mem2reg \
     -stats \
     -inline -inline-threshold=10000 \
     -instnamer -load ~/root/lib/LLVMTC.so -be -tc \
     -coarsening-factor ${COARSENING_FACTOR} \
     -coarsening-direction ${COARSENING_DIRECTION} \
     -coarsening-stride ${COARSENING_STRIDE} -o - | \
${LLVM_DIS} -o ${TMP_FILE}
${AXTOR} ${TMP_FILE} -m OCL -o ${OUTPUT_FILE} 

rm ${TMP_FILE}

$CLANG -x cl \
       -target nvptx \
       -include ${OCLDEF} \
       ${OPTIMIZATION} \
       ${OUTPUT_FILE} \
       -S -emit-llvm -fno-builtin -o - | \
$OPT -mem2reg \
     -stats \
     -load ~/root/lib/LLVMTC.so -opencl_instcount -count-kernel-name ${KERNEL_NAME} \
     -o - > /dev/null

rm ${OUTPUT_FILE}
