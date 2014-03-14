__kernel void MatVecMulUncoalesced1(const __global float* M,
                                    const __global float* V,
                                    uint width, uint height,
                                    __global float* W)
{        
    // Each work-item handles as many matrix rows as necessary
    for (uint y = get_global_id(0);
         y < height;
         y += get_global_size(0))
    {

        // Row pointer
        const __global float* row = M + y * width;

        // Compute dot product  
        float dotProduct = 0;
        for (uint x = 0; x < width; ++x)
            dotProduct += row[x] * V[x];

        // Write result to global memory
        W[y] = dotProduct;
    }
}

