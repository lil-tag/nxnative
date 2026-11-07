// Clears a load action cannot express, drawn as one triangle over the pass (nvn_mtl.m clr_pso): clr_vs places it at the depth in buffer 0, and clr_fs_<slot>_<type> writes the
// value in buffer 0 to colour attachment <slot> as float, uint or int. Every slot and type is in this one library, so no clear compiles a shader when it is first met
#include <metal_stdlib>
using namespace metal;
struct V { float4 p [[position]]; };
vertex V clr_vs(uint i [[vertex_id]], device const float *z [[buffer(0)]]) { V o; o.p = float4(float2(float((i << 1) & 2), float(i & 2)) * 2.0 - 1.0, z[0], 1.0); return o; }
#define CLR_FS(slot, T) \
    struct O_##slot##_##T { T##4 c [[color(slot)]]; }; \
    fragment O_##slot##_##T clr_fs_##slot##_##T(device const T##4 *v [[buffer(0)]]) { O_##slot##_##T o; o.c = v[0]; return o; }
#define CLR_SLOT(slot) CLR_FS(slot, float) CLR_FS(slot, uint) CLR_FS(slot, int)
CLR_SLOT(0) CLR_SLOT(1) CLR_SLOT(2) CLR_SLOT(3) CLR_SLOT(4) CLR_SLOT(5) CLR_SLOT(6) CLR_SLOT(7)
// The finite guard (nvn_mtl.m finite_guard): one triangle over the pass that turns non-finite pixels of colour attachment 1 to 0, read from the tile, so it needs no extra pass and no shader access to the target
vertex V finite_vs(uint i [[vertex_id]]) { V o; o.p = float4(float2(float((i << 1) & 2), float(i & 2)) * 2.0 - 1.0, 0.0, 1.0); return o; }
struct FiniteOut { float4 c [[color(1)]]; };
fragment FiniteOut finite_fs(float4 c [[color(1)]]) { if (all(isfinite(c))) discard_fragment(); FiniteOut o; o.c = float4(0.0); return o; }
