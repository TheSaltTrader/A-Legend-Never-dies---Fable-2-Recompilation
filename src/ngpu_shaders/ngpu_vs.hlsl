// Native-GPU M4-b test vertex shader: POSITION0 through the game's own
// c0..c3 (g_WorldViewProjection, one dp4 per output component - exactly what
// the Xenos microcode does), plus a flat per-draw colour. The 80 bytes of
// root constants come from the draw hook (device+0x780, byte-swapped).
// color.a selects how the constants are read (ngpu_const_mode): 0 = natural
// dot(v, cK); 1 = dot(v, cK.zxyw), the swizzle the translated Fable II
// vertex shaders apply to g_WorldViewProjection rows (run 25).
cbuffer Draw : register(b0)
{
    float4 c0;
    float4 c1;
    float4 c2;
    float4 c3;
    float4 color;
};

struct VSOut
{
    float4 pos : SV_Position;
    float4 col : COLOR0;
};

VSOut main(float3 p : POSITION0)
{
    float4 v = float4(p, 1.0);
    VSOut o;
    if (color.a > 0.5)
        o.pos = float4(dot(v, c0.zxyw), dot(v, c1.zxyw), dot(v, c2.zxyw), dot(v, c3.zxyw));
    else
        o.pos = float4(dot(v, c0), dot(v, c1), dot(v, c2), dot(v, c3));
    o.col = float4(color.rgb, 1.0);
    return o;
}
