#pragma once
// EDRAM PORT (user decision 2026-09-26): the SDK render-target cache (vendored from rexglue-src - see
// native_gpu_xlat/ORIGIN.txt) as the native renderer's EDRAM model.
//
// PHASE 1 (this file, ngpu_rtc_shadow): the vendored base cache runs BESIDE the replay with stub render targets -
// no textures, no transfers - so its ownership decisions can be compared line for line with the plugin's own
// (REX_OWNER_TRACE_TILE; the vendored copy logs "[owner-trace-native]"). Nothing it decides reaches the picture yet.
#include <cstdint>

namespace rex::graphics { class Shader; }

namespace fable2::ngpu::rtc {

// Per replayed SDK draw, BEFORE it is issued (the plugin calls Update before binding): regs = the replay's register
// shadow (>= 0x5003 dwords), vs = the analysed SDK vertex shader, ps_writes_color_targets = the pixel shader's
// writes_color_targets() (0 when there is none), primitive_polygonal as the plugin's primitive processor reports it.
void ShadowOnDraw(const uint32_t* regs, const rex::graphics::Shader& vs, uint32_t ps_writes_color_targets, bool primitive_polygonal);
// Once per replayed frame.
void ShadowBeginFrame();
// Counters for the log line.
struct ShadowStats { uint64_t updates = 0, update_failed = 0, not_rasterizing = 0; };
ShadowStats GetShadowStats();

}  // namespace fable2::ngpu::rtc
