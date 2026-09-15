# s96: at draw resolution scale > 1 the resolve only writes the SCALED buffer; the
# unscaled shared memory buffer keeps whatever the CPU last uploaded there (the
# impostor pool's fill). A texture object created with scaled_resolve=0 (or whose
# scaled page bits were cleared by a CPU write beside the render) loads from the
# unscaled buffer - and UploadRanges deliberately skips the pages of a pending
# readback (SplitAroundProtected), so those pages show the stale fill: the
# white/magenta impostor flash. The 0.2.10 drain hid it by landing every readback
# synchronously (the CPU copy, uploaded on the next invalidation, held the render).
# Now the 1x downscale the readback path already computes is ALSO copied into the
# unscaled shared memory buffer on the GPU (one buffer copy, no CPU, no wait, no
# boundary): an unscaled load of a resolved range always shows the render.
# readback_resolve_mirror_unscaled (default on). Boundaries/splits default off.
import io

ROOT = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"
CP = ROOT + r"\src\graphics\d3d12\command_processor.cpp"
SMH = ROOT + r"\include\rex\graphics\d3d12\shared_memory.h"


def rd(p):
    return io.open(p, "r", encoding="utf-8", newline="").read()


def wr(p, s):
    io.open(p, "w", encoding="utf-8", newline="").write(s)


def rep(s, old, new):
    assert s.count(old) == 1, old[:80]
    return s.replace(old, new)


h = rd(SMH)
h = rep(h, "  void UseAsCopySource() { CommitUAVWritesAndTransitionBuffer(D3D12_RESOURCE_STATE_COPY_SOURCE); }\n",
        "  void UseAsCopySource() { CommitUAVWritesAndTransitionBuffer(D3D12_RESOURCE_STATE_COPY_SOURCE); }\n"
        "  // [mirror] The readback path copies the 1x downscale of a scaled resolve into\n"
        "  // the buffer, so an unscaled load of that range shows the render.\n"
        "  void UseAsCopyDestination() { CommitUAVWritesAndTransitionBuffer(D3D12_RESOURCE_STATE_COPY_DEST); }\n")
wr(SMH, h)

s = rd(CP)
s = rep(s, '''REXCVAR_DEFINE_BOOL(readback_resolve_split_before_load, true, "GPU/D3D12",''',
        '''REXCVAR_DEFINE_BOOL(readback_resolve_mirror_unscaled, true, "GPU/D3D12",
                    "At a draw resolution scale above 1, also copy the 1x downscale of every resolve "
                    "into the unscaled shared memory buffer on the GPU (the resolve itself only writes "
                    "the scaled buffer), so a texture loaded from the unscaled copy of a resolved range "
                    "shows the render and not the stale fill the CPU last uploaded there - the "
                    "white/magenta impostor flash. One GPU buffer copy per resolve, no wait (0.2.11)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(readback_resolve_split_before_load, false, "GPU/D3D12",''')
s = rep(s, "static std::atomic<uint32_t> g_splits_before_load{0};\n",
        "static std::atomic<uint32_t> g_splits_before_load{0};\n"
        "static std::atomic<uint32_t> g_mirror_copies{0};\n")
s = rep(s, '''    deferred_command_list_.D3DCopyBufferRegion(rb.buffers[write_index], 0,
                                               resolve_downscale_buffer_.Get(), 0, written_length);
    PushTransitionBarrier(resolve_downscale_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    texture_cache_->TransitionCurrentScaledResolveRange(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    SubmitBarriers();''', '''    deferred_command_list_.D3DCopyBufferRegion(rb.buffers[write_index], 0,
                                               resolve_downscale_buffer_.Get(), 0, written_length);
    if (REXCVAR_GET(readback_resolve_mirror_unscaled)) {
      // [mirror] The same 1x data into the unscaled shared memory buffer, where
      // a scaled resolve never writes: an unscaled load of this range (a texture
      // object created before the range was resolved, or after a CPU write beside
      // the render cleared its scaled page bits) then shows the render instead of
      // the fill the CPU last uploaded here - the white/magenta impostor flash.
      shared_memory_->UseAsCopyDestination();
      SubmitBarriers();
      deferred_command_list_.D3DCopyBufferRegion(shared_memory_->GetBuffer(), written_address,
                                                 resolve_downscale_buffer_.Get(), 0, written_length);
      g_mirror_copies.fetch_add(1, std::memory_order_relaxed);
    }
    PushTransitionBarrier(resolve_downscale_buffer_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    texture_cache_->TransitionCurrentScaledResolveRange(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    SubmitBarriers();''')
s = rep(s, '''        const uint32_t splits = g_splits_before_load.exchange(0);
        if (ln || sn) {
          char b[200];
          std::snprintf(b, sizeof(b),
                        "%slandings %u x %.1f ms (lock %.1f ms), submissions %u x %.1f ms (%u splits before a load)",
                        line.empty() ? "" : ", ", ln, lus / 1000.0, llk / 1000.0, sn, sus / 1000.0, splits);
          line += b;
        }''', '''        const uint32_t splits = g_splits_before_load.exchange(0);
        const uint32_t mirrors = g_mirror_copies.exchange(0);
        if (ln || sn) {
          char b[220];
          std::snprintf(b, sizeof(b),
                        "%slandings %u x %.1f ms (lock %.1f ms), submissions %u x %.1f ms (%u splits before a load), %u mirror copies",
                        line.empty() ? "" : ", ", ln, lus / 1000.0, llk / 1000.0, sn, sus / 1000.0, splits, mirrors);
          line += b;
        }''')
wr(CP, s)
print("patched OK")
