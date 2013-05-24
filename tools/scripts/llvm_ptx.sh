#! /bin/bash

CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis
LLC=llc

INPUT_FILE=$1

OCLDEF=/home/s1158370/src/thrud/tools/scripts/ocldef_intel.h
OPTIMIZATION=-O3

#       -include ${OCLDEF} \
#       -target nvptx \
$CLANG -target nvptx-nvidiacl \
       -I /home/s1158370/src/libclc/generic/include \
       -I /home/s1158370/src/libclc/ptx/include \
       -include clc/clc.h \
       -Xclang -mlink-bitcode-file \
       -Xclang /home/s1158370/src/libclc/nvptx--nvidiacl/lib/builtins.bc \
       -Dcl_clang_storage_class_specifiers \
       -O3 \
       ${INPUT_FILE} \
       -S -o - 
