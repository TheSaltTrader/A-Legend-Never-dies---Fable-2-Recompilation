// PHASE A - A4: the per-draw constant buffers an SDK-translated pair reads (MIGRATION_SDK_CONTRACT sections 2-4),
// computed from the draw's registers with the SDK's own structs and helpers (RegisterFile, draw_util,
// DxbcShaderTranslator::SystemConstants, texture_util::SwizzleSigns) - the plugin's UpdateSystemConstantValues,
// RTV path, ported line for line; the ROV-only fields stay zero.
#pragma once

#include <d3d12.h>

#include <cstdint>
#include <vector>

#include "native_gpu_sdk_xlat.h"

namespace fable2::ngpu::sdk {

struct DrawConstants {
  std::vector<uint8_t> system;        // b0: DxbcShaderTranslator::SystemConstants, byte for byte
  std::vector<uint32_t> float_vs;     // b1 (vertex): packed float4s, set-bit order (or all 256 if dynamic)
  std::vector<uint32_t> float_ps;     // b1 (pixel)
  std::vector<uint32_t> bool_loop;    // b2: 40 raw dwords from SHADER_CONSTANT_BOOL_000_031
  std::vector<uint32_t> fetch;        // b3: 192 raw dwords from SHADER_CONSTANT_FETCH_00_0
  // The host viewport the plugin would set (draw_util::GetHostViewportInfo, integer bounds + depth range).
  uint32_t vp_x = 0, vp_y = 0, vp_w = 0, vp_h = 0;
  float vp_zmin = 0.0f, vp_zmax = 1.0f;
  // The scissor the plugin sets (draw_util::GetScissor, clamped to the surface pitch).
  uint32_t sc_x = 0, sc_y = 0, sc_w = 0, sc_h = 0;
  uint32_t msaa_samples = 0;   // RB_SURFACE_INFO.msaa_samples (xenos::MsaaSamples)
  // One sampler per binding, in the translation's bindful order (s0..): D3D12TextureCache::GetSamplerParameters +
  // WriteSampler, ported (texture_cache.cpp:1697 / :1790).
  std::vector<D3D12_SAMPLER_DESC> smp_vs, smp_ps;
};

// regs = the draw's full register file (at least 0x5003 dwords, the replay's register shadow). Read IN PLACE: two
// registers (VGT_DRAW_INITIATOR, PA_SC_WINDOW_OFFSET) are overridden during the call and restored before it returns.
// primitive_polygonal / index_endian / line_loop_closing_index = what the plugin's primitive processor reports.
// vgt_draw_initiator = the DRAW packet's initiator (the replay record's), written over the shadow's register.
bool BuildDrawConstants(const uint32_t* regs, uint32_t vgt_draw_initiator, const PairTranslation& pt, bool primitive_polygonal,
                        uint32_t index_endian, uint32_t line_loop_closing_index, DrawConstants& out);

}  // namespace fable2::ngpu::sdk
