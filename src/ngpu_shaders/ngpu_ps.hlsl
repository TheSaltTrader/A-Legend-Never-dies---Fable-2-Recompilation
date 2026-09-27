// Native-GPU M4-b test pixel shader: the flat per-draw colour, shaded a
// little by depth so the geometry reads as geometry.
struct VSOut
{
    float4 pos : SV_Position;
    float4 col : COLOR0;
};

float4 main(VSOut i) : SV_Target0
{
    float shade = 1.0 - saturate(i.pos.z) * 0.6;
    return float4(i.col.rgb * shade, 1.0);
}
