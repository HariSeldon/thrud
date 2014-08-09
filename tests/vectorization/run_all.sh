#! /bin/bash


CLANG=clang
OPT=opt
LLVM_DIS=llvm-dis
AXTOR=axtor
LIB_THRUD=$HOME/root/lib/libThrud.so
TMP_FILE=/tmp/tc_tmp${RANDOM}.cl

OCLDEF=$HOME/src/thrud/tools/scripts/opencl_spir.h
OPTIMIZATION=-O0

function runTest {
  INPUT_FILE=$1
  KERNEL_NAME=$2
  VECTOR_WIDTH=$3
  VECTOR_DIRECTION=$4

  RED='\e[0;31m'
  GREEN='\e[0;32m'
  BLANK='\e[0m'

  OUTPUT_STRING="runTest $INPUT_FILE $KERNEL_NAME $VECTOR_WIDTH $VECTOR_DIRECTION"

  $CLANG -x cl \
         -target spir \
         -include ${OCLDEF} \
         ${OPTIMIZATION} \
         ${INPUT_FILE} \
         -S -emit-llvm -fno-builtin -o - | \
  $OPT -mem2reg -instnamer \
       -load $LIB_THRUD -structurize-cfg -simplifycfg -be -tv \
       -vectorizing-width ${VECTOR_WIDTH} \
       -vectorizing-direction ${VECTOR_DIRECTION} \
       -div-region-mgt classic \
       -kernel-name ${KERNEL_NAME} \
       -o /dev/null 2> /dev/null
  if [ $? == 0 ] 
  then 
    echo -e "${GREEN}runTest $OUTPUT_STRING Ok!${BLANK}"
  else
    echo -e "${RED}runTest $OUTPUT_STRING Error${BLANK}"
  fi 
}

# List all test cases.

#runTest kernels/memset.cl memset1 4 0 
#runTest kernels/memset.cl memset2 4 0 
#runTest kernels/memset.cl memset2 4 1
#runTest kernels/bruno_examples.cl vectorTest 4 0
#runTest kernels/bruno_examples.cl vectorTest 4 1
#runTest kernels/bruno_examples.cl vectorTest2 4 0
#runTest kernels/bruno_examples.cl vectorTest2 4 1
#runTest kernels/mm.cl mm 4 0
#runTest kernels/mm.cl mm 4 1
#runTest kernels/mt.cl mt 4 0
#runTest kernels/mt.cl mt 4 1
#runTest kernels/2DConvolution.cl Convolution2D_kernel 4 0
#runTest kernels/2DConvolution.cl Convolution2D_kernel 4 1
#runTest kernels/2mm.cl mm2_kernel1 4 0
#runTest kernels/2mm.cl mm2_kernel1 4 1
#runTest kernels/3DConvolution.cl Convolution3D_kernel 4 0
#runTest kernels/3DConvolution.cl Convolution3D_kernel 4 1
#runTest kernels/3mm.cl mm3_kernel1 4 0
#runTest kernels/3mm.cl mm3_kernel1 4 1
#runTest kernels/atax.cl atax_kernel1 4 0
#runTest kernels/atax.cl atax_kernel2 4 0
#runTest kernels/bicg.cl bicgKernel1 4 0
#runTest kernels/correlation.cl mean_kernel 4 0
#runTest kernels/correlation.cl std_kernel 4 0
#runTest kernels/correlation.cl reduce_kernel 4 0
#runTest kernels/correlation.cl reduce_kernel 4 1
#runTest kernels/correlation.cl corr_kernel 4 0
#runTest kernels/correlation.cl corr_kernel 4 1
#runTest kernels/covariance.cl mean_kernel 4 0
#runTest kernels/covariance.cl reduce_kernel 4 0
#runTest kernels/covariance.cl reduce_kernel 4 1
#runTest kernels/covariance.cl covar_kernel 4 0
#runTest kernels/fdtd2d.cl fdtd_kernel1 4 0
#runTest kernels/fdtd2d.cl fdtd_kernel1 4 1
#runTest kernels/fdtd2d.cl fdtd_kernel2 4 0
#runTest kernels/fdtd2d.cl fdtd_kernel2 4 1
#runTest kernels/fdtd2d.cl fdtd_kernel3 4 0
#runTest kernels/fdtd2d.cl fdtd_kernel3 4 1
#runTest kernels/gemm.cl gemm 4 0
#runTest kernels/gemm.cl gemm 4 1
#runTest kernels/gesummv.cl gesummv_kernel 4 0
#runTest kernels/gramschmidt.cl gramschmidt_kernel1 4 0
#runTest kernels/gramschmidt.cl gramschmidt_kernel2 4 0
#runTest kernels/gramschmidt.cl gramschmidt_kernel3 4 0
#runTest kernels/mm2metersKernel.cl mm2metersKernel 4 0
#runTest kernels/mm2metersKernel.cl mm2metersKernel 4 1
#runTest kernels/mvt.cl mvt_kernel1 4 0
#runTest kernels/mvt.cl mvt_kernel1 4 1
#runTest kernels/syr2k.cl syr2k_kernel 4 0
#runTest kernels/syr2k.cl syr2k_kernel 4 1
#runTest kernels/syrk.cl syrk_kernel 4 0
#runTest kernels/syrk.cl syrk_kernel 4 1
#runTest kernels/sad_kernel.cl mb_sad_calc 4 0
#runTest kernels/sad_kernel.cl mb_sad_calc 4 1
#runTest kernels/sad_kernel.cl larger_sad_calc_8 4 0
#runTest kernels/sad_kernel.cl larger_sad_calc_8 4 1
#runTest kernels/sad_kernel.cl larger_sad_calc_16 4 0
#runTest kernels/sad_kernel.cl larger_sad_calc_16 4 1
#runTest kernels/kernel.cl gen_hists 4 0
#runTest kernels/histo_final.cl histo_final_kernel 4 0
#runTest kernels/histo_intermediates.cl histo_intermediates_kernel 4 0
#runTest kernels/histo_intermediates.cl histo_intermediates_kernel_compat 4 0
#runTest kernels/histo_main.cl histo_main_kernel 4 0
#runTest kernels/GPU_kernels.cl binning_kernel 4 0
#runTest kernels/GPU_kernels.cl reorder_kernel 4 0
#runTest kernels/GPU_kernels.cl gridding_GPU 4 0
#runTest kernels/GPU_kernels.cl gridding_GPU 4 1
#runTest kernels/GPU_kernels.cl gridding_GPU 4 2
#runTest kernels/scanLargeArray.cl scan_L1_kernel 4 0 
#runTest kernels/scanLargeArray.cl scan_inter1_kernel 4 0 
#runTest kernels/scanLargeArray.cl scan_inter2_kernel 4 0 
#runTest kernels/scanLargeArray.cl uniformAdd 4 0 
#runTest kernels/sort.cl splitSort 4 0
#runTest kernels/sort.cl splitRearrange 4 0
#runTest kernels/spmv.cl spmv_jds_naive 4 0
#runTest kernels/stencil.cl naive_kernel 4 0 
#runTest kernels/stencil.cl naive_kernel 4 1 
runTest kernels/stencil.cl naive_kernel 4 2 
