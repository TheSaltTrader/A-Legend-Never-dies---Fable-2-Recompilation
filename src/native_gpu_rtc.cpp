// EDRAM PORT, phase 1 - see native_gpu_rtc.h.
#include "native_gpu_rtc.h"

#include <rex/graphics/pipeline/render_target/cache.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/util/draw.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include <memory>

namespace fable2::ngpu::xlat { bool PluginBool(const char* name, bool fallback); }

namespace fable2::ngpu::rtc {

using namespace rex::graphics;

namespace {

// The plugin's D3D12RenderTargetCache answers for the pure virtuals (d3d12/render_target_cache.cpp/.h at f495e67):
// host render targets (the plugin runs the RTV path - its owner trace logged "draw" transfers, which the ROV path
// never produces), D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION, and the two flag-driven answers from the plugin's cvars.
class ShadowCache : public RenderTargetCache {
  class ShadowRenderTarget : public RenderTarget {   // a stub: phase 1 decides ownership, nothing is drawn
   public:
    explicit ShadowRenderTarget(RenderTargetKey key) : RenderTarget(key) {}
  };

 public:
  ShadowCache(const RegisterFile& rf, const rex::memory::Memory& memory) : RenderTargetCache(rf, memory, 1, 1) {
    depth_float24_convert_in_pixel_shader_ = xlat::PluginBool("depth_float24_convert_in_pixel_shader", false);
    gamma_render_target_as_unorm16_ = xlat::PluginBool("gamma_render_target_as_unorm16", true);
    InitializeCommon();
  }
  Path GetPath() const override { return Path::kHostRenderTargets; }

 protected:
  uint32_t GetMaxRenderTargetWidth() const override { return 16384; }
  uint32_t GetMaxRenderTargetHeight() const override { return 16384; }
  RenderTarget* CreateRenderTarget(RenderTargetKey key) override { return new ShadowRenderTarget(key); }
  bool IsHostDepthEncodingDifferent(xenos::DepthRenderTargetFormat format) const override {
    return format == xenos::DepthRenderTargetFormat::kD24FS8 && !depth_float24_convert_in_pixel_shader_;
  }
  bool IsGammaFormatHostStorageSeparate() const override { return gamma_render_target_as_unorm16_; }

 private:
  bool depth_float24_convert_in_pixel_shader_ = false;
  bool gamma_render_target_as_unorm16_ = true;
};

std::unique_ptr<ShadowCache> g_cache;
const uint32_t* g_cache_regs = nullptr;
ShadowStats g_stats;

ShadowCache* Cache(const uint32_t* regs) {
  if (g_cache && g_cache_regs == regs) return g_cache.get();
  // RegisterFile is exactly uint32_t values[0x5003] (static_assert in native_gpu_sdk_consts.cpp); the replay's
  // shadow is 0x5010 dwords, read in place.
  const RegisterFile& rf = *reinterpret_cast<const RegisterFile*>(regs);
  g_cache = std::make_unique<ShadowCache>(rf, *rex::system::kernel_state()->memory());
  g_cache_regs = regs;
  REXLOG_INFO("[ngpu] EDRAM PORT phase 1: vendored render-target cache created (ownership shadow, stub targets)");
  return g_cache.get();
}

}  // namespace

void ShadowOnDraw(const uint32_t* regs, const Shader& vs, uint32_t ps_writes_color_targets, bool primitive_polygonal) {
  ShadowCache* c = Cache(regs);
  const RegisterFile& rf = *reinterpret_cast<const RegisterFile*>(regs);
  // command_processor.cpp IssueDraw (f495e67 ~3040-3111): the same three inputs, computed the same way.
  const bool is_rasterization_done = draw_util::IsRasterizationPotentiallyDone(rf, primitive_polygonal);
  if (!is_rasterization_done) ++g_stats.not_rasterizing;
  const reg::RB_DEPTHCONTROL normalized_depth_control = draw_util::GetNormalizedDepthControl(rf);
  const uint32_t normalized_color_mask = ps_writes_color_targets ? draw_util::GetNormalizedColorMask(rf, ps_writes_color_targets) : 0u;
  ++g_stats.updates;
  if (!c->Update(is_rasterization_done, normalized_depth_control, normalized_color_mask, vs)) ++g_stats.update_failed;
}

void ShadowBeginFrame() {
  if (g_cache) g_cache->BeginFrame();
}

ShadowStats GetShadowStats() { return g_stats; }

}  // namespace fable2::ngpu::rtc
