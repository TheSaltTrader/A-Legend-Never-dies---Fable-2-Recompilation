// Native-GPU diagnostic pixel shader (ngpu_ps_debug=10): samples fetch slot
// 13's texture exactly the way the translated shaders do - descriptor index
// and sampler index read from the SharedConstants buffer (b2, space4:
// register 13 sits at c3.y, its sampler at c27.y) through the same
// tfetch2D helper - and outputs the sample. Compiled with the translated
// shaders' flags. Clean here + noise in the game shader = the shader math;
// noise here = the constant-buffer / descriptor path.
Texture2D<float4> g_Texture2DDescriptorHeap[] : register(t0, space0);
SamplerState g_SamplerDescriptorHeap[] : register(s0, space3);

cbuffer SharedConstants : register(b2, space4)
{
    uint4 g_Indices[32];
    uint g_Booleans;
    uint g_SwappedTexcoords;
    float2 g_HalfPixelOffset;
    float g_AlphaThreshold;
};

uint2 getTexture2DDimensions(Texture2D<float4> texture)
{
    uint2 dimensions;
    texture.GetDimensions(dimensions.x, dimensions.y);
    return dimensions;
}

float4 tfetch2D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float2 texCoord, float2 offset)
{
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[resourceDescriptorIndex];
    return texture.Sample(g_SamplerDescriptorHeap[samplerDescriptorIndex], texCoord + offset / getTexture2DDimensions(texture));
}

float4 main(float4 pos : SV_Position, float4 tc0 : TEXCOORD0) : SV_Target
{
    const uint texIndex = g_Indices[3].y;      // register 13 -> c3.y
    const uint samplerIndex = g_Indices[27].y; // 24 + 13/4 -> c27.y
    return float4(tfetch2D(texIndex, samplerIndex, tc0.xy, float2(0, 0)).rgb, 1.0);
}
