"""s72 experiment - readback_resolve_uav_barrier (bool, false): after every
resolve, explicit UAV barriers on the shared memory buffer and the EDRAM
buffer, submitted at once. A submission boundary after small resolves cures
the white impostor flashes (2026-09-14) and a boundary is, among other
things, a full UAV barrier; this tests whether the barrier alone is the cure
(it costs nothing), before shipping the boundary. Set through FABLE2_TUNE.
Also adds the public edram_buffer() accessor to the render target cache.
APPLY ONCE (requires patch_readback_split_experiments.py)."""
P = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
H = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\include\rex\graphics\d3d12\render_target_cache.h"
s = open(P, encoding="utf-8").read()
assert "readback_resolve_uav_barrier" not in s, "already applied"

def rep(old, new):
    global s
    assert s.count(old) == 1, ("expected 1, found %d: %s" % (s.count(old), old[:90]))
    s = s.replace(old, new)

rep('''static std::atomic<uint32_t> g_resolve_splits{0};''',
    '''static std::atomic<uint32_t> g_resolve_splits{0};
REXCVAR_DEFINE_BOOL(readback_resolve_uav_barrier, false, "GPU/D3D12",
                    "Experiment: explicit UAV barriers on the shared memory and EDRAM buffers after every resolve")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);''')

rep('''  if (!render_target_cache_->Resolve(*memory_, *shared_memory_, *texture_cache_, written_address,
                                     written_length)) {
    return false;
  }
''', '''  if (!render_target_cache_->Resolve(*memory_, *shared_memory_, *texture_cache_, written_address,
                                     written_length)) {
    return false;
  }
  if (REXCVAR_GET(readback_resolve_uav_barrier)) {
    // [experiment] Make the resolve's writes visible to everything recorded
    // after it, whatever state tracking believes.
    PushUAVBarrier(shared_memory_->GetBuffer());
    PushUAVBarrier(render_target_cache_->edram_buffer());
    if (texture_cache_->IsDrawResolutionScaled()) {
      if (ID3D12Resource* scaled = texture_cache_->GetCurrentScaledResolveBufferResource())
        PushUAVBarrier(scaled);
    }
    SubmitBarriers();
  }
''')
open(P, "w", encoding="utf-8", newline="").write(s)

h = open(H, encoding="utf-8").read()
if "ID3D12Resource* edram_buffer() const" not in h:
    old_h = "  void InvalidateCommandListRenderTargets() {"
    assert h.count(old_h) == 1
    h = h.replace(old_h, "  ID3D12Resource* edram_buffer() const { return edram_buffer_; }\n" + old_h)
    open(H, "w", encoding="utf-8", newline="").write(h)
print("patched: readback_resolve_uav_barrier (+ edram_buffer accessor)")
