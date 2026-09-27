// PHASE A - A4: per-draw constants for an SDK-translated pair. See native_gpu_sdk_consts.h. Every block below is the
// plugin's D3D12CommandProcessor::UpdateSystemConstantValues (command_processor.cpp) for the host-render-target (RTV)
// path, with the plugin's own types; comments name what was dropped and why.
#include "native_gpu_sdk_consts.h"

#include <rex/graphics/pipeline/shader/dxbc_translator.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/util/draw.h>
#include <rex/graphics/xenos.h>
#include <rex/cvar.h>

#include <d3d12.h>

#include <algorithm>
#include <bit>
#include <cfloat>
#include <string>
#include <cstring>
#include <memory>

namespace fable2::ngpu::sdk {

using namespace rex::graphics;

namespace {

int32_t AnisotropicOverrideFromPlugin() {
  // The plugin's registered value (cache.cpp REXCVAR_DEFINE_INT32(anisotropic_override, 3, ...)): the reference leg
  // sets it, so the SDK path must read the same number rather than assume the default.
  // Re-read every 4096 calls, not per sampler (PROF4: a by-name flag lookup + stoi per sampler per draw); a change in
  // the settings still lands within a frame or two.
  static thread_local int32_t cached = 3;
  static thread_local uint32_t calls = 0;
  if ((calls++ & 4095u) == 0) {
    const std::string v = rex::cvar::GetFlagByName("anisotropic_override");
    if (v.empty()) cached = 3;
    else { try { cached = std::stoi(v); } catch (...) { cached = 3; } }
  }
  return cached;
}

// D3D12TextureCache::NormalizeClampMode (texture_cache.cpp:3990).
xenos::ClampMode NormalizeClampMode(xenos::ClampMode m) {
  if (m == xenos::ClampMode::kClampToHalfway) return xenos::ClampMode::kClampToEdge;
  if (m == xenos::ClampMode::kMirrorClampToHalfway || m == xenos::ClampMode::kMirrorClampToBorder) return xenos::ClampMode::kMirrorClampToEdge;
  return m;
}

// GetSamplerParameters (texture_cache.cpp:1697) + WriteSampler (:1790), in one. Not carried: the host-filterability
// demotion (host_filterable_unsigned_/signed_) - every format the native cache creates is filterable on D3D12, which
// is the case in which that block does nothing.
D3D12_SAMPLER_DESC SamplerFor(const RegisterFile& regs, const SamplerBinding& b) {
  const xenos::xe_gpu_texture_fetch_t fetch = regs.GetTextureFetch(b.fetch_constant);
  xenos::ClampMode cx, cy, cz;
  texture_util::GetClampModesForDimension(fetch, cx, cy, cz);
  cx = NormalizeClampMode(cx); cy = NormalizeClampMode(cy); cz = NormalizeClampMode(cz);
  xenos::BorderColor border = xenos::BorderColor::k_ABGR_Black;
  bool force_bc_w_to_max = false;
  if (xenos::ClampModeUsesBorder(cx) || xenos::ClampModeUsesBorder(cy) || xenos::ClampModeUsesBorder(cz)) {
    border = fetch.border_color;
    force_bc_w_to_max = fetch.force_bc_w_to_max;
  }
  uint32_t base_page, mip_min_level, mip_max_level;
  texture_util::GetSubresourcesFromFetchConstant(fetch, nullptr, nullptr, nullptr, &base_page, nullptr, &mip_min_level, &mip_max_level);
  const auto bmag = xenos::TextureFilter(b.mag), bmin = xenos::TextureFilter(b.min), bmip = xenos::TextureFilter(b.mip);
  const xenos::TextureFilter mag_filter = bmag == xenos::TextureFilter::kUseFetchConst ? fetch.mag_filter : bmag;
  const xenos::TextureFilter min_filter = bmin == xenos::TextureFilter::kUseFetchConst ? fetch.min_filter : bmin;
  const xenos::TextureFilter mip_filter = bmip == xenos::TextureFilter::kUseFetchConst ? fetch.mip_filter : bmip;
  const bool min_mag_linear = mag_filter == xenos::TextureFilter::kLinear && min_filter == xenos::TextureFilter::kLinear;
  const bool mip_filter_bilinear_or_trilinear = mip_filter == xenos::TextureFilter::kPoint || mip_filter == xenos::TextureFilter::kLinear;
  const bool mip_base_map = mip_filter == xenos::TextureFilter::kBaseMap;
  if (mip_base_map && base_page != 0) mip_min_level = 0;
  const bool has_mips = mip_max_level > mip_min_level;
  const auto baniso = xenos::AnisoFilter(b.aniso);
  xenos::AnisoFilter aniso_filter = baniso == xenos::AnisoFilter::kUseFetchConst ? fetch.aniso_filter : baniso;
  const int32_t ao = AnisotropicOverrideFromPlugin();
  if (ao > -1 && ao < 6 && has_mips && !mip_base_map && min_mag_linear && mip_filter_bilinear_or_trilinear) aniso_filter = xenos::AnisoFilter(ao);
  aniso_filter = std::min(aniso_filter, xenos::AnisoFilter::kMax_16_1);
  bool mag_linear, min_linear, mip_linear;
  if (aniso_filter != xenos::AnisoFilter::kDisabled) { mag_linear = min_linear = mip_linear = true; }
  else { mag_linear = mag_filter == xenos::TextureFilter::kLinear; min_linear = min_filter == xenos::TextureFilter::kLinear; mip_linear = mip_filter == xenos::TextureFilter::kLinear; }

  D3D12_SAMPLER_DESC d = {};
  if (aniso_filter != xenos::AnisoFilter::kDisabled) {
    d.Filter = D3D12_FILTER_ANISOTROPIC;
    d.MaxAnisotropy = 1u << (uint32_t(aniso_filter) - 1);
  } else {
    d.Filter = D3D12_ENCODE_BASIC_FILTER(min_linear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT, mag_linear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
                                         mip_linear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT, D3D12_FILTER_REDUCTION_TYPE_STANDARD);
    d.MaxAnisotropy = 1;
  }
  static const D3D12_TEXTURE_ADDRESS_MODE kAddressModeMap[] = {
      D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_MIRROR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE,
      D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE};
  d.AddressU = kAddressModeMap[uint32_t(cx) & 7]; d.AddressV = kAddressModeMap[uint32_t(cy) & 7]; d.AddressW = kAddressModeMap[uint32_t(cz) & 7];
  d.MipLODBias = 0.0f;   // LOD biasing is performed in shaders
  d.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  switch (border) {
    case xenos::BorderColor::k_ABGR_White: d.BorderColor[0] = d.BorderColor[1] = d.BorderColor[2] = d.BorderColor[3] = 1.0f; break;
    case xenos::BorderColor::k_ACBYCR_Black: d.BorderColor[0] = 0.5f; d.BorderColor[1] = 0.0f; d.BorderColor[2] = 0.5f; d.BorderColor[3] = 0.0f; break;
    case xenos::BorderColor::k_ACBCRY_Black: d.BorderColor[0] = 0.0f; d.BorderColor[1] = 0.5f; d.BorderColor[2] = 0.5f; d.BorderColor[3] = 0.0f; break;
    default: break;   // black, zeros
  }
  if (force_bc_w_to_max) d.BorderColor[3] = 1.0f;
  d.MinLOD = float(mip_min_level);
  if (mip_base_map) {
    d.MaxLOD = d.MinLOD;
    if (aniso_filter == xenos::AnisoFilter::kDisabled) d.MaxLOD += 0.25f;
  } else {
    d.MaxLOD = FLT_MAX;   // the maximum mip level is in the texture resource itself
  }
  return d;
}

}  // namespace

bool BuildDrawConstants(const uint32_t* raw, uint32_t draw_initiator_in, const PairTranslation& pt, bool primitive_polygonal,
                        uint32_t index_endian, uint32_t line_loop_closing_index, DrawConstants& out) {
  // THE SHADOW ITSELF, NOT A COPY (2026-09-26, PROF3 xperf): copying the 80 KB register file per draw was ~8% of the
  // async replay thread (~240 MB per lake frame). RegisterFile is exactly `uint32_t values[0x5003]` and the replay's
  // shadow is 0x5010 dwords, so the shadow is read in place; the two registers this function overrides are put back
  // on the way out. The caller's shadow is otherwise untouched (hence the const in the signature).
  static_assert(sizeof(RegisterFile) == sizeof(uint32_t) * RegisterFile::kRegisterCount, "RegisterFile must be exactly its values");
  RegisterFile& regs = *reinterpret_cast<RegisterFile*>(const_cast<uint32_t*>(raw));
  struct Restore {
    uint32_t* v; uint32_t di, wo;
    ~Restore() { v[XE_GPU_REG_VGT_DRAW_INITIATOR] = di; v[XE_GPU_REG_PA_SC_WINDOW_OFFSET] = wo; }
  } restore{regs.values, regs.values[XE_GPU_REG_VGT_DRAW_INITIATOR], regs.values[XE_GPU_REG_PA_SC_WINDOW_OFFSET]};
  regs.values[XE_GPU_REG_VGT_DRAW_INITIATOR] = draw_initiator_in;
  // SCREEN SPACE, NOT EDRAM SPACE (SDKZ14, 2026-09-25). Fable renders PREDICATED TILES: each tile's pass carries a
  // PA_SC_WINDOW_OFFSET (Y -512 for the second) that moves the screen into that tile's EDRAM window, where the
  // plugin's host targets live. The native targets are the full screen (the old path draws every pass unshifted),
  // so the offset is removed here for the viewport and the scissor: the second pass's arches otherwise landed 512
  // rows up, after that pass's depth clear, and the water and far trees drew over the spot. The scissor registers
  // are in screen coordinates before the offset is added (draw.cpp GetScissor), so they give the tile's own band.
  regs.values[XE_GPU_REG_PA_SC_WINDOW_OFFSET] = 0;

  auto pa_cl_clip_cntl = regs.Get<reg::PA_CL_CLIP_CNTL>();
  auto pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
  auto rb_colorcontrol = regs.Get<reg::RB_COLORCONTROL>();
  auto rb_depth_info = regs.Get<reg::RB_DEPTH_INFO>();
  auto rb_surface_info = regs.Get<reg::RB_SURFACE_INFO>();
  auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  const reg::RB_DEPTHCONTROL normalized_depth_control = draw_util::GetNormalizedDepthControl(regs);

  // full_float24_in_0_to_1 = FALSE, unlike the plugin (SDK5, 2026-09-25): the plugin squeezes D24FS8 depth into
  // [0, 0.5) so it survives its EDRAM ownership transfers; the native depth buffer is never transferred and is SHARED
  // with the old path's draws, which write the full [0, 1] - with the squeeze every SDK draw sat at half the depth of
  // its neighbours and the arches vanished under later geometry.
  // The viewport, otherwise exactly as the plugin asks for it (command_processor.cpp: origin bottom-left true, D3D12 bounds,
  // no reverse Z, host render targets). The native depth is D32_FLOAT, so no float24 conversion.
  draw_util::ViewportInfo vi;
  draw_util::GetHostViewportInfo(regs, 1, 1, true, D3D12_VIEWPORT_BOUNDS_MAX, D3D12_VIEWPORT_BOUNDS_MAX, false,
                                 normalized_depth_control, /*convert_z_to_float24=*/false,
                                 /*full_float24_in_0_to_1=*/false, pt.ps.writes_depth, vi);
  out.vp_x = vi.xy_offset[0]; out.vp_y = vi.xy_offset[1]; out.vp_w = vi.xy_extent[0]; out.vp_h = vi.xy_extent[1];
  out.vp_zmin = vi.z_min; out.vp_zmax = vi.z_max;
  draw_util::Scissor scissor;
  draw_util::GetScissor(regs, scissor);
  out.sc_x = scissor.offset[0]; out.sc_y = scissor.offset[1]; out.sc_w = scissor.extent[0]; out.sc_h = scissor.extent[1];
  out.msaa_samples = uint32_t(rb_surface_info.msaa_samples);

  DxbcShaderTranslator::SystemConstants sc;
  std::memset(&sc, 0, sizeof(sc));
  uint32_t flags = 0;
  if (pt.vs.memexport || pt.ps.memexport) flags |= DxbcShaderTranslator::kSysFlag_SharedMemoryIsUAV;
  if (pa_cl_vte_cntl.vtx_xy_fmt) flags |= DxbcShaderTranslator::kSysFlag_XYDividedByW;
  if (pa_cl_vte_cntl.vtx_z_fmt) flags |= DxbcShaderTranslator::kSysFlag_ZDividedByW;
  if (pa_cl_vte_cntl.vtx_w0_fmt) flags |= DxbcShaderTranslator::kSysFlag_WNotReciprocal;
  if (primitive_polygonal) flags |= DxbcShaderTranslator::kSysFlag_PrimitivePolygonal;
  if (draw_util::IsPrimitiveLine(regs)) flags |= DxbcShaderTranslator::kSysFlag_PrimitiveLine;
  if (rb_depth_info.depth_format == xenos::DepthRenderTargetFormat::kD24FS8) flags |= DxbcShaderTranslator::kSysFlag_DepthFloat24;
  const xenos::CompareFunction alpha_test_function = rb_colorcontrol.alpha_test_enable ? rb_colorcontrol.alpha_func : xenos::CompareFunction::kAlways;
  flags |= uint32_t(alpha_test_function) << DxbcShaderTranslator::kSysFlag_AlphaPassIfLess_Shift;
  // Gamma writing: the native scene target is RGBA16F, so k_8_8_8_8_GAMMA targets are converted in the shader, as
  // the plugin does when it does not keep gamma targets as UNORM16.
  for (uint32_t i = 0; i < 4; ++i) {
    auto ci = regs.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]);
    if (ci.color_format == xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA) flags |= DxbcShaderTranslator::kSysFlag_ConvertColor0ToGamma << i;
  }
  // (ROV depth/stencil flags: RTV path, not set.)
  sc.flags = flags;
  sc.tessellation_factor_range_min = regs.Get<float>(XE_GPU_REG_VGT_HOS_MIN_TESS_LEVEL) + 1.0f;
  sc.tessellation_factor_range_max = regs.Get<float>(XE_GPU_REG_VGT_HOS_MAX_TESS_LEVEL) + 1.0f;
  sc.line_loop_closing_index = line_loop_closing_index;
  sc.vertex_index_endian = xenos::Endian(index_endian);
  sc.vertex_index_offset = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  sc.vertex_index_min = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;
  sc.vertex_index_max = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;
  if (!pa_cl_clip_cntl.clip_disable) {
    float* w = sc.user_clip_planes[0];
    uint32_t rem = pa_cl_clip_cntl.ucp_ena;
    while (rem) {
      const uint32_t i = uint32_t(std::countr_zero(rem));
      rem &= ~(1u << i);
      std::memcpy(w, &regs[XE_GPU_REG_PA_CL_UCP_0_X + i * 4], 4 * sizeof(float));
      w += 4;
    }
  }
  for (uint32_t i = 0; i < 3; ++i) { sc.ndc_scale[i] = vi.ndc_scale[i]; sc.ndc_offset[i] = vi.ndc_offset[i]; }
  if (vgt_draw_initiator.prim_type == xenos::PrimitiveType::kPointList) {
    auto mm = regs.Get<reg::PA_SU_POINT_MINMAX>();
    auto ps = regs.Get<reg::PA_SU_POINT_SIZE>();
    sc.point_vertex_diameter_min = float(mm.min_size) * (2.0f / 16.0f);
    sc.point_vertex_diameter_max = float(mm.max_size) * (2.0f / 16.0f);
    sc.point_constant_diameter[0] = float(ps.width) * (2.0f / 16.0f);
    sc.point_constant_diameter[1] = float(ps.height) * (2.0f / 16.0f);
    sc.point_screen_diameter_to_ndc_radius[0] = 1.0f / float(std::max(vi.xy_extent[0], uint32_t(1)));
    sc.point_screen_diameter_to_ndc_radius[1] = 1.0f / float(std::max(vi.xy_extent[1], uint32_t(1)));
  }
  // Texture signs: the texture cache's GetActiveTextureSwizzledSigns is texture_util::SwizzleSigns(fetch constant).
  uint32_t rem = pt.vs.used_texture_mask | pt.ps.used_texture_mask;
  while (rem) {
    const uint32_t i = uint32_t(std::countr_zero(rem));
    rem &= ~(1u << i);
    const uint8_t signs = texture_util::SwizzleSigns(regs.GetTextureFetch(i));
    uint32_t& w = sc.texture_swizzled_signs[i >> 2];
    const uint32_t sh = (i & 3) * 8;
    w = (w & ~(0xFFu << sh)) | (uint32_t(signs) << sh);
  }
  sc.textures_resolution_scaled = 0;   // 1x
  // The HOST target's sample count, which is what the plugin passes (its host render targets carry the guest's MSAA).
  // Every native target is single-sampled, so 1x: in the RTV path the value only shapes alpha-to-mask coverage
  // (dxbc_translator_om.cpp:1830), and a 2x/4x pattern on a 1x target would keep sample 0's bit only.
  sc.sample_count_log2[0] = 0;
  sc.sample_count_log2[1] = 0;
  sc.alpha_test_reference = regs.Get<float>(XE_GPU_REG_RB_ALPHA_REF);
  sc.alpha_to_mask = rb_colorcontrol.alpha_to_mask_enable ? (rb_colorcontrol.value >> 24) | (1 << 8) : 0;
  for (uint32_t i = 0; i < 4; ++i) {
    auto ci = regs.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[i]);
    int32_t bias = ci.color_exp_bias;
    // RTV path, fixed16 not truncated to -1..1 (the plugin's default): 16_16 / 16_16_16_16 targets take -5.
    if (ci.color_format == xenos::ColorRenderTargetFormat::k_16_16 || ci.color_format == xenos::ColorRenderTargetFormat::k_16_16_16_16) bias -= 5;
    const int32_t bits = int32_t(0x3F800000 + (bias << 23));
    std::memcpy(&sc.color_exp_bias[i], &bits, 4);
  }
  out.system.resize(sizeof(sc));
  std::memcpy(out.system.data(), &sc, sizeof(sc));

  // b1: float constants in set-bit order (all 256 when the shader addresses them dynamically) - command_processor.cpp.
  // Sized once and filled by set bit (PROF4: a push_back per dword was ~10% of the async replay thread).
  auto pack = [&](const StageInfo& s, uint32_t base, std::vector<uint32_t>& dst) {
    if (s.float_dynamic) { dst.assign(&regs.values[base], &regs.values[base] + 256 * 4); return; }
    uint32_t n = 0;
    for (int w = 0; w < 4; ++w) n += uint32_t(std::popcount(s.float_bitmap[w]));
    if (!n) { dst.assign(4, 0); return; }   // at least 16 bytes, as the plugin allocates
    dst.resize(size_t(n) * 4);
    uint32_t* o = dst.data();
    for (int w = 0; w < 4; ++w)
      for (uint64_t bits = s.float_bitmap[w]; bits; bits &= bits - 1) {
        const uint32_t cidx = uint32_t(w) * 64 + uint32_t(std::countr_zero(bits));
        std::memcpy(o, &regs.values[base + cidx * 4], 16); o += 4;
      }
  };
  pack(pt.vs, XE_GPU_REG_SHADER_CONSTANT_000_X, out.float_vs);
  pack(pt.ps, XE_GPU_REG_SHADER_CONSTANT_256_X, out.float_ps);
  out.bool_loop.assign(&regs[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031], &regs[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031] + 40);
  out.fetch.assign(&regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0], &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0] + 192);
  out.smp_vs.clear(); out.smp_ps.clear();
  for (const auto& b : pt.vs.samplers) out.smp_vs.push_back(SamplerFor(regs, b));
  for (const auto& b : pt.ps.samplers) out.smp_ps.push_back(SamplerFor(regs, b));
  return true;
}

}  // namespace fable2::ngpu::sdk
