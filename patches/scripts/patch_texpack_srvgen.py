"""s67 - a texture whose pack resource changes (replaced by another file,
retired, or recreated) makes the command processor rewrite the shader's
descriptor indices in the same draw. Until now the indices were compared by
texture KEY only, and a replacement change kept the key: the draw went on
sampling the previous descriptor - the previous picture, or a released one -
until the next frame's opening, one wrong frame per change (the wood
flashing where the game streams textures through one address, 2026-09-14).
The old descriptor slots are released once the submission completes
instead of leaking.
APPLY ONCE (requires patch_texpack_mips.py)."""
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

rel = "include/rex/graphics/d3d12/texture_cache.h"
s = read(rel)
assert "srv_generation_" not in s, "already applied"
s = rep(s, '''  struct TextureSRVKey {
    TextureKey key;
    uint32_t host_swizzle;
    uint8_t swizzled_signs;
  };
''', '''  struct TextureSRVKey {
    TextureKey key;
    uint32_t host_swizzle;
    uint8_t swizzled_signs;
    // [texpack] Which texture object and which set of its descriptors: a
    // pack resource change or a recreated object changes it, so the shader's
    // descriptor indices are rewritten in the same draw (see SRVGenerationOf).
    uint32_t generation;
  };
''')
s = rep(s, "    void ClearSRVDescriptors() { srv_descriptors_.clear(); }\n",
        '''    // Retires the descriptors (released once the current submission has
    // completed) and bumps the generation, so the next draw rebinds.
    void ClearSRVDescriptors();
    uint32_t srv_generation() const { return srv_generation_; }
''')
s = rep(s, "    double texpack_verified_at_ = 0.0;\n",
        "    double texpack_verified_at_ = 0.0;\n    uint32_t srv_generation_ = 0;\n")
s = rep(s, "  bool texpack_mip_init_tried_ = false;\n",
        '''  bool texpack_mip_init_tried_ = false;
  // [texpack] Descriptor slots a texture stopped using, with the submission
  // that may still read them; released once it has completed.
  std::vector<std::pair<uint64_t, uint32_t>> texpack_retired_descriptors_;
  void ReleaseRetiredDescriptors(uint64_t completed_submission);
  static uint32_t SRVGenerationOf(const TextureBinding* binding);
''')
write(rel, s)

rel = "src/graphics/d3d12/texture_cache.cpp"
s = read(rel)
# the key comparison and writing
s = rep(s, '''    if (key.key != binding->key || key.host_swizzle != binding->host_swizzle ||
        key.swizzled_signs != binding->swizzled_signs) {
      return false;
    }
''', '''    if (key.key != binding->key || key.host_swizzle != binding->host_swizzle ||
        key.swizzled_signs != binding->swizzled_signs ||
        key.generation != SRVGenerationOf(binding)) {
      return false;
    }
''')
s = rep(s, "      key.swizzled_signs = kSwizzledSignsUnsigned;\n",
        "      key.swizzled_signs = kSwizzledSignsUnsigned;\n      key.generation = 0;\n")
s = rep(s, "    key.swizzled_signs = binding->swizzled_signs;\n",
        "    key.swizzled_signs = binding->swizzled_signs;\n    key.generation = SRVGenerationOf(binding);\n")
# the generation, the deferred release, the clear
s = rep(s, "bool D3D12TextureCache::IsDecompressionNeeded(", '''uint32_t D3D12TextureCache::SRVGenerationOf(const TextureBinding* binding) {
  if (!binding || !binding->texture) return 0;
  const D3D12Texture* t = static_cast<const D3D12Texture*>(binding->texture);
  return uint32_t(reinterpret_cast<uintptr_t>(t) >> 4) * 2654435761u + t->srv_generation() * 40503u +
         1u;
}

void D3D12TextureCache::ReleaseRetiredDescriptors(uint64_t completed_submission) {
  for (auto it = texpack_retired_descriptors_.begin(); it != texpack_retired_descriptors_.end();) {
    if (it->first <= completed_submission) {
      ReleaseTextureDescriptor(it->second);
      it = texpack_retired_descriptors_.erase(it);
    } else {
      ++it;
    }
  }
}

void D3D12TextureCache::D3D12Texture::ClearSRVDescriptors() {
  auto& cache = static_cast<D3D12TextureCache&>(texture_cache());
  const uint64_t submission = cache.command_processor_.GetCurrentSubmission();
  for (const auto& p : srv_descriptors_)
    cache.texpack_retired_descriptors_.emplace_back(submission, p.second);
  srv_descriptors_.clear();
  ++srv_generation_;
}

bool D3D12TextureCache::IsDecompressionNeeded(''')
# the drains (two sites, different indentation)
s = rep(s, '''  const uint64_t completed = command_processor_.GetCompletedSubmission();
  g_texpack_uploads.erase(
      std::remove_if(g_texpack_uploads.begin(), g_texpack_uploads.end(),
                     [completed](const auto& e) { return e.first <= completed; }),
      g_texpack_uploads.end());
''', '''  const uint64_t completed = command_processor_.GetCompletedSubmission();
  g_texpack_uploads.erase(
      std::remove_if(g_texpack_uploads.begin(), g_texpack_uploads.end(),
                     [completed](const auto& e) { return e.first <= completed; }),
      g_texpack_uploads.end());
  ReleaseRetiredDescriptors(completed);
''')
s = rep(s, '''    const uint64_t completed = command_processor_.GetCompletedSubmission();
    g_texpack_uploads.erase(
        std::remove_if(g_texpack_uploads.begin(), g_texpack_uploads.end(),
                       [completed](const auto& e) { return e.first <= completed; }),
        g_texpack_uploads.end());
''', '''    const uint64_t completed = command_processor_.GetCompletedSubmission();
    g_texpack_uploads.erase(
        std::remove_if(g_texpack_uploads.begin(), g_texpack_uploads.end(),
                       [completed](const auto& e) { return e.first <= completed; }),
        g_texpack_uploads.end());
    ReleaseRetiredDescriptors(completed);
''')
write(rel, s)
print("done")
