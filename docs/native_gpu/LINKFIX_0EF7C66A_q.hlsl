#ifndef SHADER_COMMON_H_INCLUDED
#define SHADER_COMMON_H_INCLUDED

#define SPEC_CONSTANT_R11G11B10_NORMAL  (1 << 0)
#define SPEC_CONSTANT_ALPHA_TEST        (1 << 1)

#ifdef UNLEASHED_RECOMP
    #define SPEC_CONSTANT_BICUBIC_GI_FILTER (1 << 2)
    #define SPEC_CONSTANT_ALPHA_TO_COVERAGE (1 << 3)
    #define SPEC_CONSTANT_REVERSE_Z         (1 << 4)
#endif

#if !defined(__cplusplus) || defined(__INTELLISENSE__)

#define FLT_MIN asfloat(0xff7fffff)
#define FLT_MAX asfloat(0x7f7fffff)

#ifdef __spirv__

struct PushConstants
{
    uint64_t VertexShaderConstants;
    uint64_t PixelShaderConstants;
    uint64_t SharedConstants;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> g_PushConstants;

#define g_Booleans                 vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 256)
#define g_SwappedTexcoords         vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 260)
#define g_HalfPixelOffset          vk::RawBufferLoad<float2>(g_PushConstants.SharedConstants + 264)
#define g_AlphaThreshold           vk::RawBufferLoad<float>(g_PushConstants.SharedConstants + 272)
#define g_TessFactor               vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 280)
#define g_TessOffset               vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 284)

[[vk::constant_id(0)]] const uint g_SpecConstants = 0;

#define g_SpecConstants() g_SpecConstants

#else

#define DEFINE_SHARED_CONSTANTS() \
    uint g_Booleans : packoffset(c32.x); \
    uint g_SwappedTexcoords : packoffset(c32.y); \
    float2 g_HalfPixelOffset : packoffset(c32.z); \
    float g_AlphaThreshold : packoffset(c33.x); \
    uint g_SpecFlags : packoffset(c33.y); \
    uint g_TessFactor : packoffset(c33.z); \
    uint g_TessOffset : packoffset(c33.w); \
    uint4 g_BoolConstants[2] : packoffset(c34); \
    uint4 g_LoopConstants[8] : packoffset(c36); \
    uint4 g_StreamSlots[4] : packoffset(c44);

// Fable II: the spec constants come per draw from the shared constants
// (c33.y: SPEC_CONSTANT_ALPHA_TEST when RB_COLORCONTROL enables the alpha
// test) - a plain function keeps the vs_6_0 / ps_6_0 profiles.
#define g_SpecConstants() g_SpecFlags

#endif

// Fable II: the boolean constants (registers 0x4900..0x4907, 256 bits: b0..b127
// vertex, b128..b255 pixel) and the loop constants (0x4908..0x4927, one dword
// each: count in bits 0..7, start 8..15, step 16..23) come per draw from the
// shared constants at c34..c35 and c36..c43. The recompiler names an unnamed
// boolean by its absolute index (b132) and a loop by its id (NGPU_LOOP(0).x = count);
// translate_all.sh's fix_hlsl.py rewrites those to NGPU_BOOL(n) / NGPU_LOOP(n)
// (plain bN defines would break the register(bN) bindings).
#define NGPU_BOOL(n) ((g_BoolConstants[(n) >> 7][((n) >> 5) & 3] >> ((n) & 31)) & 1u)
#define NGPU_LOOPW(n) (g_LoopConstants[(n) >> 2][(n) & 3])
#define NGPU_LOOP(n) int4(int(NGPU_LOOPW(n) & 0xFFu), int((NGPU_LOOPW(n) >> 8) & 0xFFu), int((NGPU_LOOPW(n) >> 16) & 0xFFu), 0)

// Fable II: vertex shaders fetch textures too (displacement, instancing
// data); implicit-LOD Sample is a pixel-stage opcode, so the vertex stage
// samples level 0.
#if __SHADER_TARGET_STAGE == __SHADER_STAGE_VERTEX
#define XSAMPLE(s, c) SampleLevel(s, c, 0)
#define XSAMPLE3(s, c, o) SampleLevel(s, c, 0, o)
#else
#define XSAMPLE(s, c) Sample(s, c)
#endif
// Fable II: signed 2_10_10_10 vertex attributes arrive as the raw dword in a
// float input (R32_FLOAT keeps the bits); D3D12 has no R10G10B10A2_SNORM.
float4 unpack2_10_10_10_snorm(uint v)
{
    int3 i = int3(v << 22, v << 12, v << 2) >> 22;
    return float4(max(float3(i) / 511.0, -1.0), float((v >> 30) & 3) / 3.0);
}

Texture2D<float4> g_Texture2DDescriptorHeap[] : register(t0, space0);
Texture3D<float4> g_Texture3DDescriptorHeap[] : register(t0, space1);
TextureCube<float4> g_TextureCubeDescriptorHeap[] : register(t0, space2);
SamplerState g_SamplerDescriptorHeap[] : register(s0, space3);
// Fable II: the vertex streams as raw dword buffers (space 4, the runtime's
// descriptor set 4) for fetches whose index the shader computes; c44..c47
// carry the descriptor index per stream.
StructuredBuffer<uint> g_VertexStreamHeap[] : register(t0, space4);
#define NGPU_STREAM(s) g_VertexStreamHeap[g_StreamSlots[(s) >> 2][(s) & 3]]

uint2 getTexture2DDimensions(Texture2D<float4> texture)
{
    uint2 dimensions;
    texture.GetDimensions(dimensions.x, dimensions.y);
    return dimensions;
}

float4 tfetch2D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float2 texCoord, float2 offset)
{
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[resourceDescriptorIndex];
    return texture.XSAMPLE(g_SamplerDescriptorHeap[samplerDescriptorIndex], texCoord + offset / getTexture2DDimensions(texture));
}

