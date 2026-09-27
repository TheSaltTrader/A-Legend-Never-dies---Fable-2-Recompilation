// PHASE A - A2: VS/PS pair translation through the SDK's DxbcShaderTranslator. See native_gpu_sdk_xlat.h.
#include "native_gpu_sdk_xlat.h"

#include <rex/graphics/pipeline/shader/dxbc.h>
#include <rex/graphics/pipeline/shader/dxbc_translator.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/util/draw.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>
#include <rex/string/buffer.h>
#include <rex/ui/graphics_provider.h>

#include <bit>
#include <map>
#include <memory>
#include <mutex>
#include <tuple>

namespace fable2::ngpu::shader_census { const char* ValidateShaderUcode(const std::vector<uint32_t>& host_endian_ucode); }

namespace fable2::ngpu::sdk {

using namespace rex::graphics;

namespace {

// One analysed shader per microcode hash: analysis is modification-independent, and a DxbcShader caches one
// Translation per modification value - exactly the plugin's arrangement (PipelineCache::LoadShader).
struct Loaded { std::unique_ptr<DxbcShader> shader; bool analyzed = false; };
std::map<uint64_t, Loaded> g_shaders;
std::map<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>, PairTranslation> g_pairs;
std::mutex g_lock;
XlatStats g_stats;

DxbcShaderTranslator& Translator() {
  // Bindful, RTV (no ROV), no gamma-as-sRGB, 2x MSAA supported, 1x resolution scale - the plugin's own defaults on
  // this machine for the RTV path. One translator instance, reused (it resets itself per translation).
  static DxbcShaderTranslator t(rex::ui::GraphicsProvider::GpuVendorID::kNvidia, /*bindless=*/false, /*rov=*/false);
  return t;
}

DxbcShader* Load(xenos::ShaderType type, uint64_t hash, const uint32_t* ucode_be, uint32_t dwords) {
  Loaded& l = g_shaders[hash];
  if (!l.shader) {
    l.shader = std::make_unique<DxbcShader>(type, hash, ucode_be, dwords, std::endian::big);
    // The census's structural guard first: a blob that is not a walkable Xenos shader is never analysed (ALL1 crashed
    // in AnalyzeUcode's disassembler on one). Refused = not analysed; the caller draws it on the old path.
    if (const char* why = fable2::ngpu::shader_census::ValidateShaderUcode(l.shader->ucode_data())) {
      ++g_stats.refused_ucode;
      REXLOG_INFO("[ngpu] SDK path: {} shader {:016X} ({} dw) refused before analysis: {}", type == xenos::ShaderType::kPixel ? "pixel" : "vertex", hash, dwords, why);
      return nullptr;
    }
    rex::string::StringBuffer disasm(4096);
    l.shader->AnalyzeUcode(disasm);
    l.analyzed = l.shader->is_ucode_analyzed();
  }
  return l.analyzed ? l.shader.get() : nullptr;
}

void FillStage(StageInfo& s, const DxbcShader& sh, const Shader::Translation& t, uint64_t mod) {
  s.dxbc = t.translated_binary();
  s.modification = mod;
  for (const auto& b : sh.GetTextureBindingsAfterTranslation())
    s.textures.push_back({b.fetch_constant, uint32_t(b.dimension), b.is_signed});
  for (const auto& b : sh.GetSamplerBindingsAfterTranslation())
    s.samplers.push_back({b.fetch_constant, uint32_t(b.mag_filter), uint32_t(b.min_filter), uint32_t(b.mip_filter), uint32_t(b.aniso_filter)});
  const auto& cm = sh.constant_register_map();
  for (int i = 0; i < 4; ++i) s.float_bitmap[i] = cm.float_bitmap[i];
  s.float_count = cm.float_count;
  s.float_dynamic = cm.float_dynamic_addressing;
  for (const auto& vb : sh.vertex_bindings()) s.vertex.push_back({vb.fetch_constant, vb.stride_words});
  s.memexport = sh.memexport_eM_written() != 0;
  s.memexport_consts.assign(sh.memexport_stream_constants().begin(), sh.memexport_stream_constants().end());
  s.writes_depth = sh.writes_depth();
  s.used_texture_mask = sh.GetUsedTextureMaskAfterTranslation();
}

}  // namespace

// The analysed SDK shader for a microcode hash (loaded and analysed on first use, as the pair path does), or nullptr
// when it is not a walkable shader. For the vendored render-target cache's Update, which needs every draw's VS.
const Shader* AnalyzedShader(bool pixel, uint64_t hash, const uint32_t* ucode_be, uint32_t dwords) {
  std::lock_guard<std::mutex> lk(g_lock);
  return Load(pixel ? xenos::ShaderType::kPixel : xenos::ShaderType::kVertex, hash, ucode_be, dwords);
}

const PairTranslation* TranslatePair(uint64_t vs_hash, const uint32_t* vs_ucode_be, uint32_t vs_dwords,
                                     uint64_t ps_hash, const uint32_t* ps_ucode_be, uint32_t ps_dwords,
                                     const PairRegs& r) {
  std::lock_guard<std::mutex> lk(g_lock);
  DxbcShader* vs = Load(xenos::ShaderType::kVertex, vs_hash, vs_ucode_be, vs_dwords);
  DxbcShader* ps = ps_ucode_be && ps_dwords ? Load(xenos::ShaderType::kPixel, ps_hash, ps_ucode_be, ps_dwords) : nullptr;
  if (!vs || (ps_ucode_be && ps_dwords && !ps)) return nullptr;

  reg::SQ_PROGRAM_CNTL sq_program_cntl; sq_program_cntl.value = r.sq_program_cntl;
  reg::SQ_CONTEXT_MISC sq_context_misc; sq_context_misc.value = r.sq_context_misc;
  reg::SQ_INTERPOLATOR_CNTL sq_interpolator_cntl; sq_interpolator_cntl.value = r.sq_interpolator_cntl;
  reg::PA_CL_CLIP_CNTL pa_cl_clip_cntl; pa_cl_clip_cntl.value = r.pa_cl_clip_cntl;
  reg::VGT_DRAW_INITIATOR vgt_draw_initiator; vgt_draw_initiator.value = r.vgt_draw_initiator;
  reg::RB_SURFACE_INFO rb_surface_info; rb_surface_info.value = r.rb_surface_info;
  reg::RB_DEPTHCONTROL rb_depthcontrol; rb_depthcontrol.value = r.rb_depthcontrol;
  reg::RB_COLORCONTROL rb_colorcontrol; rb_colorcontrol.value = r.rb_colorcontrol;

  // The interpolators the pair exchanges (command_processor.cpp: vs.writes_interpolators() & the PS's input mask).
  uint32_t param_gen_pos = UINT32_MAX;
  const uint32_t interpolator_mask = ps ? (vs->writes_interpolators() & ps->GetInterpolatorInputMask(sq_program_cntl, sq_context_misc, param_gen_pos)) : 0;

  // HOST VERTEX SHADER TYPE - PrimitiveProcessor::Process (primitive_processor.cpp:285-340): tessellated when the major
  // mode is explicit and VGT_OUTPUT_PATH_CNTL selects tessellation; the VS then runs as the DOMAIN shader.
  Shader::HostVertexShaderType host_type = Shader::HostVertexShaderType::kVertex;
  const auto prim = vgt_draw_initiator.prim_type;
  reg::VGT_OUTPUT_PATH_CNTL path; path.value = r.vgt_output_path_cntl;
  reg::VGT_HOS_CNTL hos; hos.value = r.vgt_hos_cntl;
  const bool tessellated = xenos::IsMajorModeExplicit(vgt_draw_initiator.major_mode, prim) &&
                           path.path_select == xenos::VGTOutputPath::kTessellationEnable;
  if (tessellated) {
    const bool cp_ok = hos.tess_mode == xenos::TessellationMode::kDiscrete || hos.tess_mode == xenos::TessellationMode::kContinuous;
    switch (prim) {
      case xenos::PrimitiveType::kTriangleList: case xenos::PrimitiveType::kTriangleFan: case xenos::PrimitiveType::kTriangleStrip:
        if (cp_ok) host_type = Shader::HostVertexShaderType::kTriangleDomainCPIndexed; break;
      case xenos::PrimitiveType::kQuadList:
        if (cp_ok) host_type = Shader::HostVertexShaderType::kQuadDomainCPIndexed; break;
      case xenos::PrimitiveType::kTrianglePatch: host_type = Shader::HostVertexShaderType::kTriangleDomainPatchIndexed; break;
      case xenos::PrimitiveType::kQuadPatch: host_type = Shader::HostVertexShaderType::kQuadDomainPatchIndexed; break;
      default: break;
    }
    if (host_type == Shader::HostVertexShaderType::kVertex) return nullptr;   // unsupported tessellation (the plugin refuses it too)
  }

  // VS modification - PipelineCache::GetCurrentVertexShaderModification, line for line.
  DxbcShaderTranslator::Modification vmod(Translator().GetDefaultVertexShaderModification(
      vs->GetDynamicAddressableRegisterCount(sq_program_cntl.vs_num_reg), host_type));
  vmod.vertex.interpolator_mask = interpolator_mask;
  const uint32_t user_clip_planes = pa_cl_clip_cntl.clip_disable ? 0 : pa_cl_clip_cntl.ucp_ena;
  vmod.vertex.user_clip_plane_count = std::popcount(user_clip_planes);
  vmod.vertex.user_clip_plane_cull = uint32_t(user_clip_planes && pa_cl_clip_cntl.ucp_cull_only_ena);
  vmod.vertex.point_ps_ucp_mode = pa_cl_clip_cntl.ps_ucp_mode;
  vmod.vertex.vertex_kill_and = uint32_t((vs->writes_point_size_edge_flag_kill_vertex() & 0b100) && !pa_cl_clip_cntl.vtx_kill_or);
  vmod.vertex.output_point_size = uint32_t((vs->writes_point_size_edge_flag_kill_vertex() & 0b001) &&
                                           vgt_draw_initiator.prim_type == xenos::PrimitiveType::kPointList);

  // PS modification - PipelineCache::GetCurrentPixelShaderModification, host-render-target (RTV) branch. The native
  // path's depth is D32_FLOAT, so the float24 conversion modes do not apply: early hint or no modifiers.
  DxbcShaderTranslator::Modification pmod(0);
  if (ps) {
    pmod = DxbcShaderTranslator::Modification(Translator().GetDefaultPixelShaderModification(
        ps->GetDynamicAddressableRegisterCount(sq_program_cntl.ps_num_reg)));
    pmod.pixel.interpolator_mask = interpolator_mask;
    pmod.pixel.interpolators_centroid = interpolator_mask & ~xenos::GetInterpolatorSamplingPattern(
        rb_surface_info.msaa_samples, sq_context_misc.sc_sample_cntl, sq_interpolator_cntl.sampling_pattern);
    if (param_gen_pos < xenos::kMaxInterpolators) {
      pmod.pixel.param_gen_enable = 1;
      pmod.pixel.param_gen_interpolator = param_gen_pos;
      pmod.pixel.param_gen_point = uint32_t(vgt_draw_initiator.prim_type == xenos::PrimitiveType::kPointList);
    }
    using DSM = DxbcShaderTranslator::Modification::DepthStencilMode;
    pmod.pixel.depth_stencil_mode = (ps->implicit_early_z_write_allowed() &&
                                     (!ps->writes_color_target(0) || !draw_util::DoesCoverageDependOnAlpha(rb_colorcontrol)))
                                        ? DSM::kEarlyHint : DSM::kNoModifiers;
  }

  const auto key = std::make_tuple(vs_hash, ps ? ps_hash : 0ull, vmod.value, pmod.value);
  auto it = g_pairs.find(key);
  if (it != g_pairs.end()) { ++g_stats.cache_hits; return &it->second; }
  PairTranslation& out = g_pairs[key];
  ++g_stats.pairs;
  out.interpolator_mask = interpolator_mask;
  out.tessellated = tessellated;
  out.host_vs_type = uint32_t(host_type);
  out.tess_mode = uint32_t(hos.tess_mode);
  out.param_gen_pos = param_gen_pos;
  out.vs_shader = vs;   // the analysed SDK shaders, for the vendored render-target cache (Update / extent estimate)
  out.ps_writes_color_targets = ps ? ps->writes_color_targets() : 0u;
  auto translate = [&](DxbcShader* sh, uint64_t mod, StageInfo& st, const char* what) -> bool {
    Shader::Translation* t = sh->GetOrCreateTranslation(mod);
    if (!t) { out.error = std::string(what) + ": no translation object"; return false; }
    if (!t->is_translated() && !Translator().TranslateAnalyzedShader(*t)) { out.error = std::string(what) + ": translator returned false"; return false; }
    if (!t->is_valid() || t->translated_binary().empty()) { out.error = std::string(what) + ": translation not valid"; return false; }
    FillStage(st, *sh, *t, mod);
    return true;
  };
  out.ok = translate(vs, vmod.value, out.vs, "VS") && (!ps || translate(ps, pmod.value, out.ps, "PS"));
  (out.ok ? ++g_stats.ok : ++g_stats.failed);
  if (!out.ok) REXLOG_INFO("[ngpu] SDK path: pair VS {:016X} PS {:016X} did not translate: {}", vs_hash, ps_hash, out.error);
  return &out;
}

PairRegs PairRegsFrom(const uint32_t* regs, uint32_t vgt_draw_initiator) {
  PairRegs r;
  r.sq_program_cntl = regs[XE_GPU_REG_SQ_PROGRAM_CNTL];
  r.sq_context_misc = regs[XE_GPU_REG_SQ_CONTEXT_MISC];
  r.sq_interpolator_cntl = regs[XE_GPU_REG_SQ_INTERPOLATOR_CNTL];
  r.pa_cl_clip_cntl = regs[XE_GPU_REG_PA_CL_CLIP_CNTL];
  r.vgt_draw_initiator = vgt_draw_initiator;
  r.rb_surface_info = regs[XE_GPU_REG_RB_SURFACE_INFO];
  r.rb_depthcontrol = regs[XE_GPU_REG_RB_DEPTHCONTROL];
  r.rb_colorcontrol = regs[XE_GPU_REG_RB_COLORCONTROL];
  r.rb_depth_info = regs[XE_GPU_REG_RB_DEPTH_INFO];
  r.vgt_output_path_cntl = regs[XE_GPU_REG_VGT_OUTPUT_PATH_CNTL];
  r.vgt_hos_cntl = regs[XE_GPU_REG_VGT_HOS_CNTL];
  return r;
}

std::string GetUcodeDisassembly(uint64_t h) {
  std::lock_guard<std::mutex> lk(g_lock);
  auto it = g_shaders.find(h);
  return it != g_shaders.end() && it->second.shader ? it->second.shader->ucode_disassembly() : std::string();
}

XlatStats GetXlatStats() { std::lock_guard<std::mutex> lk(g_lock); return g_stats; }

}  // namespace fable2::ngpu::sdk
