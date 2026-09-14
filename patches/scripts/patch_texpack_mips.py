"""s66 - a texture-pack replacement gets a full mip chain, generated on the
GPU (a 2x2 box compute pass per level, src/graphics/shaders/texpack_mip.cs.hlsl
compiled with fxc to shaders/bytecode/d3d12_5_1/texpack_mip_cs.h) right after
its level-0 upload. The replacements were single-level resources: a 2x
texture sampled at distance with no smaller levels aliases, and far grass
looked like a television with a bad signal (2026-09-14). The SRV of a
replacement now exposes every level.
APPLY ONCE (requires patch_texpack_reverify.py)."""
import os
R = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"

def read(rel):
    return open(os.path.join(R, rel), encoding="utf-8").read()

def write(rel, s):
    open(os.path.join(R, rel), "w", encoding="utf-8", newline="").write(s)
    print("patched", rel)

def rep(s, old, new):
    assert s.count(old) == 1, ("expected 1, found %d: %s" % (s.count(old), old[:90]))
    return s.replace(old, new)

assert os.path.isfile(os.path.join(R, "src/graphics/shaders/bytecode/d3d12_5_1/texpack_mip_cs.h")), \
    "compile texpack_mip.cs.hlsl first (fxc /T cs_5_1 /E main /O3 /Fh ... /Vn texpack_mip_cs)"

# 1. header: the pass and its objects
rel = "include/rex/graphics/d3d12/texture_cache.h"
s = read(rel)
assert "TexpackGenerateMips" not in s, "already applied"
s = rep(s, "  void TexpackReverify(D3D12Texture& texture);\n",
        '''  void TexpackReverify(D3D12Texture& texture);
  // [texpack] The mip chain of a replacement: a 2x2 box compute pass per
  // level, recorded after the level-0 upload; leaves every level sampled.
  bool TexpackMipInit();
  bool TexpackGenerateMips(ID3D12Resource* res, uint32_t w, uint32_t h, uint32_t levels);
  Microsoft::WRL::ComPtr<ID3D12RootSignature> texpack_mip_root_signature_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> texpack_mip_pipeline_;
  bool texpack_mip_init_tried_ = false;
''')
write(rel, s)

# 2. the cache
rel = "src/graphics/d3d12/texture_cache.cpp"
s = read(rel)
s = rep(s, "#include <rex/graphics/d3d12/texture_cache.h>\n",
        '''#include <rex/graphics/d3d12/texture_cache.h>

namespace texpack_shaders {
#include "../shaders/bytecode/d3d12_5_1/texpack_mip_cs.h"
}  // namespace texpack_shaders
''')

# the resource: the full chain, writable by the pass
s = rep(s, '''  rdesc.MipLevels = 1;
  rdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
''', '''  // The full chain: a 2x texture with one level aliases at distance.
  uint32_t texpack_levels = 1;
  while ((std::max<uint32_t>(w, h) >> texpack_levels) >= 1u) ++texpack_levels;
  rdesc.MipLevels = UINT16(texpack_levels);
  rdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
''')
s = rep(s, "  rdesc.Flags = D3D12_RESOURCE_FLAG_NONE;\n",
        "  rdesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;\n")

# after the level-0 copy: the pass (which also does the final transition)
s = rep(s, '''  cl.D3DCopyTextureRegion(&dstloc, 0, 0, 0, &srcloc, nullptr);
  command_processor_.PushTransitionBarrier(
      res.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  command_processor_.SubmitBarriers();
''', '''  cl.D3DCopyTextureRegion(&dstloc, 0, 0, 0, &srcloc, nullptr);
  TexpackGenerateMips(res.Get(), w, h, texpack_levels);
''')

# the SRV of a replacement: every level
s = rep(s, "  uint32_t mip_levels = srv_replacement ? 1u : texture_key.mip_max_level + 1;\n",
        "  uint32_t mip_levels = srv_replacement ? uint32_t(-1) : texture_key.mip_max_level + 1;\n")