float2 getWeights2D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float2 texCoord, float2 offset)
{
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[resourceDescriptorIndex];
    return select(isnan(texCoord), 0.0, frac(texCoord * getTexture2DDimensions(texture) + offset - 0.5));
}

float w0(float a)
{
    return (1.0f / 6.0f) * (a * (a * (-a + 3.0f) - 3.0f) + 1.0f);
}

float w1(float a)
{
    return (1.0f / 6.0f) * (a * a * (3.0f * a - 6.0f) + 4.0f);
}

float w2(float a)
{
    return (1.0f / 6.0f) * (a * (a * (-3.0f * a + 3.0f) + 3.0f) + 1.0f);
}

float w3(float a)
{
    return (1.0f / 6.0f) * (a * a * a);
}

float g0(float a)
{
    return w0(a) + w1(a);
}

float g1(float a)
{
    return w2(a) + w3(a);
}

float h0(float a)
{
    return -1.0f + w1(a) / (w0(a) + w1(a)) + 0.5f;
}

float h1(float a)
{
    return 1.0f + w3(a) / (w2(a) + w3(a)) + 0.5f;
}

float4 tfetch2DBicubic(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float2 texCoord, float2 offset)
{
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[resourceDescriptorIndex];
    SamplerState samplerState = g_SamplerDescriptorHeap[samplerDescriptorIndex];
    uint2 dimensions = getTexture2DDimensions(texture);
    
    float x = texCoord.x * dimensions.x + offset.x;
    float y = texCoord.y * dimensions.y + offset.y;

    x -= 0.5f;
    y -= 0.5f;
    float px = floor(x);
    float py = floor(y);
    float fx = x - px;
    float fy = y - py;

    float g0x = g0(fx);
    float g1x = g1(fx);
    float h0x = h0(fx);
    float h1x = h1(fx);
    float h0y = h0(fy);
    float h1y = h1(fy);

    float4 r =
        g0(fy) * (g0x * texture.XSAMPLE(samplerState, float2(px + h0x, py + h0y) / float2(dimensions)) +
            g1x * texture.XSAMPLE(samplerState, float2(px + h1x, py + h0y) / float2(dimensions))) +
        g1(fy) * (g0x * texture.XSAMPLE(samplerState, float2(px + h0x, py + h1y) / float2(dimensions)) +
            g1x * texture.XSAMPLE(samplerState, float2(px + h1x, py + h1y) / float2(dimensions)));

    return r;
}

float4 tfetch3D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float3 texCoord)
{
    return g_Texture3DDescriptorHeap[resourceDescriptorIndex].XSAMPLE(g_SamplerDescriptorHeap[samplerDescriptorIndex], texCoord);
}

struct CubeMapData
{
    float3 cubeMapDirections[2];
    uint cubeMapIndex;
};

float4 tfetchCube(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float3 texCoord, inout CubeMapData cubeMapData)
{
    return g_TextureCubeDescriptorHeap[resourceDescriptorIndex].XSAMPLE(g_SamplerDescriptorHeap[samplerDescriptorIndex], cubeMapData.cubeMapDirections[texCoord.z]);
}

float4 tfetchR11G11B10(uint4 value)
{
    // Fable II: the recompiler calls this only for 11_11_10 fetch formats, so the decode always applies.
    if (true)
    {
        return float4(
            (value.x & 0x00000400 ? -1.0 : 0.0) + ((value.x & 0x3FF) / 1024.0),
            (value.x & 0x00200000 ? -1.0 : 0.0) + (((value.x >> 11) & 0x3FF) / 1024.0),
            (value.x & 0x80000000 ? -1.0 : 0.0) + (((value.x >> 22) & 0x1FF) / 512.0),
            0.0);
    }
    else
    {
        return asfloat(value);
    }
}

float4 tfetchTexcoord(uint swappedTexcoords, float4 value, uint semanticIndex)
{
    return (swappedTexcoords & (1ull << semanticIndex)) != 0 ? value.yxwz : value;
}

float4 cube(float4 value, inout CubeMapData cubeMapData)
{
    uint index = cubeMapData.cubeMapIndex;
    cubeMapData.cubeMapDirections[index] = value.xyz;
    ++cubeMapData.cubeMapIndex;
    
    return float4(0.0, 0.0, 0.0, index);
}

float4 dst(float4 src0, float4 src1)
{
    float4 dest;
    dest.x = 1.0;
    dest.y = src0.y * src1.y;
    dest.z = src0.z;
    dest.w = src1.w;
    return dest;
}

float4 max4(float4 src0)
{
    return max(max(src0.x, src0.y), max(src0.z, src0.w));
}

float2 getPixelCoord(uint resourceDescriptorIndex, float2 texCoord)
{
    return getTexture2DDimensions(g_Texture2DDescriptorHeap[resourceDescriptorIndex]) * texCoord;
}

