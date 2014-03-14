__kernel void verticalMultiplication(const __global float* M,
                                     const __global float* V,
                                     uint width, uint height,
                                     __global float* W)
{
    uint y = get_global_id(0);
    float dotProduct = 0;
    for (int x = 0; x < width; ++x) {
      dotProduct += M[y + width * x] * V[x];
    }
    W[y] = dotProduct;
}
