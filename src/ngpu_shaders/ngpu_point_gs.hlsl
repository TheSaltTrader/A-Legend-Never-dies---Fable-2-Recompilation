// POINT_LIST (Xenos primitive 1) as sprites, after the translated vertex shader - the plugin's
// method (rexglue pipeline_cache.cpp, PipelineGeometryShader::kPointList, and the point constants
// in d3d12/command_processor.cpp): the diameter is PA_SU_POINT_SIZE (width 31:16, height 15:0,
// units of 1/8 pixel -> x 2/16 = pixels), converted to an NDC radius by 1 / viewport extent, and
// to clip space by W. Zero-size points are dropped (a point list may also be a memexport
// "compute" pass). Corners: top-left, top-right, bottom-left, bottom-right as a strip.
//
// NOT YET: the per-vertex size override (the translated VS writes no PSIZE) and the point
// sprite coordinates (Xenia's param gen puts them in zw with the point flag in y's sign bit;
// the translated param gen carries the pixel position only). The same interpolators are sent
// to all four corners, as in the plugin.

cbuffer SharedConstants : register(b2, space4)
{
    // x, y: the point's diameter in guest pixels; z, w: 1 / viewport extent (x, y).
    float4 g_PointInfo : packoffset(c48);
};

struct V {
    float4 pos : SV_Position;
    float4 t0 : TEXCOORD0;   float4 t1 : TEXCOORD1;   float4 t2 : TEXCOORD2;   float4 t3 : TEXCOORD3;
    float4 t4 : TEXCOORD4;   float4 t5 : TEXCOORD5;   float4 t6 : TEXCOORD6;   float4 t7 : TEXCOORD7;
    float4 t8 : TEXCOORD8;   float4 t9 : TEXCOORD9;   float4 t10 : TEXCOORD10; float4 t11 : TEXCOORD11;
    float4 t12 : TEXCOORD12; float4 t13 : TEXCOORD13; float4 t14 : TEXCOORD14; float4 t15 : TEXCOORD15;
    float4 c0 : COLOR0;      float4 c1 : COLOR1;
};

[maxvertexcount(4)]
void main(point V v[1], inout TriangleStream<V> s) {
    const float2 d = g_PointInfo.xy;
    if (!(d.x > 0.0) || !(d.y > 0.0)) return;
    const float2 r = d * g_PointInfo.zw * v[0].pos.w;   // clip-space radius (0.5 x 2 cancel)
    V o = v[0];
    o.pos.xy = v[0].pos.xy + float2(-r.x,  r.y); s.Append(o);
    o.pos.xy = v[0].pos.xy + float2( r.x,  r.y); s.Append(o);
    o.pos.xy = v[0].pos.xy + float2(-r.x, -r.y); s.Append(o);
    o.pos.xy = v[0].pos.xy + float2( r.x, -r.y); s.Append(o);
    s.RestartStrip();
}
