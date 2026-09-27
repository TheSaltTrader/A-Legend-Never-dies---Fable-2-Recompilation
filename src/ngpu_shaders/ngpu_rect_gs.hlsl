// RECT_LIST (Xenos primitive 8) as a geometry shader, after the translated vertex shader.
//
// A Xenos rectangle arrives as THREE vertices; the fourth is implied. As the plugin does it
// (rexglue pipeline_cache.cpp, PipelineGeometryShader::kRectangleList): the LONGEST edge in
// clip-space xy is the diagonal, the vertex opposite it is the shared corner, and the fourth
// vertex mirrors that corner across the diagonal - applied to EVERY output, not only the
// position, so interpolants stay affine across the rectangle.
//
//   12 longest -> strip 0 1 2 3, v3 = -v0 + v1 + v2
//   20 longest -> strip 1 2 0 3, v3 = -v1 + v2 + v0
//   01 longest -> strip 2 0 1 3, v3 = -v2 + v0 + v1
//
// Doing it here rather than on the CPU covers the rect draws whose vertex shader computes the
// position itself (no POSITION element to mirror) and those whose stream is shorter than the
// draw - the two classes the CPU expansion (BuildRectExpansion) could not handle.
//
// The signature is every translated vertex shader's output (SV_Position, TEXCOORD0..15,
// COLOR0, COLOR1 - the same in all 42 cached VS on 2026-09-25), so one shader serves them all.

struct V {
    float4 pos : SV_Position;
    float4 t0 : TEXCOORD0;   float4 t1 : TEXCOORD1;   float4 t2 : TEXCOORD2;   float4 t3 : TEXCOORD3;
    float4 t4 : TEXCOORD4;   float4 t5 : TEXCOORD5;   float4 t6 : TEXCOORD6;   float4 t7 : TEXCOORD7;
    float4 t8 : TEXCOORD8;   float4 t9 : TEXCOORD9;   float4 t10 : TEXCOORD10; float4 t11 : TEXCOORD11;
    float4 t12 : TEXCOORD12; float4 t13 : TEXCOORD13; float4 t14 : TEXCOORD14; float4 t15 : TEXCOORD15;
    float4 c0 : COLOR0;      float4 c1 : COLOR1;
};

V Mirror(V a, V b, V c) {   // -a + b + c
    V r;
    r.pos = -a.pos + b.pos + c.pos;
    r.t0 = -a.t0 + b.t0 + c.t0;     r.t1 = -a.t1 + b.t1 + c.t1;     r.t2 = -a.t2 + b.t2 + c.t2;     r.t3 = -a.t3 + b.t3 + c.t3;
    r.t4 = -a.t4 + b.t4 + c.t4;     r.t5 = -a.t5 + b.t5 + c.t5;     r.t6 = -a.t6 + b.t6 + c.t6;     r.t7 = -a.t7 + b.t7 + c.t7;
    r.t8 = -a.t8 + b.t8 + c.t8;     r.t9 = -a.t9 + b.t9 + c.t9;     r.t10 = -a.t10 + b.t10 + c.t10; r.t11 = -a.t11 + b.t11 + c.t11;
    r.t12 = -a.t12 + b.t12 + c.t12; r.t13 = -a.t13 + b.t13 + c.t13; r.t14 = -a.t14 + b.t14 + c.t14; r.t15 = -a.t15 + b.t15 + c.t15;
    r.c0 = -a.c0 + b.c0 + c.c0;     r.c1 = -a.c1 + b.c1 + c.c1;
    return r;
}

[maxvertexcount(4)]
void main(triangle V v[3], inout TriangleStream<V> s) {
    const float2 e12 = v[2].pos.xy - v[1].pos.xy;
    const float2 e20 = v[0].pos.xy - v[2].pos.xy;
    const float2 e01 = v[1].pos.xy - v[0].pos.xy;
    const float l12 = dot(e12, e12), l20 = dot(e20, e20), l01 = dot(e01, e01);
    uint a = 0, b = 1, c = 2;                            // 12 longest (the common case)
    if (!(l12 > l20 && l12 > l01)) {
        if (l20 > l01) { a = 1; b = 2; c = 0; }          // 20 longest
        else           { a = 2; b = 0; c = 1; }          // 01 longest
    }
    s.Append(v[a]);
    s.Append(v[b]);
    s.Append(v[c]);
    s.Append(Mirror(v[a], v[b], v[c]));
    s.RestartStrip();
}
