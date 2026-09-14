"""s62 part 2 - the texture pack and the texture dump keep away from memory
the GPU wrote.

A texture whose pages are valid and GPU-written is a resolve destination: its
bytes came from a render target, never from art, and they change every frame.
Hashing one for the pack meant hashing whatever the memory held at that
moment - with 'some' readback the copy lands later, so that was the PREVIOUS
occupant: the main menu's loading-spinner sheet was recorded under 8 texture
ids of the same shape, and in Oakfield the sky showed it (2026-09-13). The
lookup at creation, the resolve-at-load lookup and the dump all skip such
textures now, and say how many they skipped.

Requires patch_readback_selfcopy.py (SharedMemory::AnyPageGpuWritten).
APPLY ONCE.
"""
import os
R = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"
p = os.path.join(R, "src/graphics/d3d12/texture_cache.cpp")
s = open(p, encoding="utf-8").read()

def rep(old, new):
    global s
    n = s.count(old)
    assert n == 1, ("expected 1, found %d: %s" % (n, old[:120]))
    s = s.replace(old, new)

# resolve-at-load
rep('''  const uint32_t gsize = texture.GetGuestBaseSize();
  if (!guest || !gsize) return;
  const uint32_t hash = TexturePackContentHash(guest, gsize);
  if (texture.texpack_resource() && texture.texpack_content_hash() == hash) return;
''',
'''  const uint32_t gsize = texture.GetGuestBaseSize();
  if (!guest || !gsize) return;
  // A resolve destination: the bytes are a render target's, never art, and
  // they change every frame - hashing them served the previous occupant of
  // the memory (the menu spinner across the Oakfield sky, 2026-09-13).
  if (shared_memory().AnyPageGpuWritten(uint32_t(key.base_page) << 12, gsize)) {
    static std::atomic<uint32_t> gpu_written_skipped{0};
    const uint32_t n = ++gpu_written_skipped;
    if (n == 1 || n % 1000 == 0)
      REXLOG_INFO("[texpack] {} texture loads left alone: memory written by the GPU (render "
                  "targets, not art)", n);
    retire_current();
    return;
  }
  const uint32_t hash = TexturePackContentHash(guest, gsize);
  if (texture.texpack_resource() && texture.texpack_content_hash() == hash) return;
''')

# creation-time lookup (the non-resolve mode)
rep('''  } else if (const TexturePackFile* found = TexturePackLookup(
          key, texpack_guest, key.GetGuestLayout().base.level_data_extent_bytes, true)) {
''',
'''  } else if (const TexturePackFile* found =
                 shared_memory().AnyPageGpuWritten(
                     uint32_t(key.base_page) << 12,
                     key.GetGuestLayout().base.level_data_extent_bytes)
                     ? nullptr   // a render target's memory: never art
                     : TexturePackLookup(key, texpack_guest,
                                         key.GetGuestLayout().base.level_data_extent_bytes,
                                         true)) {
''')

# the dump
rep('''    if (src && tsize && tsize < (64u << 20)) {
      const uint32_t hash = TexturePackContentHash(src, tsize);
      const std::string dir = rex::cvar::Query<std::string>("texture_dump_path");
''',
'''    if (src && tsize && tsize < (64u << 20) &&
        !shared_memory().AnyPageGpuWritten(uint32_t(tk.base_page) << 12, tsize)) {
      const uint32_t hash = TexturePackContentHash(src, tsize);
      const std::string dir = rex::cvar::Query<std::string>("texture_dump_path");
''')

open(p, "w", encoding="utf-8", newline="").write(s)
print("patched texture_cache.cpp")
