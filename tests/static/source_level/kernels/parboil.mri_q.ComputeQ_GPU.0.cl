#define PI   3.1415926535897932384626433832795029f
#define PIx2 6.2831853071795864769252867665590058f

#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
#define K_ELEMS_PER_GRID 2048

#define KERNEL_PHI_MAG_THREADS_PER_BLOCK 256 /* 512 */
#define KERNEL_Q_THREADS_PER_BLOCK 256
#define KERNEL_Q_K_ELEMS_PER_GRID 1024

struct kValues {
  float Kx;
  float Ky;
  float Kz;
  float PhiMag;
};

#define NC 1

__kernel void
ComputePhiMag_GPU(__global float* phiR, __global float* phiI, __global float* phiMag, int numK) {
  int indexK = get_global_id(0);
  if (indexK < numK) {
    float real = phiR[indexK];
    float imag = phiI[indexK];
    phiMag[indexK] = real*real + imag*imag;
  }
}

__kernel void
ComputeQ_GPU(int numK, int kGlobalIndex,
	     __global float* x, __global float* y, __global float* z,
	     __global float* Qr, __global float* Qi, __global struct kValues* ck) 
{
  float sX;
  float sY;
  float sZ;
  float sQr;
  float sQi;

  int xIndex = get_group_id(0) * get_local_size(0) + get_local_id(0);

  // Read block's X values from global mem to shared mem
  sX = x[xIndex];
  sY = y[xIndex];
  sZ = z[xIndex];
  sQr = Qr[xIndex];
  sQi = Qi[xIndex];

  for (int kIndex = 0; (kIndex < KERNEL_Q_K_ELEMS_PER_GRID) && (kGlobalIndex < numK);
       kIndex ++, kGlobalIndex ++) {
    float expArg = PIx2 * (ck[kIndex].Kx * sX + ck[kIndex].Ky * sY + ck[kIndex].Kz * sZ);
    sQr += ck[kIndex].PhiMag * cos(expArg);
    sQi += ck[kIndex].PhiMag * sin(expArg);
  }

  Qr[xIndex] = sQr;
  Qi[xIndex] = sQi;
 
//  float sX[NC];
//  float sY[NC];
//  float sZ[NC];
//  float sQr[NC];
//  float sQi[NC];
//
//  #pragma unroll
//  for (int tx = 0; tx < NC; tx++) {
//    int xIndex = get_group_id(0) * get_local_size(0) + NC * get_local_id(0) + tx;
//
//    sX[tx] = x[xIndex];
//    sY[tx] = y[xIndex];
//    sZ[tx] = z[xIndex];
//    sQr[tx] = Qr[xIndex];
//    sQi[tx] = Qi[xIndex];
//  }
//
//  // Loop over all elements of K in constant mem to compute a partial value
//  // for X.
//  int kIndex = 0;
//  for (; (kIndex < KERNEL_Q_K_ELEMS_PER_GRID) && (kGlobalIndex < numK);
//       kIndex ++, kGlobalIndex ++) {
//    float kx = ck[kIndex].Kx;
//    float ky = ck[kIndex].Ky;
//    float kz = ck[kIndex].Kz;
//    float pm = ck[kIndex].PhiMag;
//
//    #pragma unroll
//    for (int tx = 0; tx < NC; tx++) {
//      float expArg = PIx2 *
//                   (kx * sX[tx] +
//                    ky * sY[tx] +
//                    kz * sZ[tx]);
//      sQr[tx] += pm * cos(expArg);
//      sQi[tx] += pm * sin(expArg);
//    }
//  }
//
//  #pragma unroll
//  for (int tx = 0; tx < NC; tx++) {
//    int xIndex = get_group_id(0) * get_local_size(0) + NC * get_local_id(0) + tx;
//    Qr[xIndex] = sQr[tx];
//    Qi[xIndex] = sQi[tx];
//  }
}
