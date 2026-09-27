// PHASE A: tessellation for the SDK draw path - the plugin's fixed tessellation vertex shaders and hull shaders
// (vendored bytecode, native_gpu_xlat/bytecode), selected exactly as D3D12 PipelineCache::CreateD3D12Pipeline selects
// them (pipeline_cache.cpp:2733-2805). The domain shader is the guest VS translated with a domain host type.
#include "native_gpu_sdk_tess.h"

#include <windows.h>

#include <cstddef>

namespace fable2::ngpu::sdk {
namespace tess_bytecode {
#include "native_gpu_xlat/bytecode/adaptive_quad_hs.h"
#include "native_gpu_xlat/bytecode/adaptive_triangle_hs.h"
#include "native_gpu_xlat/bytecode/continuous_quad_1cp_hs.h"
#include "native_gpu_xlat/bytecode/continuous_quad_4cp_hs.h"
#include "native_gpu_xlat/bytecode/continuous_triangle_1cp_hs.h"
#include "native_gpu_xlat/bytecode/continuous_triangle_3cp_hs.h"
#include "native_gpu_xlat/bytecode/discrete_quad_1cp_hs.h"
#include "native_gpu_xlat/bytecode/discrete_quad_4cp_hs.h"
#include "native_gpu_xlat/bytecode/discrete_triangle_1cp_hs.h"
#include "native_gpu_xlat/bytecode/discrete_triangle_3cp_hs.h"
#include "native_gpu_xlat/bytecode/tessellation_adaptive_vs.h"
#include "native_gpu_xlat/bytecode/tessellation_indexed_vs.h"
}  // namespace tess_bytecode

// tess_mode: xenos::TessellationMode (0 discrete, 1 continuous, 2 adaptive).
// host_type: Shader::HostVertexShaderType (3 triangle CP-indexed, 4 triangle patch-indexed, 5 quad CP-indexed,
// 6 quad patch-indexed - the values of shader.h:640, checked in the caller with static_asserts).
bool GetTessShaders(uint32_t tess_mode, uint32_t host_type, TessShaders& out) {
  using namespace tess_bytecode;
  if (tess_mode == 2) { out.vs = tessellation_adaptive_vs; out.vs_len = sizeof(tessellation_adaptive_vs); }
  else { out.vs = tessellation_indexed_vs; out.vs_len = sizeof(tessellation_indexed_vs); }
  out.hs = nullptr; out.hs_len = 0;
  switch (tess_mode) {
    case 0:
      switch (host_type) {
        case 3: out.hs = discrete_triangle_3cp_hs; out.hs_len = sizeof(discrete_triangle_3cp_hs); break;
        case 4: out.hs = discrete_triangle_1cp_hs; out.hs_len = sizeof(discrete_triangle_1cp_hs); break;
        case 5: out.hs = discrete_quad_4cp_hs; out.hs_len = sizeof(discrete_quad_4cp_hs); break;
        case 6: out.hs = discrete_quad_1cp_hs; out.hs_len = sizeof(discrete_quad_1cp_hs); break;
        default: return false;
      }
      break;
    case 1:
      switch (host_type) {
        case 3: out.hs = continuous_triangle_3cp_hs; out.hs_len = sizeof(continuous_triangle_3cp_hs); break;
        case 4: out.hs = continuous_triangle_1cp_hs; out.hs_len = sizeof(continuous_triangle_1cp_hs); break;
        case 5: out.hs = continuous_quad_4cp_hs; out.hs_len = sizeof(continuous_quad_4cp_hs); break;
        case 6: out.hs = continuous_quad_1cp_hs; out.hs_len = sizeof(continuous_quad_1cp_hs); break;
        default: return false;
      }
      break;
    case 2:
      switch (host_type) {
        case 4: out.hs = adaptive_triangle_hs; out.hs_len = sizeof(adaptive_triangle_hs); break;
        case 6: out.hs = adaptive_quad_hs; out.hs_len = sizeof(adaptive_quad_hs); break;
        default: return false;
      }
      break;
    default:
      return false;
  }
  return true;
}

}  // namespace fable2::ngpu::sdk