// A vertex fetch from a stream buffer at a dword address: the formats the
// runtime's input layouts also know (the cache stores the streams already
// byte-swapped per their fetch constant, so dwords read as little-endian).
// Reads past the view return 0 (D3D12 structured-buffer bounds).
float4 ngpu_vload(StructuredBuffer<uint> b, uint a, uint fmt, uint sgn, uint integer)
{
    uint w0 = b[a], w1 = b[a + 1], w2 = b[a + 2], w3 = b[a + 3];
    switch (fmt)
    {
    case 57: return float4(asfloat(w0), asfloat(w1), asfloat(w2), 1.0);
    case 38: return float4(asfloat(w0), asfloat(w1), asfloat(w2), asfloat(w3));
    case 37: return float4(asfloat(w0), asfloat(w1), 0.0, 1.0);
    case 36: return float4(asfloat(w0), 0.0, 0.0, 1.0);
    case 32: return float4(f16tof32(w0), f16tof32(w0 >> 16), f16tof32(w1), f16tof32(w1 >> 16));
    case 31: return float4(f16tof32(w0), f16tof32(w0 >> 16), 0.0, 1.0);
    case 6:
    {
        uint4 u = uint4(w0 & 0xFF, (w0 >> 8) & 0xFF, (w0 >> 16) & 0xFF, w0 >> 24);
        int4 i = (int4(u << 24)) >> 24;
        if (integer) return sgn ? float4(i) : float4(u);
        return sgn ? max(float4(i) / 127.0, -1.0) : float4(u) / 255.0;
    }
    case 26:
    {
        uint4 u = uint4(w0 & 0xFFFF, w0 >> 16, w1 & 0xFFFF, w1 >> 16);
        int4 i = (int4(u << 16)) >> 16;
        if (integer) return sgn ? float4(i) : float4(u);
        return sgn ? max(float4(i) / 32767.0, -1.0) : float4(u) / 65535.0;
    }
    case 25:
    {
        uint2 u = uint2(w0 & 0xFFFF, w0 >> 16);
        int2 i = (int2(u << 16)) >> 16;
        float2 v = integer ? (sgn ? float2(i) : float2(u)) : (sgn ? max(float2(i) / 32767.0, -1.0) : float2(u) / 65535.0);
        return float4(v, 0.0, 1.0);
    }
    case 7:
    {
        uint3 u = uint3(w0 & 0x3FF, (w0 >> 10) & 0x3FF, (w0 >> 20) & 0x3FF);
        uint w = w0 >> 30;
        if (sgn) return float4(max(float3((int3(u << 22)) >> 22) / 511.0, -1.0), max(float((int(w << 30)) >> 30), -1.0));
        return float4(float3(u) / 1023.0, float(w) / 3.0);
    }
    case 16:
    case 17:
        return tfetchR11G11B10(uint4(w0, 0, 0, 0));
    default:
        return float4(0.0, 0.0, 0.0, 1.0);
    }
}

float computeMipLevel(float2 pixelCoord)
{
    float2 dx = ddx(pixelCoord);
    float2 dy = ddy(pixelCoord);
    float deltaMaxSqr = max(dot(dx, dx), dot(dy, dy));
    return max(0.0, 0.5 * log2(deltaMaxSqr));
}

#endif

#endif

#ifdef __spirv__

#define g_Consts(INDEX) select((INDEX) < 256, vk::RawBufferLoad<float4>(g_PushConstants.VertexShaderConstants + (0 + min(INDEX, 255)) * 16, 0x10), 0.0)
#define g_Sampler0_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 0)
#define g_Sampler0_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 64)
#define g_Sampler0_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 128)
#define g_Sampler0_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 192)
#define g_Sampler1_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 4)
#define g_Sampler1_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 68)
#define g_Sampler1_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 132)
#define g_Sampler1_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 196)
#define g_Sampler2_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 8)
#define g_Sampler2_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 72)
#define g_Sampler2_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 136)
#define g_Sampler2_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 200)
#define g_Sampler3_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 12)
#define g_Sampler3_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 76)
#define g_Sampler3_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 140)
#define g_Sampler3_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 204)
#define g_Sampler4_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 16)
#define g_Sampler4_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 80)
#define g_Sampler4_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 144)
#define g_Sampler4_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 208)
#define g_Sampler5_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 20)
#define g_Sampler5_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 84)
#define g_Sampler5_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 148)
#define g_Sampler5_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 212)
#define g_Sampler6_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 24)
#define g_Sampler6_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 88)
#define g_Sampler6_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 152)
#define g_Sampler6_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 216)
#define g_Sampler7_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 28)
#define g_Sampler7_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 92)
#define g_Sampler7_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 156)
#define g_Sampler7_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 220)
#define g_Sampler8_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 32)
#define g_Sampler8_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 96)
#define g_Sampler8_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 160)
#define g_Sampler8_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 224)
#define g_Sampler9_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 36)
#define g_Sampler9_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 100)
#define g_Sampler9_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 164)
#define g_Sampler9_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 228)
#define g_Sampler10_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 40)
#define g_Sampler10_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 104)
#define g_Sampler10_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 168)
#define g_Sampler10_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 232)
#define g_Sampler11_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 44)
#define g_Sampler11_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 108)
#define g_Sampler11_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 172)
#define g_Sampler11_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 236)
#define g_Sampler12_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 48)
#define g_Sampler12_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 112)
#define g_Sampler12_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 176)
#define g_Sampler12_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 240)
#define g_Sampler13_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 52)
#define g_Sampler13_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 116)
#define g_Sampler13_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 180)
#define g_Sampler13_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 244)
#define g_Sampler14_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 56)
#define g_Sampler14_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 120)
#define g_Sampler14_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 184)
#define g_Sampler14_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 248)
#define g_Sampler15_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 60)
#define g_Sampler15_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 124)
#define g_Sampler15_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 188)
#define g_Sampler15_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 252)
#define g_Sampler16_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 64)
#define g_Sampler16_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 128)
#define g_Sampler16_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 192)
#define g_Sampler16_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 256)
#define g_Sampler17_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 68)
#define g_Sampler17_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 132)
#define g_Sampler17_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 196)
#define g_Sampler17_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 260)
#define g_Sampler18_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 72)
#define g_Sampler18_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 136)
#define g_Sampler18_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 200)
#define g_Sampler18_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 264)
#define g_Sampler19_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 76)
#define g_Sampler19_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 140)
#define g_Sampler19_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 204)
#define g_Sampler19_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 268)
#define g_Sampler20_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 80)
#define g_Sampler20_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 144)
#define g_Sampler20_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 208)
#define g_Sampler20_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 272)
#define g_Sampler21_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 84)
#define g_Sampler21_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 148)
#define g_Sampler21_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 212)
#define g_Sampler21_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 276)
#define g_Sampler22_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 88)
#define g_Sampler22_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 152)
#define g_Sampler22_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 216)
#define g_Sampler22_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 280)
#define g_Sampler23_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 92)
#define g_Sampler23_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 156)
#define g_Sampler23_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 220)
#define g_Sampler23_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 284)
#define g_Sampler24_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 96)
#define g_Sampler24_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 160)
#define g_Sampler24_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 224)
#define g_Sampler24_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 288)
#define g_Sampler25_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 100)
#define g_Sampler25_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 164)
#define g_Sampler25_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 228)
#define g_Sampler25_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 292)
#define g_Sampler26_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 104)
#define g_Sampler26_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 168)
#define g_Sampler26_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 232)
#define g_Sampler26_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 296)
#define g_Sampler27_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 108)
#define g_Sampler27_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 172)
#define g_Sampler27_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 236)
#define g_Sampler27_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 300)
#define g_Sampler28_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 112)
#define g_Sampler28_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 176)
#define g_Sampler28_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 240)
#define g_Sampler28_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 304)
#define g_Sampler29_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 116)
#define g_Sampler29_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 180)
#define g_Sampler29_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 244)
#define g_Sampler29_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 308)
#define g_Sampler30_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 120)
#define g_Sampler30_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 184)
#define g_Sampler30_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 248)
#define g_Sampler30_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 312)
#define g_Sampler31_Texture2DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 124)
#define g_Sampler31_Texture3DDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 188)
#define g_Sampler31_TextureCubeDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 252)
#define g_Sampler31_SamplerDescriptorIndex vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 316)