# the pass
s = rep(s, "static double TexpackNowSeconds() {\n", '''// [texpack] Mip chain for a replacement. The pack files hold one level; a 2x
// texture sampled at distance with no smaller levels aliases - far grass
// looked like a television with a bad signal (2026-09-14). A 2x2 box compute
// pass per level, recorded right after the level-0 upload.
bool D3D12TextureCache::TexpackMipInit() {
  if (texpack_mip_init_tried_) return texpack_mip_pipeline_ != nullptr;
  texpack_mip_init_tried_ = true;
  const ui::d3d12::D3D12Provider& provider = command_processor_.GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();
  D3D12_ROOT_PARAMETER params[3] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].Constants.ShaderRegister = 0;
  params[0].Constants.RegisterSpace = 0;
  params[0].Constants.Num32BitValues = 4;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_DESCRIPTOR_RANGE src_range = {};
  src_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  src_range.NumDescriptors = 1;
  src_range.BaseShaderRegister = 0;
  src_range.RegisterSpace = 0;
  src_range.OffsetInDescriptorsFromTableStart = 0;
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges = &src_range;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_DESCRIPTOR_RANGE dst_range = {};
  dst_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  dst_range.NumDescriptors = 1;
  dst_range.BaseShaderRegister = 0;
  dst_range.RegisterSpace = 0;
  dst_range.OffsetInDescriptorsFromTableStart = 0;
  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[2].DescriptorTable.NumDescriptorRanges = 1;
  params[2].DescriptorTable.pDescriptorRanges = &dst_range;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC rs_desc = {};
  rs_desc.NumParameters = 3;
  rs_desc.pParameters = params;
  rs_desc.NumStaticSamplers = 0;
  rs_desc.pStaticSamplers = nullptr;
  rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  *(texpack_mip_root_signature_.ReleaseAndGetAddressOf()) =
      ui::d3d12::util::CreateRootSignature(provider, rs_desc);
  if (!texpack_mip_root_signature_) {
    REXLOG_ERROR("[texpack] mip root signature failed; replacements get no mip chain");
    return false;
  }
  *(texpack_mip_pipeline_.ReleaseAndGetAddressOf()) = ui::d3d12::util::CreateComputePipeline(
      device, texpack_shaders::texpack_mip_cs, sizeof(texpack_shaders::texpack_mip_cs),
      texpack_mip_root_signature_.Get());
  if (!texpack_mip_pipeline_) {
    REXLOG_ERROR("[texpack] mip pipeline failed; replacements get no mip chain");
    return false;
  }
  REXLOG_INFO("[texpack] mip chains for replacements: compute pass ready");
  return true;
}

// The resource arrives with every level in COPY_DEST and level 0 just
// copied; every level is left in PIXEL|NON_PIXEL_SHADER_RESOURCE whatever
// happens (a failure leaves the smaller levels unwritten, and is logged).
bool D3D12TextureCache::TexpackGenerateMips(ID3D12Resource* res, uint32_t w, uint32_t h,
                                            uint32_t levels) {
  const D3D12_RESOURCE_STATES kSampled = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  ui::d3d12::util::DescriptorCpuGpuHandlePair descs[2 * 16];
  const uint32_t passes = levels > 1 ? std::min<uint32_t>(levels - 1, 16) : 0;
  const bool ok = passes > 0 && TexpackMipInit() &&
                  command_processor_.RequestOneUseSingleViewDescriptors(2 * passes, descs);
  if (!ok) {
    static std::atomic<uint32_t> n{0};
    if (passes > 0 && ++n <= 3)
      REXLOG_WARN("[texpack] no mip chain for a {}x{} replacement (descriptors or pipeline)",
                  w, h);
    command_processor_.PushTransitionBarrier(res, D3D12_RESOURCE_STATE_COPY_DEST, kSampled);
    command_processor_.SubmitBarriers();
    return false;
  }
  ID3D12Device* device = command_processor_.GetD3D12Provider().GetDevice();
  DeferredCommandList& cl = command_processor_.GetDeferredCommandList();
  command_processor_.PushTransitionBarrier(res, D3D12_RESOURCE_STATE_COPY_DEST,
                                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0);
  for (uint32_t i = 1; i <= passes; ++i)
    command_processor_.PushTransitionBarrier(res, D3D12_RESOURCE_STATE_COPY_DEST,
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS, i);
  command_processor_.SubmitBarriers();
  cl.D3DSetComputeRootSignature(texpack_mip_root_signature_.Get());
  command_processor_.SetExternalPipeline(texpack_mip_pipeline_.Get());
  uint32_t sw = w, sh = h;
  for (uint32_t i = 1; i <= passes; ++i) {
    const uint32_t dw = std::max<uint32_t>(sw / 2, 1), dh = std::max<uint32_t>(sh / 2, 1);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MostDetailedMip = i - 1;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(res, &srv, descs[2 * (i - 1)].first);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav.Texture2D.MipSlice = i;
    device->CreateUnorderedAccessView(res, nullptr, &uav, descs[2 * (i - 1) + 1].first);
    const uint32_t constants[4] = {dw, dh, sw - 1, sh - 1};
    cl.D3DSetComputeRoot32BitConstants(0, 4, constants, 0);
    cl.D3DSetComputeRootDescriptorTable(1, descs[2 * (i - 1)].second);
    cl.D3DSetComputeRootDescriptorTable(2, descs[2 * (i - 1) + 1].second);
    cl.D3DDispatch((dw + 7) / 8, (dh + 7) / 8, 1);
    command_processor_.PushUAVBarrier(res);
    command_processor_.PushTransitionBarrier(res, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, i);
    command_processor_.SubmitBarriers();
    sw = dw;
    sh = dh;
  }
  for (uint32_t i = passes + 1; i < levels; ++i)  // beyond 16 passes: never in practice
    command_processor_.PushTransitionBarrier(res, D3D12_RESOURCE_STATE_COPY_DEST, kSampled, i);
  for (uint32_t i = 0; i <= passes; ++i)
    command_processor_.PushTransitionBarrier(res, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                             kSampled, i);
  command_processor_.SubmitBarriers();
  return true;
}

static double TexpackNowSeconds() {
''')
write(rel, s)
print("done")
