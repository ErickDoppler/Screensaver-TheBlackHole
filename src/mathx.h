/* Minimal vector/matrix helpers. Column-major 4x4 matrices, OpenGL style. */
#ifndef BH_MATHX_H
#define BH_MATHX_H
#include <math.h>

#define BH_PI 3.14159265358979323846f

typedef struct { float x, y, z; } vec3;
typedef struct { float m[16]; } mat4;

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float smoothstepf(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
/* Wraps v into [lo, hi) - used for angles that run for hours. */
static inline float wrapf(float v, float lo, float hi) {
    float span = hi - lo;
    if (span <= 0.f) return lo;
    while (v >= hi) v -= span;
    while (v < lo)  v += span;
    return v;
}
/* Exponential approach used for frame-rate independent smoothing. */
static inline float approachf(float cur, float target, float tau, float dt) {
    if (tau <= 0.f) return target;
    float a = 1.f - expf(-dt / tau);
    return cur + (target - cur) * a;
}

static inline vec3 v3(float x, float y, float z) { vec3 r = { x, y, z }; return r; }
static inline vec3 v3_add(vec3 a, vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline vec3 v3_sub(vec3 a, vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline vec3 v3_scale(vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline vec3 v3_cross(vec3 a, vec3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline vec3 v3_norm(vec3 a) {
    float l = sqrtf(v3_dot(a, a));
    return l > 1e-8f ? v3_scale(a, 1.f / l) : a;
}

static inline mat4 m4_identity(void) {
    mat4 r = {{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 }};
    return r;
}
static inline mat4 m4_mul(mat4 a, mat4 b) {
    mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
static inline mat4 m4_perspective(float fovy_rad, float aspect, float znear, float zfar) {
    mat4 r = {{0}};
    float f = 1.f / tanf(fovy_rad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.f;
    r.m[14] = (2.f * zfar * znear) / (znear - zfar);
    return r;
}
static inline mat4 m4_look_at(vec3 eye, vec3 center, vec3 up) {
    vec3 f = v3_norm(v3_sub(center, eye));
    vec3 s = v3_norm(v3_cross(f, up));
    vec3 u = v3_cross(s, f);
    mat4 r = m4_identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8]  = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9]  = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -v3_dot(s, eye);
    r.m[13] = -v3_dot(u, eye);
    r.m[14] = v3_dot(f, eye);
    return r;
}
/* Cheap deterministic PRNG (xorshift32) so the simulation is reproducible. */
typedef struct { unsigned s; } rng_t;
static inline unsigned rng_u32(rng_t *r) {
    unsigned x = r->s ? r->s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    r->s = x;
    return x;
}
static inline float rng_f(rng_t *r) { return (rng_u32(r) >> 8) * (1.f / 16777216.f); }
static inline float rng_range(rng_t *r, float a, float b) { return a + (b - a) * rng_f(r); }
#endif
