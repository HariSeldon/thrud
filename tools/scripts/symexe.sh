#! /bin/bash

CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis
LLVM_LINK=llvm-link
AXTOR=axtor

LIB_THRUD=$HOME/root/lib/libThrud.so
THRUD_DIR=$HOME/src/thrud/tools/scripts

TMP_NAME=$THRUD_DIR/tmp.ll
#RANDOM_FILE=/tmp/tc_tmp${RANDOM}.cl
#OUTPUT_FILE=/tmp/tc_output${RANDOM}.cl

OCLDEF=$THRUD_DIR/opencl_spir.h
TARGET=spir

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

## Compile kernel.
#$CLANG -x cl \
#       -target $TARGET \
#       -include $OCLDEF \
#       -O0 \
#       $INPUT_FILE \
#       -S -emit-llvm -fno-builtin -o - | \
#$OPT -mem2reg \
#     -inline -inline-threshold=10000 \
#     -instnamer -load ${LIB_THRUD} -be -tc \
#     -coarsening-factor ${COARSENING_FACTOR} \
#     -coarsening-direction ${COARSENING_DIRECTION} \
#     -coarsening-stride ${COARSENING_STRIDE} -o - |
#${LLVM_DIS} -o ${RANDOM_FILE}
#${AXTOR} ${RANDOM_FILE} -m OCL -o ${OUTPUT_FILE} &>  /dev/null
#
#$CLANG -x cl \
#       -target $TARGET \
#       -include $OCLDEF \
#       -O0 \
#       $OUTPUT_FILE \
#       -S -emit-llvm -fno-builtin -o - | 
#$OPT -instnamer \
#     -mem2reg \
#     -inline -inline-threshold=100000 \
#     -load $LIB_THRUD -symbolic-execution -symbolic-kernel-name $KERNEL_NAME \
#     -o /dev/null

# Compile kernel.
$CLANG -x cl \
       -target $TARGET \
       -include $OCLDEF \
       -O0 \
       $INPUT_FILE \
       -S -emit-llvm -fno-builtin -o - | \
$OPT -mem2reg \
     -inline -inline-threshold=10000 \
     -instnamer -load ${LIB_THRUD} -be -tc \
     -coarsening-factor ${COARSENING_FACTOR} \
     -coarsening-direction ${COARSENING_DIRECTION} \
     -coarsening-stride ${COARSENING_STRIDE} -o - |
$OPT -instnamer \
     -mem2reg \
     -inline -inline-threshold=100000 \
     -load $LIB_THRUD -symbolic-execution -symbolic-kernel-name $KERNEL_NAME \
     -o /dev/null

## Delete tmp files.
#rm -f $RANDOM_FILE
#rm -f $OUTPUT_FILE
