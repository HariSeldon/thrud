#! /bin/bash

CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis
AXTOR=axtor
LIB_THRUD=$HOME/root/lib/libThrud.so

if [ $# -ne 5 ]
then
  echo "Must specify: input file, kernel name, cd, cf, st"
exit 1;
fi

INPUT_FILE=$1
KERNEL_NAME=$2
COARSENING_DIRECTION=$3
COARSENING_FACTOR=$4
COARSENING_STRIDE=$5

OCLDEF=$HOME/src/thrud/tools/scripts/ocldef.h
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
     -inline -inline-threshold=10000 \
     -instnamer -load ${LIB_THRUD} -be -tc \
     -coarsening-factor ${COARSENING_FACTOR} \
     -coarsening-direction ${COARSENING_DIRECTION} \
     -coarsening-stride ${COARSENING_STRIDE} -o - |
${LLVM_DIS} -o ${TMP_FILE}
${AXTOR} ${TMP_FILE} -m OCL -o ${OUTPUT_FILE} &>  /dev/null

rm ${TMP_FILE}

$CLANG -x cl \
       -target nvptx \
       -include ${OCLDEF} \
       -O3 \
       ${OUTPUT_FILE} \
       -S -emit-llvm -fno-builtin -o - | \
$OPT -instnamer \
     -mem2reg \
     -inline -inline-threshold=10000 \
     -O3 -load ${LIB_THRUD} -opencl-loop-instcount -count-loop-kernel-name ${KERNEL_NAME} \
     -o /dev/null

rm ${OUTPUT_FILE}
