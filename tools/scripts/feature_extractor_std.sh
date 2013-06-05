#! /bin/bash

CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis

LIB_THRUD=$HOME/root/lib/libThrud.so

INPUT_FILE=$1
KERNEL_NAME=$2

if [ $# -ne 2 ]
then
  echo "Must specify input file and kernel name"
  exit 1;
fi


OCLDEF=$HOME/src/thrud/tools/scripts/ocldef_intel.h
OPTIMIZATION=-O3

$CLANG -x cl \
       -target nvptx \
       -include ${OCLDEF} \
       -O0 \
       ${INPUT_FILE} \
       -S -emit-llvm -fno-builtin -o - | \
$OPT -instnamer \
     -mem2reg \
     -inline -inline-threshold=10000 \
     -O3 -load $LIB_THRUD -opencl-instcount -count-kernel-name $KERNEL_NAME \
     -o /dev/null
