// PHASE A (docs/native_gpu/MIGRATION_SDK_CONTRACT_2026-09-25.md) - A2: translate a Xenos VS/PS PAIR with the SDK's own
// DxbcShaderTranslator (bindful, RTV path), with the modification bits computed the way the plugin's PipelineCache
// computes them (GetCurrentVertexShaderModification / GetCurrentPixelShaderModification, pipeline_cache.cpp), and
// keep what the draw path needs to bind the result. Cached per (VS hash, PS hash, VS modification, PS modification).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rex::graphics { class Shader; }

namespace fable2::ngpu::sdk {

// The draw's registers the modifications depend on (raw dwords, as the ring / bridge record holds them).
struct PairRegs {
  uint32_t sq_program_cntl = 0, sq_context_misc = 0, sq_interpolator_cntl = 0;
  uint32_t pa_cl_clip_cntl = 0, vgt_draw_initiator = 0;
  uint32_t rb_surface_info = 0, rb_depthcontrol = 0, rb_colorcontrol = 0, rb_depth_info = 0;
  uint32_t vgt_output_path_cntl = 0, vgt_hos_cntl = 0;   // tessellation (primitive_processor.cpp:287)
};

// The same registers read from a full register file (0x5003 dwords), with the DRAW packet's initiator.
PairRegs PairRegsFrom(const uint32_t* regs, uint32_t vgt_draw_initiator);

struct TextureBinding { uint32_t fetch_constant; uint32_t dimension; bool is_signed; };   // dimension = xenos::FetchOpDimension
struct SamplerBinding { uint32_t fetch_constant; uint32_t mag, min, mip, aniso; };
struct VertexBinding { uint32_t fetch_constant; uint32_t stride_words; };

struct StageInfo {
  std::vector<uint8_t> dxbc;
  uint64_t modification = 0;
  std::vector<TextureBinding> textures;   // bindful: binding i -> t(1 + i), space 0
  std::vector<SamplerBinding> samplers;   // bindful: binding i -> s(i)
  uint64_t float_bitmap[4] = {0, 0, 0, 0};   // used float constants (256 bits) (packed in set-bit order), unless dynamic
  uint32_t float_count = 0;
  bool float_dynamic = false;             // dynamic addressing: all 256 laid out unpacked
  std::vector<VertexBinding> vertex;      // VS only: the vertex fetch constants it reads (mirror requests)
  bool memexport = false;
  std::vector<uint32_t> memexport_consts;   // float constants holding the eA stream descriptors
  bool writes_depth = false;              // PS: the viewport depth range depends on it (GetHostViewportInfo)
  uint32_t used_texture_mask = 0;         // fetch slots whose sign bytes go into the system constants
};

struct PairTranslation {
  bool ok = false;
  std::string error;
  uint32_t interpolator_mask = 0, param_gen_pos = 0xFFFFFFFFu;
  // Tessellation: the VS runs as a DOMAIN shader of host_vs_type (Shader::HostVertexShaderType, 0 = plain vertex).
  bool tessellated = false;
  uint32_t host_vs_type = 0, tess_mode = 0;
  StageInfo vs, ps;
  // EDRAM port (2026-09-26): the analysed SDK vertex shader (persistent - owned by the translation cache) and the
  // pixel shader's writes_color_targets(), for RenderTargetCache::Update's inputs.
  const rex::graphics::Shader* vs_shader = nullptr;
  uint32_t ps_writes_color_targets = 0;
};

// ucode = the guest microcode as it sits in guest memory (big-endian dwords). Returns nullptr only when a shader
// cannot be analysed at all; a failed translation comes back with ok = false and the reason.
const PairTranslation* TranslatePair(uint64_t vs_hash, const uint32_t* vs_ucode_be, uint32_t vs_dwords,
                                     uint64_t ps_hash, const uint32_t* ps_ucode_be, uint32_t ps_dwords,
                                     const PairRegs& regs);

// The analysed SDK shader for a microcode hash (nullptr if not walkable) - see the .cpp.
const rex::graphics::Shader* AnalyzedShader(bool pixel, uint64_t hash, const uint32_t* ucode_be, uint32_t dwords);

struct XlatStats { uint64_t pairs = 0, ok = 0, failed = 0, cache_hits = 0, refused_ucode = 0; };
XlatStats GetXlatStats();
// The SDK analyser's microcode disassembly of an already-loaded shader (empty if unknown).
std::string GetUcodeDisassembly(uint64_t ucode_hash);

}  // namespace fable2::ngpu::sdk
