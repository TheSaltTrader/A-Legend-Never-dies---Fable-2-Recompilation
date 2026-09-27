// Native-GPU: fullscreen triangle for the scene blit (SV_VertexID, no inputs);
// outputs the three TEXCOORDs the diagnostic pixel shader declares.
struct VSOut {
    float4 pos : SV_Position;
    float4 tc0 : TEXCOORD0;
    float4 tc1 : TEXCOORD1;
    float4 tc2 : TEXCOORD2;
};
VSOut main(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.tc0 = float4(uv, 0.0, 0.0);
    o.tc1 = 0.0;
    o.tc2 = 0.0;
    return o;
}
