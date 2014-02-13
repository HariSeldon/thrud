#! /bin/bash

CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis
LLVM_LINK=llvm-link

LIB_THRUD=$HOME/root/lib/libThrud.so
THRUD_DIR=$HOME/src/thrud/tools/scripts

INPUT_FILE=$1
KERNEL_NAME=$2

BRIDGE_NAME=$THRUD_DIR/bridge
LIBDEVICE_NAME=$THRUD_DIR/libdevice.compute_35.10.ll
TMP_NAME=$THRUD_DIR/tmp.ll
LINKER_OUTPUT=$THRUD_DIR/linked.bc

OCLDEF=$THRUD_DIR/ocldef_intel.h
OPTIMIZATION=-O3

if [ $# -ne 2 ]
then
  echo "Must specify input file and kernel name"
  exit 1;
fi

# Compile bridge.
$CLANG -target nvptx \
        ${BRIDGE_NAME}.cpp \
       -fno-builtin \
       -S -emit-llvm \
       -o ${BRIDGE_NAME}.ll

# Compile kernel.
$CLANG -x cl \
       -target nvptx \
       -include $OCLDEF \
       -O0 \
       $INPUT_FILE \
       -S -emit-llvm -fno-builtin -o $TMP_NAME

# Link kernel, bridge and libdevice.
$LLVM_LINK ${BRIDGE_NAME}.ll $TMP_NAME $LIBDEVICE_NAME -o $LINKER_OUTPUT 

# Optimize.
$OPT -internalize -internalize-public-api-list=$KERNEL_NAME \
     -nvvm-reflect-list=__CUDA_FTZ=0 \
     -nvvm-reflect \
     $OPTIMIZATION $LINKER_OUTPUT -o - | \
$OPT -instnamer \
     -mem2reg \
     -inline -inline-threshold=10000 \
     $OPTIMIZATION \
     -load $LIB_THRUD -opencl-instcount -count-kernel-name $KERNEL_NAME -coarsening-direction 0 \
     -o /dev/null

# Delete tmp files.
rm ${BRIDGE_NAME}.ll
rm $TMP_NAME
rm $LINKER_OUTPUT
