// Texture copies drawn as a render pass: colour, depth and stencil (nvn_mtl.m copy_t2t_draw)
#include <metal_stdlib>
using namespace metal;
struct V { float4 p [[position]]; };
vertex V vs(uint id [[vertex_id]]) { float2 uv = float2((id << 1) & 2, id & 2); V o; o.p = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }
// p[0] = source origin (texels) + source texels per destination texel, p[1] = destination origin + source level size
fragment float4 fs(V in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], constant float4 *p [[buffer(0)]]) {
  float2 st = p[0].xy + (in.p.xy - p[1].xy) * p[0].zw; return t.sample(s, st / p[1].zw, level(0)); }
// fs, each colour channel squared keeping its sign: c * |c| (an upscaler's root-encoded input into linear colour)
fragment float4 fsq(V in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], constant float4 *p [[buffer(0)]]) {
  float2 st = p[0].xy + (in.p.xy - p[1].xy) * p[0].zw; float4 c = t.sample(s, st / p[1].zw, level(0)); return float4(c.rgb * abs(c.rgb), c.a); }
// fs with contrast-adaptive sharpening (AMD CAS on the four neighbours; p[2].x = the amount 0-1): the picture an upscaler left, sharpened on its way into the game's image (DM_SHARPEN)
fragment float4 fsh(V in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], constant float4 *p [[buffer(0)]]) {
  float2 st = p[0].xy + (in.p.xy - p[1].xy) * p[0].zw, d = 1 / p[1].zw, uv = st / p[1].zw;
  float4 c = t.sample(s, uv, level(0));
  float3 n = t.sample(s, uv - float2(0, d.y), level(0)).rgb, so = t.sample(s, uv + float2(0, d.y), level(0)).rgb;
  float3 e = t.sample(s, uv + float2(d.x, 0), level(0)).rgb, w = t.sample(s, uv - float2(d.x, 0), level(0)).rgb;
  float3 mn = min(min(min(n, so), min(e, w)), c.rgb), mx = max(max(max(n, so), max(e, w)), c.rgb);
  float3 amp = sqrt(saturate(min(mn, 1 - mx) / max(mx, 1e-4)));   // flat or saturated areas take little; colour above 1 (HDR) none
  float3 k = amp * (-1 / mix(8.0, 5.0, p[2].x));
  return float4((c.rgb + (n + so + e + w) * k) / (1 + 4 * k), c.a); }
struct D { float d [[depth(any)]]; };
fragment D fsd(V in [[stage_in]], depth2d<float> t [[texture(0)]], sampler s [[sampler(0)]], constant float4 *p [[buffer(0)]]) {
  float2 st = p[0].xy + (in.p.xy - p[1].xy) * p[0].zw; D o; o.d = t.read(uint2(st)); return o; }
struct DS { float d [[depth(any)]]; uint s [[stencil]]; };
fragment DS fsds(V in [[stage_in]], depth2d<float> t [[texture(0)]], texture2d<uint> ts [[texture(1)]], constant float4 *p [[buffer(0)]]) {
  float2 st = p[0].xy + (in.p.xy - p[1].xy) * p[0].zw; DS o; o.d = t.read(uint2(st)); o.s = ts.read(uint2(st)).r; return o; }
