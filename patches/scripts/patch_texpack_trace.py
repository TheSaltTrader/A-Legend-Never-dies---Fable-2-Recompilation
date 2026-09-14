"""s63 - FABLE2_TEXPACK_TRACE=1 logs every resolve-at-load decision for the
texture pack: which id, which content hash, which file (or why none), and
when a texture's content hash changes between loads. For finding why a
texture shows another texture's picture. Capped at 4000 lines a session.
APPLY ONCE (requires patch_texpack_gpuwritten.py)."""
P = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\texture_cache.cpp"
s = open(P, encoding="utf-8").read()

def rep(old, new):
    global s
    assert s.count(old) == 1, ("expected 1, found %d: %s" % (s.count(old), old[:100]))
    s = s.replace(old, new)

rep('''void D3D12TextureCache::ApplyTexpackResolve(D3D12Texture& texture, const TextureKey& key) {
  const std::string dir = rex::cvar::Query<std::string>("texture_pack_path");
''', '''// FABLE2_TEXPACK_TRACE=1: one line per resolve-at-load decision, capped.
static bool TexpackTraceOn() {
  static const bool on = [] {
    const char* v = std::getenv("FABLE2_TEXPACK_TRACE");
    return v && *v && *v != '0';
  }();
  return on;
}
static void TexpackTrace(const char* what, uint64_t id, uint32_t hash, uint32_t w, uint32_t h,
                         uint64_t file_id, uint32_t prev_hash) {
  static std::atomic<uint32_t> lines{0};
  if (!TexpackTraceOn() || ++lines > 4000) return;
  REXLOG_INFO("[texpack-trace] {:016X} {}x{} hash {:08X} prev {:08X} -> {} {:016X}", id, w, h, hash,
              prev_hash, what, file_id);
}

void D3D12TextureCache::ApplyTexpackResolve(D3D12Texture& texture, const TextureKey& key) {
  const std::string dir = rex::cvar::Query<std::string>("texture_pack_path");
''')

rep('''  if (shared_memory().AnyPageGpuWritten(uint32_t(key.base_page) << 12, gsize)) {
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
''', '''  if (shared_memory().AnyPageGpuWritten(uint32_t(key.base_page) << 12, gsize)) {
    static std::atomic<uint32_t> gpu_written_skipped{0};
    const uint32_t n = ++gpu_written_skipped;
    if (n == 1 || n % 1000 == 0)
      REXLOG_INFO("[texpack] {} texture loads left alone: memory written by the GPU (render "
                  "targets, not art)", n);
    if (texture.texpack_resource())
      TexpackTrace("retire:gpu-written", TexturePackId(key), 0, key.GetWidth(), key.GetHeight(), 0,
                   texture.texpack_content_hash());
    retire_current();
    return;
  }
  const uint32_t hash = TexturePackContentHash(guest, gsize);
  if (texture.texpack_resource() && texture.texpack_content_hash() == hash) return;
  const uint32_t trace_prev = texture.texpack_resource() ? texture.texpack_content_hash() : 0;
''')

rep('''    if (file_id)
      g_texpack_file_of[id] = {file_id, hash};
  }
  if (!file_id) { retire_current(); return; }
''', '''    if (file_id)
      g_texpack_file_of[id] = {file_id, hash};
  }
  if (!file_id) {
    TexpackTrace(texture.texpack_resource() ? "retire:no-file" : "none", id, hash, key.GetWidth(),
                 key.GetHeight(), 0, trace_prev);
    retire_current();
    return;
  }
  TexpackTrace(trace_prev ? "replace:changed" : "replace", id, hash, key.GetWidth(), key.GetHeight(),
               file_id, trace_prev);
''')

open(P, "w", encoding="utf-8", newline="").write(s)
print("patched: texpack trace")