#else

cbuffer VertexShaderConstants : register(b0, space4)
{
	float4 g_Consts[256] : packoffset(c0);
#define g_Consts(INDEX) select((INDEX) < 256, g_Consts[min(INDEX, 255)], 0.0)
};

cbuffer SharedConstants : register(b2, space4)
{
	uint g_Sampler0_Texture2DDescriptorIndex : packoffset(c0.x);
	uint g_Sampler0_Texture3DDescriptorIndex : packoffset(c8.x);
	uint g_Sampler0_TextureCubeDescriptorIndex : packoffset(c16.x);
	uint g_Sampler0_SamplerDescriptorIndex : packoffset(c24.x);
	uint g_Sampler1_Texture2DDescriptorIndex : packoffset(c0.y);
	uint g_Sampler1_Texture3DDescriptorIndex : packoffset(c8.y);
	uint g_Sampler1_TextureCubeDescriptorIndex : packoffset(c16.y);
	uint g_Sampler1_SamplerDescriptorIndex : packoffset(c24.y);
	uint g_Sampler2_Texture2DDescriptorIndex : packoffset(c0.z);
	uint g_Sampler2_Texture3DDescriptorIndex : packoffset(c8.z);
	uint g_Sampler2_TextureCubeDescriptorIndex : packoffset(c16.z);
	uint g_Sampler2_SamplerDescriptorIndex : packoffset(c24.z);
	uint g_Sampler3_Texture2DDescriptorIndex : packoffset(c0.w);
	uint g_Sampler3_Texture3DDescriptorIndex : packoffset(c8.w);
	uint g_Sampler3_TextureCubeDescriptorIndex : packoffset(c16.w);
	uint g_Sampler3_SamplerDescriptorIndex : packoffset(c24.w);
	uint g_Sampler4_Texture2DDescriptorIndex : packoffset(c1.x);
	uint g_Sampler4_Texture3DDescriptorIndex : packoffset(c9.x);
	uint g_Sampler4_TextureCubeDescriptorIndex : packoffset(c17.x);
	uint g_Sampler4_SamplerDescriptorIndex : packoffset(c25.x);
	uint g_Sampler5_Texture2DDescriptorIndex : packoffset(c1.y);
	uint g_Sampler5_Texture3DDescriptorIndex : packoffset(c9.y);
	uint g_Sampler5_TextureCubeDescriptorIndex : packoffset(c17.y);
	uint g_Sampler5_SamplerDescriptorIndex : packoffset(c25.y);
	uint g_Sampler6_Texture2DDescriptorIndex : packoffset(c1.z);
	uint g_Sampler6_Texture3DDescriptorIndex : packoffset(c9.z);
	uint g_Sampler6_TextureCubeDescriptorIndex : packoffset(c17.z);
	uint g_Sampler6_SamplerDescriptorIndex : packoffset(c25.z);
	uint g_Sampler7_Texture2DDescriptorIndex : packoffset(c1.w);
	uint g_Sampler7_Texture3DDescriptorIndex : packoffset(c9.w);
	uint g_Sampler7_TextureCubeDescriptorIndex : packoffset(c17.w);
	uint g_Sampler7_SamplerDescriptorIndex : packoffset(c25.w);
	uint g_Sampler8_Texture2DDescriptorIndex : packoffset(c2.x);
	uint g_Sampler8_Texture3DDescriptorIndex : packoffset(c10.x);
	uint g_Sampler8_TextureCubeDescriptorIndex : packoffset(c18.x);
	uint g_Sampler8_SamplerDescriptorIndex : packoffset(c26.x);
	uint g_Sampler9_Texture2DDescriptorIndex : packoffset(c2.y);
	uint g_Sampler9_Texture3DDescriptorIndex : packoffset(c10.y);
	uint g_Sampler9_TextureCubeDescriptorIndex : packoffset(c18.y);
	uint g_Sampler9_SamplerDescriptorIndex : packoffset(c26.y);
	uint g_Sampler10_Texture2DDescriptorIndex : packoffset(c2.z);
	uint g_Sampler10_Texture3DDescriptorIndex : packoffset(c10.z);
	uint g_Sampler10_TextureCubeDescriptorIndex : packoffset(c18.z);
	uint g_Sampler10_SamplerDescriptorIndex : packoffset(c26.z);
	uint g_Sampler11_Texture2DDescriptorIndex : packoffset(c2.w);
	uint g_Sampler11_Texture3DDescriptorIndex : packoffset(c10.w);
	uint g_Sampler11_TextureCubeDescriptorIndex : packoffset(c18.w);
	uint g_Sampler11_SamplerDescriptorIndex : packoffset(c26.w);
	uint g_Sampler12_Texture2DDescriptorIndex : packoffset(c3.x);
	uint g_Sampler12_Texture3DDescriptorIndex : packoffset(c11.x);
	uint g_Sampler12_TextureCubeDescriptorIndex : packoffset(c19.x);
	uint g_Sampler12_SamplerDescriptorIndex : packoffset(c27.x);
	uint g_Sampler13_Texture2DDescriptorIndex : packoffset(c3.y);
	uint g_Sampler13_Texture3DDescriptorIndex : packoffset(c11.y);
	uint g_Sampler13_TextureCubeDescriptorIndex : packoffset(c19.y);
	uint g_Sampler13_SamplerDescriptorIndex : packoffset(c27.y);
	uint g_Sampler14_Texture2DDescriptorIndex : packoffset(c3.z);
	uint g_Sampler14_Texture3DDescriptorIndex : packoffset(c11.z);
	uint g_Sampler14_TextureCubeDescriptorIndex : packoffset(c19.z);
	uint g_Sampler14_SamplerDescriptorIndex : packoffset(c27.z);
	uint g_Sampler15_Texture2DDescriptorIndex : packoffset(c3.w);
	uint g_Sampler15_Texture3DDescriptorIndex : packoffset(c11.w);
	uint g_Sampler15_TextureCubeDescriptorIndex : packoffset(c19.w);
	uint g_Sampler15_SamplerDescriptorIndex : packoffset(c27.w);
	uint g_Sampler16_Texture2DDescriptorIndex : packoffset(c4.x);
	uint g_Sampler16_Texture3DDescriptorIndex : packoffset(c12.x);
	uint g_Sampler16_TextureCubeDescriptorIndex : packoffset(c20.x);
	uint g_Sampler16_SamplerDescriptorIndex : packoffset(c28.x);
	uint g_Sampler17_Texture2DDescriptorIndex : packoffset(c4.y);
	uint g_Sampler17_Texture3DDescriptorIndex : packoffset(c12.y);
	uint g_Sampler17_TextureCubeDescriptorIndex : packoffset(c20.y);
	uint g_Sampler17_SamplerDescriptorIndex : packoffset(c28.y);
	uint g_Sampler18_Texture2DDescriptorIndex : packoffset(c4.z);
	uint g_Sampler18_Texture3DDescriptorIndex : packoffset(c12.z);
	uint g_Sampler18_TextureCubeDescriptorIndex : packoffset(c20.z);
	uint g_Sampler18_SamplerDescriptorIndex : packoffset(c28.z);
	uint g_Sampler19_Texture2DDescriptorIndex : packoffset(c4.w);
	uint g_Sampler19_Texture3DDescriptorIndex : packoffset(c12.w);
	uint g_Sampler19_TextureCubeDescriptorIndex : packoffset(c20.w);
	uint g_Sampler19_SamplerDescriptorIndex : packoffset(c28.w);
	uint g_Sampler20_Texture2DDescriptorIndex : packoffset(c5.x);
	uint g_Sampler20_Texture3DDescriptorIndex : packoffset(c13.x);
	uint g_Sampler20_TextureCubeDescriptorIndex : packoffset(c21.x);
	uint g_Sampler20_SamplerDescriptorIndex : packoffset(c29.x);
	uint g_Sampler21_Texture2DDescriptorIndex : packoffset(c5.y);
	uint g_Sampler21_Texture3DDescriptorIndex : packoffset(c13.y);
	uint g_Sampler21_TextureCubeDescriptorIndex : packoffset(c21.y);
	uint g_Sampler21_SamplerDescriptorIndex : packoffset(c29.y);
	uint g_Sampler22_Texture2DDescriptorIndex : packoffset(c5.z);
	uint g_Sampler22_Texture3DDescriptorIndex : packoffset(c13.z);
	uint g_Sampler22_TextureCubeDescriptorIndex : packoffset(c21.z);
	uint g_Sampler22_SamplerDescriptorIndex : packoffset(c29.z);
	uint g_Sampler23_Texture2DDescriptorIndex : packoffset(c5.w);
	uint g_Sampler23_Texture3DDescriptorIndex : packoffset(c13.w);
	uint g_Sampler23_TextureCubeDescriptorIndex : packoffset(c21.w);
	uint g_Sampler23_SamplerDescriptorIndex : packoffset(c29.w);
	uint g_Sampler24_Texture2DDescriptorIndex : packoffset(c6.x);
	uint g_Sampler24_Texture3DDescriptorIndex : packoffset(c14.x);
	uint g_Sampler24_TextureCubeDescriptorIndex : packoffset(c22.x);
	uint g_Sampler24_SamplerDescriptorIndex : packoffset(c30.x);
	uint g_Sampler25_Texture2DDescriptorIndex : packoffset(c6.y);
	uint g_Sampler25_Texture3DDescriptorIndex : packoffset(c14.y);
	uint g_Sampler25_TextureCubeDescriptorIndex : packoffset(c22.y);
	uint g_Sampler25_SamplerDescriptorIndex : packoffset(c30.y);
	uint g_Sampler26_Texture2DDescriptorIndex : packoffset(c6.z);
	uint g_Sampler26_Texture3DDescriptorIndex : packoffset(c14.z);
	uint g_Sampler26_TextureCubeDescriptorIndex : packoffset(c22.z);
	uint g_Sampler26_SamplerDescriptorIndex : packoffset(c30.z);
	uint g_Sampler27_Texture2DDescriptorIndex : packoffset(c6.w);
	uint g_Sampler27_Texture3DDescriptorIndex : packoffset(c14.w);
	uint g_Sampler27_TextureCubeDescriptorIndex : packoffset(c22.w);
	uint g_Sampler27_SamplerDescriptorIndex : packoffset(c30.w);
	uint g_Sampler28_Texture2DDescriptorIndex : packoffset(c7.x);
	uint g_Sampler28_Texture3DDescriptorIndex : packoffset(c15.x);
	uint g_Sampler28_TextureCubeDescriptorIndex : packoffset(c23.x);
	uint g_Sampler28_SamplerDescriptorIndex : packoffset(c31.x);
	uint g_Sampler29_Texture2DDescriptorIndex : packoffset(c7.y);
	uint g_Sampler29_Texture3DDescriptorIndex : packoffset(c15.y);
	uint g_Sampler29_TextureCubeDescriptorIndex : packoffset(c23.y);
	uint g_Sampler29_SamplerDescriptorIndex : packoffset(c31.y);
	uint g_Sampler30_Texture2DDescriptorIndex : packoffset(c7.z);
	uint g_Sampler30_Texture3DDescriptorIndex : packoffset(c15.z);
	uint g_Sampler30_TextureCubeDescriptorIndex : packoffset(c23.z);
	uint g_Sampler30_SamplerDescriptorIndex : packoffset(c31.z);
	uint g_Sampler31_Texture2DDescriptorIndex : packoffset(c7.w);
	uint g_Sampler31_Texture3DDescriptorIndex : packoffset(c15.w);
	uint g_Sampler31_TextureCubeDescriptorIndex : packoffset(c23.w);
	uint g_Sampler31_SamplerDescriptorIndex : packoffset(c31.w);
	DEFINE_SHARED_CONSTANTS();
};

