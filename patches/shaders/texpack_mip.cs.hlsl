// [texpack] One mip level of a texture-pack replacement from the level above:
// a 2x2 box average. The replacements are uploaded as a single level, and a
// 2x texture with no smaller levels aliases at distance - far grass looked
// like a television with a bad signal (2026-09-14). Dispatched once per level
// after the level-0 upload; a few microseconds of GPU per texture.

cbuffer TexpackMipConstants : register(b0) {
  uint texpack_mip_dst_width;
  uint texpack_mip_dst_height;
  uint texpack_mip_src_width_minus_1;
  uint texpack_mip_src_height_minus_1;
};

Texture2D<float4> texpack_mip_source : register(t0);
RWTexture2D<float4> texpack_mip_dest : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= texpack_mip_dst_width || id.y >= texpack_mip_dst_height) {
    return;
  }
  uint2 s0 = id.xy * 2u;
  // Odd sizes (768 -> 384 -> ... -> 3 -> 1): the last column and row of the
  // source are clamped instead of read out of range as black.
  uint2 s1 = min(s0 + 1u, uint2(texpack_mip_src_width_minus_1, texpack_mip_src_height_minus_1));
  float4 c = texpack_mip_source.Load(int3(s0.x, s0.y, 0)) +
             texpack_mip_source.Load(int3(s1.x, s0.y, 0)) +
             texpack_mip_source.Load(int3(s0.x, s1.y, 0)) +
             texpack_mip_source.Load(int3(s1.x, s1.y, 0));
  texpack_mip_dest[id.xy] = c * 0.25;
}
