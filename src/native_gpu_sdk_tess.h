// PHASE A: the plugin's tessellation VS/HS (vendored bytecode) for SDK-path patch draws. See native_gpu_sdk_tess.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

namespace fable2::ngpu::sdk {

struct TessShaders { const void* vs; size_t vs_len; const void* hs; size_t hs_len; };
bool GetTessShaders(uint32_t tess_mode, uint32_t host_vertex_shader_type, TessShaders& out);

}  // namespace fable2::ngpu::sdk
