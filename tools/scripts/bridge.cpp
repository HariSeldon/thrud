extern "C" {
  float __nv_sqrtf(float);
  float __nv_cosf(float);
  float __nv_sinf(float);
  float __nv_expf(float);
  float __nv_logf(float);
  float __nv_hypotf(float, float);
}

extern float sqrt(float f){
  return __nv_sqrtf(f);
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

extern float hypot(float a, float b){
  return __nv_hypotf(a, b);
}
