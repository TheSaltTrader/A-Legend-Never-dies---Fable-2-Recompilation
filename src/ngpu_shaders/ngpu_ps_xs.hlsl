// Native-GPU M5 pixel shader paired with the game's own translated vertex
// shaders: a flat per-draw colour from the push constants, so the geometry
// the real vertex shader produces (skinning, instancing, wind, scaling) can
// be judged before textures exist. Register space 0 keeps clear of the
// translated shaders' space-4 constant buffers. color.a selects a
// diagnostic (ngpu_ps_debug): 2 / 4 / 5 = TEXCOORD0.xy / 1.xy / 2.xy as
// colour, 7 = TEXCOORD0.zw (the material shader's lightmap coordinate),
// 3 = fetch slot 0's texture (descriptor index in color.r) sampled with
// TEXCOORD0.xy, 6 = slot 1's texture (index in color.g) with TEXCOORD1.xy,
// 8 = slot 1's texture with TEXCOORD0.zw.
cbuffer Draw : register(b0, space0)
{
    float4 color;
};

Texture2D<float4> g_Texture2DDescriptorHeap[] : register(t0, space0);
SamplerState g_SamplerDescriptorHeap[] : register(s0, space3);

float4 main(float4 pos : SV_Position, float4 tc0 : TEXCOORD0, float4 tc1 : TEXCOORD1, float4 tc2 : TEXCOORD2) : SV_Target
{
    const int mode = int(color.a + 0.5);
    switch (mode)
    {
    case 2: return float4(frac(tc0.xy), 0.0, 1.0);
    case 3: return float4(g_Texture2DDescriptorHeap[uint(color.r)].Sample(g_SamplerDescriptorHeap[0], tc0.xy).rgb, 1.0);
    case 4: return float4(frac(tc1.xy), 0.0, 1.0);
    case 5: return float4(frac(tc2.xy), 0.0, 1.0);
    case 6: return float4(g_Texture2DDescriptorHeap[uint(color.g)].Sample(g_SamplerDescriptorHeap[0], tc1.xy).rgb, 1.0);
    case 7: return float4(frac(tc0.zw), 0.0, 1.0);
    case 8: return float4(g_Texture2DDescriptorHeap[uint(color.g)].Sample(g_SamplerDescriptorHeap[0], tc0.zw).rgb, 1.0);
    case 9: return float4(g_Texture2DDescriptorHeap[uint(color.b)].Sample(g_SamplerDescriptorHeap[0], tc0.xy).rgb, 1.0);
    // 11: slot 13's texture laid flat over the screen (4 x 4 tiles of the window) - the texture image itself, whatever the texcoords
    case 11: return float4(g_Texture2DDescriptorHeap[uint(color.b)].SampleLevel(g_SamplerDescriptorHeap[0], pos.xy / float2(316.0, 178.0), 0).rgb, 1.0);
    // 12: slot 0's texture the same way
    case 12: return float4(g_Texture2DDescriptorHeap[uint(color.r)].SampleLevel(g_SamplerDescriptorHeap[0], pos.xy / float2(316.0, 178.0), 0).rgb, 1.0);
    // 13: the descriptor index of slot 13 as a colour (index / 64 in red, index % 64 / 64 in green)
    case 13: return float4(floor(color.b / 64.0) / 64.0, fmod(color.b, 64.0) / 64.0, 0.0, 1.0);
    // 17: the scene blit - the HDR target (descriptor in color.r) sampled with the
    // fullscreen triangle's uv, scaled by the exposure in color.g
    case 17: {
        // exposure then a Reinhard curve: the scene target is HDR (the game's own tonemap does not run yet)
        float3 c = max(g_Texture2DDescriptorHeap[uint(color.r)].SampleLevel(g_SamplerDescriptorHeap[1], tc0.xy, 0).rgb, 0.0) * color.g;
        return float4(c / (1.0 + c), 1.0);
    }
    case 24: return float4(g_Texture2DDescriptorHeap[uint(color.r)].SampleLevel(g_SamplerDescriptorHeap[1], tc0.xy, 0).rgb, 1.0);  // plain copy: the game's own post-processed target
    case 25: return float4(frac(pos.xy / 64.0), 0.5, 1.0);  // coverage: which pixels any translated draw touches
    case 18: return float4(1.0, 0.0, 1.0, 1.0);  // blit-path test
    case 19: return float4(tc0.xy, 0.5, 1.0);      // the fullscreen triangle's uv
    case 20: { float3 c = g_Texture2DDescriptorHeap[uint(color.r)].SampleLevel(g_SamplerDescriptorHeap[1], tc0.xy, 0).rgb; return float4(saturate(c.r), saturate(c.g), saturate(c.b) * 0.5 + 0.25 * tc0.x, 1.0); }
    case 21: return float4(g_Texture2DDescriptorHeap[uint(color.r)].Sample(g_SamplerDescriptorHeap[0], tc0.xy).rgb, 1.0);  // the material's diffuse (slot 13)
    case 22: return float4(g_Texture2DDescriptorHeap[uint(color.g)].Sample(g_SamplerDescriptorHeap[0], tc0.zw).rgb, 1.0);  // the material's lightmap (slot 3)
    // 25: WHAT THE GPU SEES AT THIS DESCRIPTOR. Not the sampled colour - the
    // texture's own dimensions, straight from the descriptor the draw reads.
    // Every CPU-side check says the composite's slot holds a 1280x720 resolve
    // and the sample still comes back flat; this asks the only party that has
    // not been consulted. R = width/2048, G = height/2048, so 1280x720 lands at
    // about (159, 90) in 8 bits and a 1x1 at (0, 0).
    case 30: {
        uint tw = 0, th = 0;
        g_Texture2DDescriptorHeap[uint(color.r)].GetDimensions(tw, th);
        return float4(float(tw) / 2048.0, float(th) / 2048.0, 0.0, 1.0);
    }
    case 23: return float4(g_Texture2DDescriptorHeap[uint(color.b)].Sample(g_SamplerDescriptorHeap[0], tc0.xy).rgb, 1.0);  // slot 0
    default: return float4(color.rgb, 1.0);
    }
}
