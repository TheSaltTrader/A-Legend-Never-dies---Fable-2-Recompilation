"""s64 - a replaced texture is re-verified as it is bound: eight 8-byte samples
of its guest memory, taken when the replacement was resolved, are compared
every half second; a change means the memory was rewritten without the
plugin seeing a reload, so the range is invalidated exactly as a CPU write
would do it - the pages are re-uploaded, the watch fires, the next use
reloads the guest resource and re-resolves the pack. Bounds any stale
picture to half a second, whatever the cause.
APPLY ONCE (requires patch_texpack_gpuwritten.py)."""
import os
R = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"

def read(rel):
    return open(os.path.join(R, rel), encoding="utf-8").read()

def write(rel, s):
    open(os.path.join(R, rel), "w", encoding="utf-8", newline="").write(s)
    print("patched", rel)

# 1. D3D12Texture: samples + verified time; the cache: the re-verify method
rel = "include/rex/graphics/d3d12/texture_cache.h"
s = read(rel)
assert "TexpackReverify" not in s, "already applied"
old = "    uint32_t texpack_content_hash() const { return texpack_content_hash_; }\n"
assert s.count(old) == 1
s = s.replace(old, old + '''    // Eight 8-byte samples of the guest bytes the replacement was resolved
    // from, spread across the base level, and when they were last checked.
    void SetTexpackSamples(const uint8_t* guest, uint32_t size, double now) {
      for (int i = 0; i < 8; ++i)
        texpack_samples_[i] = TexpackSampleAt(guest, size, i);
      texpack_verified_at_ = now;
    }
    bool TexpackSamplesMatch(const uint8_t* guest, uint32_t size) const {
      for (int i = 0; i < 8; ++i)
        if (texpack_samples_[i] != TexpackSampleAt(guest, size, i)) return false;
      return true;
    }
    double texpack_verified_at() const { return texpack_verified_at_; }
    void SetTexpackVerifiedAt(double now) { texpack_verified_at_ = now; }
    static uint64_t TexpackSampleAt(const uint8_t* guest, uint32_t size, int i) {
      if (size < 8) return 0;
      const uint64_t offset = (uint64_t(size - 8) * uint64_t(i)) / 7u;
      uint64_t v = 0;
      std::memcpy(&v, guest + offset, 8);
      return v;
    }
''')
old = "    uint32_t texpack_content_hash_ = 0;\n"
assert s.count(old) == 1
s = s.replace(old, old + "    uint64_t texpack_samples_[8] = {};\n    double texpack_verified_at_ = 0.0;\n")
old = "  void ApplyTexpackResolve(D3D12Texture& texture, const TextureKey& key);\n"
assert s.count(old) == 1
s = s.replace(old, old + "  // Once per half second per bound replaced texture: the samples against the\n"
                         "  // memory; a change invalidates the range as a CPU write would.\n"
                         "  void TexpackReverify(D3D12Texture& texture);\n")
if "#include <cstring>" not in s:
    s = s.replace("#include <string>", "#include <cstring>\n#include <string>", 1)
write(rel, s)

# 2. the cache: sample at resolve time; verify at bind time
rel = "src/graphics/d3d12/texture_cache.cpp"
s = read(rel)
old = "  texture.SetTexpackResource(res, hash);\n"
assert s.count(old) == 1
s = s.replace(old, old + "  texture.SetTexpackSamples(guest, gsize, TexpackNowSeconds());\n")
old = '''      // Will be referenced by the command list, so mark as used.
      binding_texture->MarkAsUsed();
'''
assert s.count(old) == 1
s = s.replace(old, '''      // Will be referenced by the command list, so mark as used.
      TexpackReverify(*binding_texture);
      binding_texture->MarkAsUsed();
''')
old = "void D3D12TextureCache::ApplyTexpackResolve(D3D12Texture& texture, const TextureKey& key) {\n"
assert s.count(old) == 1
s = s.replace(old, '''static double TexpackNowSeconds() {
  static const auto t0 = std::chrono::steady_clock::now();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

// The bytes under a replaced texture, re-checked as it is bound. The pack is
// resolved from the memory at load time; a texture whose memory is rewritten
// without the plugin seeing a reload (a write it did not catch, a GPU copy it
// could not see) kept the old picture for as long as it lived - the Oakfield
// ground and sky wearing the market's textures (2026-09-14). Eight samples
// every half second cost nothing measurable and bound that to half a second.
void D3D12TextureCache::TexpackReverify(D3D12Texture& texture) {
  if (!texture.texpack_resource()) return;
  const double now = TexpackNowSeconds();
  if (now - texture.texpack_verified_at() < 0.5) return;
  texture.SetTexpackVerifiedAt(now);
  const uint8_t* guest = shared_memory().memory().TranslatePhysical<const uint8_t*>(
      uint32_t(texture.key().base_page) << 12);
  const uint32_t size = texture.GetGuestBaseSize();
  if (!guest || !size) return;
  if (texture.TexpackSamplesMatch(guest, size)) return;
  static std::atomic<uint32_t> n{0};
  const uint32_t k = ++n;
  if (k <= 20 || k % 100 == 0)
    REXLOG_INFO("[texpack] re-verify: the memory under {:016X} ({}x{}) changed without a reload "
                "({} so far); reloading it", TexturePackId(texture.key()), texture.key().GetWidth(),
                texture.key().GetHeight(), k);
  // The path a CPU write takes: the pages are re-uploaded from guest memory
  // and the watch fires (base outdated, handle released) - nothing dangles.
  shared_memory().MemoryInvalidationCallback(uint32_t(texture.key().base_page) << 12, size, true);
}

void D3D12TextureCache::ApplyTexpackResolve(D3D12Texture& texture, const TextureKey& key) {
''')
write(rel, s)
print("done")
