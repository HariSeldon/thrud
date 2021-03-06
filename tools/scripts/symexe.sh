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

if [ $# -ne 11 ]
then
  echo "Must specify: input file, kernel name, cd, cf, st"
exit 1;
fi

inputFile=$1
kernelName=$2
numberOfGroupsX=$3
numberOfGroupsY=$4
numberOfGroupsZ=$5
localX=$6
localY=$7
localZ=$8
coarseningDirection=$9
coarseningFactor=${10}
coarseningStride=${11}

# Compile kernel.
$CLANG -x cl \
       -target $TARGET \
       -include $OCLDEF \
       -O0 \
       $inputFile \
       -S -emit-llvm -fno-builtin -o - | \
$OPT -mem2reg \
     -inline -inline-threshold=10000 \
     -instnamer -load ${LIB_THRUD} -be -tc \
     -coarsening-factor ${coarseningFactor} \
     -coarsening-direction ${coarseningDirection} \
     -coarsening-stride ${coarseningStride} -o - |
$OPT -instnamer \
     -mem2reg \
     -inline -inline-threshold=100000 \
     -load $LIB_THRUD -symbolic-execution -symbolic-kernel-name ${kernelName} \
     -localSizeX ${localX} -localSizeY ${localY} -localSizeZ ${localZ} \
     -numberOfGroupsX ${numberOfGroupsX} -numberOfGroupsY ${numberOfGroupsY} -numberOfGroupsZ ${numberOfGroupsZ} \
     -o /dev/null