#endif
	#define g_Bool0 (1 << 0)
	#define g_Bool1 (1 << 1)
	#define g_Bool2 (1 << 2)
	#define g_Bool3 (1 << 3)
	#define g_Bool4 (1 << 4)
	#define g_Bool5 (1 << 5)
	#define g_Bool6 (1 << 6)
	#define g_Bool7 (1 << 7)
	#define g_Bool8 (1 << 8)
	#define g_Bool9 (1 << 9)
	#define g_Bool10 (1 << 10)
	#define g_Bool11 (1 << 11)
	#define g_Bool12 (1 << 12)
	#define g_Bool13 (1 << 13)
	#define g_Bool14 (1 << 14)
	#define g_Bool15 (1 << 15)
	#define g_Bool16 (1 << 16)
	#define g_Bool17 (1 << 17)
	#define g_Bool18 (1 << 18)
	#define g_Bool19 (1 << 19)
	#define g_Bool20 (1 << 20)
	#define g_Bool21 (1 << 21)
	#define g_Bool22 (1 << 22)
	#define g_Bool23 (1 << 23)
	#define g_Bool24 (1 << 24)
	#define g_Bool25 (1 << 25)
	#define g_Bool26 (1 << 26)
	#define g_Bool27 (1 << 27)
	#define g_Bool28 (1 << 28)
	#define g_Bool29 (1 << 29)
	#define g_Bool30 (1 << 30)
	#define g_Bool31 (1 << 31)

