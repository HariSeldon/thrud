#! /bin/bash

CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis
LLC=llc

INPUT_FILE=$1

OCLDEF=$HOME/src/thrud/tools/scripts/ocldef_intel.h
OPTIMIZATION=-O0

$CLANG -target spir--nvidiacl \
       -I $HOME/src/libclc/generic/include \
       -I $HOME/src/libclc/ptx/include \
       -include clc/clc.h \
       -Xclang -mlink-bitcode-file \
       -Xclang $HOME/src/libclc/spir--nvidiacl/lib/builtins.bc \
       -Dcl_clang_storage_class_specifiers \
       ${OPTIMIZATION} \
       ${INPUT_FILE} \
       -S -o - 
