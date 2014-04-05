#! /bin/bash

CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis
LLVM_LINK=llvm-link
AXTOR=axtor

LIB_THRUD=$HOME/root/lib/libThrud.so
THRUD_DIR=$HOME/src/thrud/tools/scripts

BRIDGE_NAME=$THRUD_DIR/bridge
LIBDEVICE_NAME=$THRUD_DIR/libdevice.compute_35.10.ll
TMP_NAME=$THRUD_DIR/tmp.ll
LINKER_OUTPUT=$THRUD_DIR/linked.bc
EXTRACTOR_INPUT=$THRUD_DIR/extractor_input.bc
RANDOM_FILE=/tmp/tc_tmp${RANDOM}.cl
OUTPUT_FILE=/tmp/tc_output${RANDOM}.cl

OCLDEF=$THRUD_DIR/opencl_spir.h
OPTIMIZATION=-O3
TARGET=spir

if [ $# -ne 6 ]
then
  echo "Must specify: math extraction, input file, kernel name, cd, cf, st"
exit 1;
fi

NVVM_MATH_FUNCTIONS=$1
INPUT_FILE=$2
KERNEL_NAME=$3
COARSENING_DIRECTION=$4
COARSENING_FACTOR=$5
COARSENING_STRIDE=$6

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
${LLVM_DIS} -o ${RANDOM_FILE}
${AXTOR} ${RANDOM_FILE} -m OCL -o ${OUTPUT_FILE} &>  /dev/null

$CLANG -x cl \
       -target $TARGET \
       -include $OCLDEF \
       -O0 \
       $OUTPUT_FILE \
       -S -emit-llvm -fno-builtin -o $TMP_NAME

# If expand math libraries is enabled.
if [ $NVVM_MATH_FUNCTIONS -eq 1 ]
then
  # Compile bridge.
  $CLANG -target $TARGET \
          ${BRIDGE_NAME}.cpp \
         -fno-builtin \
         -S -emit-llvm \
         -o ${BRIDGE_NAME}.ll

  # Link kernel, bridge and libdevice.
  $LLVM_LINK ${BRIDGE_NAME}.ll $TMP_NAME $LIBDEVICE_NAME -o $LINKER_OUTPUT

  # Optimize.
  $OPT -internalize -internalize-public-api-list=$KERNEL_NAME \
       -nvvm-reflect-list=__CUDA_FTZ=0 \
       -nvvm-reflect \
       $OPTIMIZATION $LINKER_OUTPUT -o $EXTRACTOR_INPUT
#  $LLVM_DIS $EXTRACTOR_INPUT -o -
  $OPT $EXTRACTOR_INPUT \
       -instnamer \
       -mem2reg \
       -inline -inline-threshold=10000 \
       $OPTIMIZATION \
       -load $LIB_THRUD -opencl-loop-instcount -count-loop-kernel-name $KERNEL_NAME -coarsening-direction 0 \
       -o /dev/null
else
  $OPT $TMP_NAME \
       -instnamer \
       -mem2reg \
       -inline -inline-threshold=10000 \
       $OPTIMIZATION \
       -load $LIB_THRUD -opencl-loop-instcount -count-loop-kernel-name $KERNEL_NAME -coarsening-direction 0 \
       -o /dev/null
fi

# Delete tmp files.
rm -f ${BRIDGE_NAME}.ll
rm -f $TMP_NAME
rm -f $LINKER_OUTPUT
rm -f $EXTRACTOR_INPUT
rm -f $RANDOM_FILE
rm -f $OUTPUT_FILE
