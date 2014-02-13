typedef float float4 __attribute__((ext_vector_type(4)));

extern "C" {
  float __nv_fsqrt_ru(float);
  float __nv_cosf(float);
  float __nv_sinf(float);
  float __nv_expf(float);
  float __nv_logf(float);
  float __nv_hypotf(float, float);
}

extern float sqrt(float f){
  return __nv_fsqrt_ru(f);
}

extern float cos(float f){
  return __nv_cosf(f);
}

extern float sin(float f){
  return __nv_sinf(f);
}

extern float exp(float f){
  return __nv_expf(f);
}

extern float log(float f){
  return __nv_logf(f);
}

extern float4 hypot(float4 a, float4 b){
  float4 result;
  result.x = __nv_hypotf(a.x, b.x);
  result.y = __nv_hypotf(a.y, b.y);
  result.z = __nv_hypotf(a.z, b.z);
  result.w = __nv_hypotf(a.w, b.w);

  return result;
}