#ifndef __spirv__
[shader("vertex")]
#endif
void main(
	in uint iVertexId : SV_VertexID,
	in uint iInstanceId : SV_InstanceID,
	out precise float4 oPos : SV_Position,
	out float4 oTexCoord0 : TEXCOORD0,
	out float4 oTexCoord1 : TEXCOORD1,
	out float4 oTexCoord2 : TEXCOORD2,
	out float4 oTexCoord3 : TEXCOORD3,
	out float4 oTexCoord4 : TEXCOORD4,
	out float4 oTexCoord5 : TEXCOORD5,
	out float4 oTexCoord6 : TEXCOORD6,
	out float4 oTexCoord7 : TEXCOORD7,
	out float4 oTexCoord8 : TEXCOORD8,
	out float4 oTexCoord9 : TEXCOORD9,
	out float4 oTexCoord10 : TEXCOORD10,
	out float4 oTexCoord11 : TEXCOORD11,
	out float4 oTexCoord12 : TEXCOORD12,
	out float4 oTexCoord13 : TEXCOORD13,
	out float4 oTexCoord14 : TEXCOORD14,
	out float4 oTexCoord15 : TEXCOORD15,
	out float4 oColor0 : COLOR0,
	out float4 oColor1 : COLOR1)
{
	float4 c252 = asfloat(uint4(0x0, 0x0, 0x0, 0x0));
	float4 c253 = asfloat(uint4(0x3F000000, 0x3D888889, 0xBF800000, 0x3F892492));
	float4 c254 = asfloat(uint4(0x0, 0x3F800000, 0x40400000, 0x40000000));
	float4 c255 = asfloat(uint4(0x7FE00000, 0x7FE00000, 0x7FE00000, 0x7FE00000));

	oTexCoord0 = 0.0;
	oTexCoord1 = 0.0;
	oTexCoord2 = 0.0;
	oTexCoord3 = 0.0;
	oTexCoord4 = 0.0;
	oTexCoord5 = 0.0;
	oTexCoord6 = 0.0;
	oTexCoord7 = 0.0;
	oTexCoord8 = 0.0;
	oTexCoord9 = 0.0;
	oTexCoord10 = 0.0;
	oTexCoord11 = 0.0;
	oTexCoord12 = 0.0;
	oTexCoord13 = 0.0;
	oTexCoord14 = 0.0;
	oTexCoord15 = 0.0;
	oColor0 = 0.0;
	oColor1 = 0.0;

	uint tq_t = max(g_TessFactor & 0xFFu, 1u);
	uint tq_flags = (g_TessFactor >> 8) & 0xFFu;
	uint tq_min = max((g_TessFactor >> 16) & 0xFFu, 1u);
	uint tq_patch = iInstanceId;
	float tq_pidx;
	float4 tq_r1 = float4(0.0, 0.0, 0.0, 0.0);
	if (tq_flags & 2u) {
		uint tq_b = g_TessOffset + tq_patch * 4u;
		tq_pidx = float(tq_b);
		tq_r1 = float4(float(tq_b + 1u), float(tq_b + 2u), float(tq_b + 3u), 0.0);
	} else if (tq_flags & 8u) {
		tq_pidx = float((tq_patch + g_TessOffset) & 0xFFFFFFu);
	} else if (tq_flags & 1u) {
		tq_pidx = float((tq_patch + g_TessOffset) & 0xFFFFFFu);
		float4 tq_f = float4(asfloat(NGPU_STREAM(15)[tq_patch * 4u]), asfloat(NGPU_STREAM(15)[tq_patch * 4u + 1u]), asfloat(NGPU_STREAM(15)[tq_patch * 4u + 2u]), asfloat(NGPU_STREAM(15)[tq_patch * 4u + 3u]));
		float tq_fm = max(max(tq_f.x, tq_f.y), max(tq_f.z, tq_f.w)) + 1.0;
		tq_t = clamp(uint(ceil(tq_fm)), tq_min, tq_t);
	} else {
		tq_pidx = float((NGPU_STREAM(15)[tq_patch] + g_TessOffset) & 0xFFFFFFu);
	}
	uint tq_vpp = 6u * tq_t * tq_t;
	uint tq_cell = iVertexId / 6u, tq_corner = iVertexId - tq_cell * 6u;
	uint tq_i = tq_cell % tq_t, tq_j = tq_cell / tq_t;
	uint tq_du = (tq_corner == 1u || tq_corner == 3u || tq_corner == 4u) ? 1u : 0u;
	uint tq_dv = (tq_corner == 2u || tq_corner == 4u || tq_corner == 5u) ? 1u : 0u;
	if (tq_flags & 4u) { uint tq_s = tq_du; tq_du = tq_dv; tq_dv = tq_s; }
	float2 tq_uv = (iVertexId < tq_vpp) ? float2(float(tq_i + tq_du), float(tq_j + tq_dv)) / float(tq_t) : float2(0.0, 0.0);
	float4 r0 = (tq_flags & 2u) ? float4(tq_uv.x, tq_uv.y, tq_pidx, 0.0) : float4(tq_pidx, tq_uv.x, tq_uv.y, 0.0);
	float4 r1 = tq_r1;
	float4 r2 = 0.0;
	float4 r3 = 0.0;
	float4 r4 = 0.0;
	float4 r5 = 0.0;
	float4 r6 = 0.0;
	float4 r7 = 0.0;
	float4 r8 = 0.0;
	float4 r9 = 0.0;
	float4 r10 = 0.0;
	float4 r11 = 0.0;
	float4 r12 = 0.0;
	float4 r13 = 0.0;
	float4 r14 = 0.0;
	float4 r15 = 0.0;
	float4 r16 = 0.0;
	float4 r17 = 0.0;
	float4 r18 = 0.0;
	float4 r19 = 0.0;
	float4 r20 = 0.0;
	float4 r21 = 0.0;
	float4 r22 = 0.0;
	float4 r23 = 0.0;
	float4 r24 = 0.0;
	float4 r25 = 0.0;
	float4 r26 = 0.0;
	float4 r27 = 0.0;
	float4 r28 = 0.0;
	float4 r29 = 0.0;
	float4 r30 = 0.0;
	float4 r31 = 0.0;
	int a0 = 0;
	int aL = 0;
	bool p0 = false;
	float ps = 0.0;

	r1.zw = -g_Consts(47).zw + g_Consts(113).xy;
	r3.xyzw = r1.xxxx == g_Consts(254).xyzw;
	ps = g_Consts(11).y * r0.x;
	r0.w = ps;
	r2.z = dot(r3.xy, r0.zz) + g_Consts(254).x;
	r1.x = dot(r3.zx, r0.yy) + g_Consts(254).x;
	r0.yz = -r0.yz + g_Consts(254).yy;
	ps = floor(r0.w);
	r2.w = ps;
	r2.x = -r2.w * g_Consts(11).x + r0.x;
	r2.y = dot(r0.yy, r3.wy) + g_Consts(254).x;
	r1.y = dot(r0.zz, r3.zw) + g_Consts(254).x;
	r2.yz = r2.yz + r1.xy;
	r1.xy = r2.zx + r2.wy;
	r0.xy = r1.yx * g_Consts(46).xy + r1.zw;
	r2.xy = r0.xy * g_Consts(47).xy;
	r0.x = tfetch2D(g_Sampler16_Texture2DDescriptorIndex, g_Sampler16_SamplerDescriptorIndex, r2.xy, float2(0, 0)).x;
	r0.x = r0.x * g_Consts(46).z;
	r0.zw = r1.xy * g_Consts(46).yx + g_Consts(113).yx;
	r1.xy = r1.xy * g_Consts(115).yx + g_Consts(115).wz;
	r1.z = tfetch2D(g_Sampler18_Texture2DDescriptorIndex, g_Sampler18_SamplerDescriptorIndex, r1.yx, float2(0, 0)).w;
	r1.xy = tfetch2D(g_Sampler17_Texture2DDescriptorIndex, g_Sampler17_SamplerDescriptorIndex, r2.xy, float2(0, 0)).xy;
	r1.yw = r1.yx * g_Consts(254).ww + g_Consts(253).zz;
	r3.xyz = r1.yww * r1.ywy;
	ps = r3.y + r3.x;
	r1.x = ps;
	ps = g_Consts(254).y - r1.x;
	r0.y = ps;
	r2.y = -r0.y * -g_Consts(254).y + r3.x;
	ps = sqrt(abs(r0.y));
	r1.x = ps;
	ps = g_Consts(253).z * r1.x;
	r0.y = ps;
	r2.x = r0.y * r1.w;
	r0.y = dot(r2.xy, r2.xy) + g_Consts(254).x;
	r0.y = -r3.z * -r3.z + r0.y;
	ps = clamp(rsqrt(abs(r0.y)), FLT_MIN, FLT_MAX);
	r2.z = ps;
	r0.y = g_Consts(114).x > r1.z;
	ps = max(-r3.z, -r3.z);
	r2.xy = r2.xy * r2.zz;
	ps = r2.z * ps;
	r2.z = ps;
	r2.xyz = r2.zyx * g_Consts(253).xxx;
	r4.xyz = r2.yxz + g_Consts(253).xxx;
	p0 = r0.y != 0.0;
	ps = p0 ? 0.0 : 1.0;
	if (p0)
	{
		r6.xyz = -abs(r0.xxx) > g_Consts(254).xxx;
		ps = -abs(r0.x) > 0.0;
		r1.w = ps;
	}
	if (p0)
	{
		r2.xyz = -abs(r0.xxx) > g_Consts(254).xxx;
		ps = -abs(r0.x) > 0.0;
		r1.y = ps;
	}
	if (p0)
	{
		r0.xzw = -abs(r0.xxx) > g_Consts(254).xxx;
		ps = -abs(r0.x) > 0.0;
		r1.x = ps;
	}
	if (p0)
	{
		r4.xyzw = -abs(r0.xxxx) > g_Consts(254).xxxx;
		ps = -abs(r0.x) > 0.0;
		r5.x = ps;
	}
	if (p0)
	{
		r7.xyzw = -abs(r0.xxxx) > g_Consts(254).xxxx;
		ps = -abs(r0.x) > 0.0;
		r5.y = ps;
	}
	if (p0)
	{
		r3.xyzw = max(g_Consts(255).xyzw, g_Consts(255).xyzw);
		ps = -abs(r0.x) > 0.0;
		r5.z = ps;
	}
	if (!p0)
	{
		r7.y = -r0.z * g_Consts(8).y;
	}
	if (!p0)
	{
		r5.z = dot(r2.zyx, r1.xwy);
	}
	if (!p0)
	{
		r0.y = dot(r1.xwy, g_Consts(19).zxy);
	}
	if (!p0)
	{
		r9.xyzw = select(g_Consts(254).xxxy == 0.0, r0.xwzz, g_Consts(254).yyyy);
	}
	if (!p0)
	{
		r3.x = dot(g_Consts(0).zxyw, r9.xyzw);
	}
	if (!p0)
	{
		r3.y = dot(g_Consts(1).zxyw, r9.xyzw);
	}
	if (!p0)
	{
		r3.z = dot(g_Consts(2).zxyw, r9.xyzw);
	}
	if (!p0)
	{
		r3.w = dot(g_Consts(3).zxyw, r9.xyzw);
	}
	if (!p0)
	{
		r6.x = dot(g_Consts(4).zxyw, r9.xyzw);
		ps = g_Consts(8).y * r0.w;
		r7.x = ps;
	}
	if (!p0)
	{
		r6.y = dot(g_Consts(5).zxyw, r9.xyzw);
		ps = max(g_Consts(19).x, g_Consts(19).x);
	}
	if (!p0)
	{
		r0.y = r0.y + g_Consts(254).y;
		ps = g_Consts(253).x * ps;
		r0.x = ps;
	}
	if (!p0)
	{
		r10.x = r0.x * r0.y;
		ps = g_Consts(253).x * r0.y;
		r0.x = ps;
	}
	if (!p0)
	{
		r10.yz = r0.xx * g_Consts(19).zy;
		ps = g_Consts(254).y - r0.x;
		r5.w = ps;
	}
	if (!p0)
	{
		r5.xy = r1.wy * r5.ww + r10.xz;
	}
	if (!p0)
	{
		r8.yz = -r1.yw * r5.zz + r2.xy;
	}
	if (!p0)
	{
		r5.zw = r5.zw * r1.xx;
		ps = -g_Consts(253).y - -r1.z;
		r0.x = ps;
	}
	if (!p0)
	{
		r8.x = -r5.z + r2.z;
		ps = g_Consts(253).w * r0.x;
		r4.w = ps;
	}
	if (!p0)
	{
		r0.y = dot(r8.xzy, r8.xzy);
		ps = -g_Consts(47).w - -r0.z;
		r0.x = ps;
	}
	if (!p0)
	{
		r5.z = r5.w + r10.y;
		ps = clamp(rsqrt(abs(r0.y)), FLT_MIN, FLT_MAX);
		r0.y = ps;
	}
	if (!p0)
	{
		r2.xyz = r8.zyx * r0.yyy;
		ps = -g_Consts(47).z - -r0.w;
		r0.y = ps;
	}
	if (!p0)
	{
		r8.xyzw = r2.xyyx * r1.xwxy;
		ps = g_Consts(14).y * r0.x;
		r7.w = ps;
	}
	if (!p0)
	{
		r0.xw = r2.zz * r1.wy;
		ps = r8.y - r8.w;
		r0.z = ps;
	}
	if (!p0)
	{
		r6.z = dot(g_Consts(6).zxyw, r9.xyzw);
		ps = max(r8.x, r8.x);
	}
	if (!p0)
	{
		r0.w = r0.w + -r8.z;
		ps = -r0.x + ps;
		r0.x = ps;
	}
	if (!p0)
	{
		r0.xzw = r0.wxz * g_Consts(253).zzz;
		ps = g_Consts(14).x * r0.y;
		r7.z = ps;
	}
	oPos.xyzw = max(r3.xyzw, r3.xyzw);
	oTexCoord0.xy = max(r7.xy, r7.xy);
	oTexCoord0.z = 0.0;
	oTexCoord0.w = 0.0;
	oTexCoord3.xy = max(r7.zw, r7.zw);
	oTexCoord1.xyz = max(r1.wyx, r1.wyx);
	oTexCoord2.xyz = max(r6.xyz, r6.xyz);
	oTexCoord4.xyz = max(r2.xyz, r2.xyz);
	oTexCoord5.xyz = max(r0.xzw, r0.xzw);
	oTexCoord6.xyz = max(r5.xyz, r5.xyz);
	oTexCoord7.xyzw = max(r4.xyzw, r4.xyzw);
	oColor0 = oTexCoord7;   // LINKFIX TEST: the PS reads its alpha from COLOR0.w
	oPos.xy += g_HalfPixelOffset * oPos.w;
	return;
}