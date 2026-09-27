# SDK DXBC binding contract (phase A reference) - 2026-09-25

Mapped from rexglue-src by an Explore agent; every element carries a file:line. VERIFY each element against the
source as it is implemented - this is a map, not a measurement.

Files (root `rexglue-src\`): CP = src\graphics\d3d12\command_processor.cpp, CPH = include\rex\graphics\d3d12\command_processor.h,
TH = include\rex\graphics\pipeline\shader\dxbc_translator.h, TC = src\graphics\pipeline\shader\dxbc_translator.cpp,
TF = ...\dxbc_translator_fetch.cpp, PC = src\graphics\d3d12\pipeline_cache.cpp, SH = include\rex\graphics\pipeline\shader\shader.h,
DX = include\rex\graphics\pipeline\shader\dxbc.h.

Two switches shape the contract: `d3d12_bindless` (default true, CP:60) and the render-target path (ROV vs RTV,
render_target_cache.cpp:176-202; RTV everywhere except Intel). PHASE A TARGET: BINDFUL + RTV (simplest).

## 1. Root signature
Registers (TH:195, TH:450-505), all cbuffers space 0: b0 system, b1 float, b2 bool/loop, b3 fetch, b4 descriptor indices
(bindless only). SRV t0 space 0 = shared memory (raw), t1+ = bindful textures; bindless SRVs in spaces 1/2/3 (2D array, 3D,
cube). UAV u0 = shared memory, u1 = EDRAM (ROV only).
- BINDLESS (default; two global signatures, CP:1209-1403): 0 CBV b3 fetch ALL; 1 CBV b1 VS float VERTEX (DOMAIN tess);
  2 CBV b1 PS float PIXEL; 3 CBV b4 PS descriptor indices PIXEL; 4 CBV b4 VS descriptor indices VERTEX/DOMAIN;
  5 CBV b0 system ALL; 6 CBV b2 bool/loop ALL; 7 table [t0 raw SRV + u0 raw UAV] ALL; 8 table unbounded samplers s0 ALL;
  9 view-heap table (u1 EDRAM on ROV, then unbounded SRVs spaces 1,2,3 from kUnboundedSRVsStart).
- BINDFUL (one per tex/sampler count + tess bit, CP:483-696): 0 fetch b3; 1 VS float b1; 2 PS float b1; 3 system b0;
  4 bool/loop b2; 5 table [t0 SRV, u0 UAV (+u1 EDRAM if ROV)]; then only if non-empty: PS textures (t1.., space0),
  PS samplers (s0..), VS textures, VS samplers. Index logic: GetRootBindfulExtraParameterIndices (CP:698).

## 2. System constants b0 (TH:270-390, 464 bytes), filled by UpdateSystemConstantValues (CP:4931-5355)
flags@0x00, tess_factor_range[2]@0x04, line_loop_closing_index@0x0C, vertex_index_endian@0x10, vertex_index_offset@0x14,
vertex_index_min/max@0x18, user_clip_planes[6][4]@0x20, ndc_scale[3]@0x80, point_vertex_diameter_min@0x8C,
ndc_offset[3]@0x90, point_vertex_diameter_max@0x9C, point_constant_diameter[2]@0xA0,
point_screen_diameter_to_ndc_radius[2]@0xA8, texture_swizzled_signs[8]@0xB0, textures_resolution_scaled@0xD0,
sample_count_log2[2]@0xD4, alpha_test_reference@0xDC, alpha_to_mask@0xE0, edram_32bpp_tile_pitch@0xE4,
edram_depth_base@0xE8, color_exp_bias[4]@0xF0, poly_offset_front/back@0x100/0x108, edram_stencil[2][4]@0x110,
rt_base[4]@0x130, rt_format_flags[4]@0x140, rt_clamp[4][4]@0x150, rt_keep_mask[4][2]@0x190, rt_blend_factors_ops[4]@0x1B0,
blend_constant[4]@0x1C0.
Sources: flags (bits TH:204) - SharedMemoryIsUAV (memexport), XY/Z/W from PA_CL_VTE_CNTL, primitive polygonal/line,
DepthFloat24 (RB_DEPTH_INFO), alpha func (RB_COLORCONTROL << 7), gamma per RT (RB_COLOR_INFO), ROV depth/stencil
(RB_DEPTHCONTROL). tess range = VGT_HOS_MIN/MAX_TESS_LEVEL + 1. vertex index fields = primitive processor result +
VGT_INDX_OFFSET / VGT_MIN/MAX_VTX_INDX. user clip = PA_CL_UCP_n for ucp_ena bits, packed. ndc_* =
draw_util::GetHostViewportInfo. point = PA_SU_POINT_MINMAX / POINT_SIZE x 2/16. texture signs / resolution scaled = per
fetch slot from the texture cache. sample_count_log2 = RB_SURFACE_INFO.msaa. alpha = RB_ALPHA_REF; alpha_to_mask =
RB_COLORCONTROL>>24 | 0x100. color_exp_bias = 2^RB_COLOR_INFO.exp_bias (-5 for 16_16 on RTV). edram_* = ROV only.

## 3. Float constants b1
Packed by the used bitmap: ConstantRegisterMap.float_bitmap[4] / float_count; GetPackedFloatConstantIndex (SH:703-748).
Dynamic addressing => all 256 unpacked. VS from SHADER_CONSTANT_000 + i, PS from SHADER_CONSTANT_256 + i, in set-bit
order (CP:5579-5603, CP:6104-6128); allocate >= 16 bytes; shader declares float_count vectors (TC:3124).
NOTE: this fork's CP:5604-6100 rewrites VS constants for Fable (FOV widen, ultrawide HUD) - to be matched or excluded.

## 4. Bool/loop b2 and fetch b3
b2 = raw 40 dwords from SHADER_CONSTANT_BOOL_000_031 (CP:6131): bools[8] then loops[32] at byte 32; bool i =
cb2[i>>7][(i>>5)&3] bit i&31; loop i = cb2[2+(i>>2)][i&3] (TC:1571, TC:1716); 10 vectors.
b3 = raw 32x6 dwords from SHADER_CONSTANT_FETCH_00_0 (CP:6144), 48 vectors - texture (6 dw) and vertex (2 dw) fetch.

## 5. Vertex fetch
No input layout: the VS reads SV_VertexID (TC:2553), applies line-loop, endian swap (vertex_index_endian), offset,
min/max clamp (TC:342-391). DMA index buffers bound straight from shared memory (CP:3437). Vertex data by ld_raw at
the fetch-constant address with bounds check + endian swap, from t0 (ByteAddressBuffer) or u0 (RW) by the
SharedMemoryIsUAV flag (TF:56-224). Shared memory = ONE 512 MB buffer (kBufferSizeLog2 = 29) mirroring guest physical
memory, tiled by default (shared_memory.cpp:184-259). Before a draw: RequestRange every vertex_fetch_bitmap slot
(address<<2, size<<2) and every memexport range (CP:3246-3321).

## 6. Textures and samplers
1D/2D fetches are Texture2DArray; also Texture3D, TextureCube; stacked -> 3D + 2D bindings (TF:482-531, 1452-1468).
Per fetch slot an UNSIGNED and a SIGNED SRV; picked at run time from texture_swizzled_signs. Bindless: b4 uint array
indexed by bindless_descriptor_index (heap index - kUnboundedSRVsStart; samplers = sampler-heap index; CP:6365-6426).
Bindful: texture binding i at t(1+i) space 0 in GetTextureBindingsAfterTranslation() order; sampler binding i at s(i).
Binding lists + GetUsedTextureMaskAfterTranslation(): DX:41-79. Sampler binding = fetch_constant + mag/min/mip/aniso
overrides.

## 7. VS/PS pairing - the 64-bit Modification (TH:110-192)
The VS translation DEPENDS ON THE PS: interpolator mask = vs.writes_interpolators() & ps.GetInterpolatorInputMask(
SQ_PROGRAM_CNTL, SQ_CONTEXT_MISC, &param_gen_pos) (CP:3092).
VS bits (PC:817-845): interpolator_mask 16, user_clip_plane_count 3, user_clip_plane_cull 1, vertex_kill_and 1,
output_point_size 1, dynamic_addressable_register_count 8, point_ps_ucp_mode 2, host_vertex_shader_type 4.
PS bits (PC:847-896): interpolator_mask 16, interpolators_centroid 16, param_gen_enable 1, param_gen_interpolator 4,
param_gen_point 1, dynamic_addressable_register_count 8, depth_stencil_mode 2.
Signature order (TH:56-108): TEXCOORD#, XESPRITETEXCOORD, SV_Position, clip/cull, XEPSIZE.

## 8. Other per-draw requirements
RTV: PS writes SV_Target0-3 (+ SV_Coverage, SV_Depth/SV_DepthLessEqual as needed); no PS -> depth-only PS (PC:105).
ROV: PS does the output merger through u1 (EDRAM R32_UINT) with the edram_* constants.
Memexport: shared memory as UAV + flag; guest index buffers then copied to scratch (CP:3419).
Primitive processor (primitive_processor.h:123-166): builds/converts index buffers (fans, line loops, reset index),
reports host type / endian / closing index.
Generated GSs for points, rect lists, quad lists (LINELIST_ADJ) (PC:1327-1335, 1611+); they read b0.

## 9. Standalone translate entry
None as an API. Recipe: DxbcShaderTranslator(vendor, bindless, rov, ...) (TH:50) -> D3D12Shader(type, hash, ucode, n)
-> AnalyzeUcode -> GetOrCreateTranslation(mod) with GetDefault{Vertex,Pixel}ShaderModification as the base ->
TranslateAnalyzedShader -> translated_binary(); metadata: texture/sampler bindings, used texture mask,
constant_register_map(), writes_interpolators(), memexport_eM_written(). Model wrapper:
PipelineCache::TranslateAnalyzedShader (PC:1058-1140). The native module already does steps 1-5 in-process
(src/native_gpu_shader_census.cpp; 406/406 byte-match with the plugin's own DXBC).
