"""s74 - two things from the market profile (2026-09-14, plugin command thread
at ~50 fps): (1) gpu_log_unknown_registers (bool, false): every register
write looked its register up only to feed a debug log that the info level
never printed - 2.2% of the thread in GetRegisterInfo plus part of
WriteRegister's own 5%; the lookup now runs only when the switch is on.
(2) "uploads N MB" on the [gpu] fence-waits line every 5 s: the volume the
game's CPU writes force into the GPU buffer (RequestRanges was 13% of the
thread; a hitch frame uploaded 21 MB). APPLY ONCE."""
P = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\command_processor.cpp"
Q = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"

s = open(P, encoding="utf-8").read()
assert "gpu_log_unknown_registers" not in s, "already applied"
old = '''REXCVAR_DEFINE_BOOL(vsync, true, "GPU", "Enable vertical sync");'''
assert s.count(old) == 1
s = s.replace(old, old + '''
REXCVAR_DEFINE_BOOL(gpu_log_unknown_registers, false, "GPU",
                    "Look every register write up and log unknown registers at debug level (costs a "
                    "lookup per write; off unless debugging)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);''')
old = '''  const_cast<volatile uint32_t&>(regs.values[index]) = value;
  if (!regs.GetRegisterInfo(index)) {
    REXGPU_DEBUG("GPU: Write to unknown register ({:04X} = {:08X})", index, value);
  }
'''
assert s.count(old) == 1
s = s.replace(old, '''  const_cast<volatile uint32_t&>(regs.values[index]) = value;
  // [perf] The lookup only fed a debug line; at ~350k writes a second it was
  // 2% of the command thread (2026-09-14 market profile).
  if (REXCVAR_GET(gpu_log_unknown_registers) && !regs.GetRegisterInfo(index)) {
    REXGPU_DEBUG("GPU: Write to unknown register ({:04X} = {:08X})", index, value);
  }
''')
open(P, "w", encoding="utf-8", newline="").write(s)

q = open(Q, encoding="utf-8").read()
assert "g_upload_bytes_window" not in q
old = '''void D3D12CommandProcessor::NoteSharedMemoryUpload(uint64_t bytes) {
  g_frame_upload_bytes += bytes;
}
'''
assert q.count(old) == 1
q = q.replace(old, '''static std::atomic<uint64_t> g_upload_bytes_window{0};  // [perf] per fence-line window
void D3D12CommandProcessor::NoteSharedMemoryUpload(uint64_t bytes) {
  g_frame_upload_bytes += bytes;
  g_upload_bytes_window.fetch_add(bytes, std::memory_order_relaxed);
}
''')
old = '''    const uint32_t splits = g_resolve_splits.exchange(0);
    if (splits) {
      char b[64];
      std::snprintf(b, sizeof(b), "%sresolve splits %u", line.empty() ? "" : ", ", splits);
      line += b;
    }
'''
assert q.count(old) == 1
q = q.replace(old, old + '''    const uint64_t upload_mb = g_upload_bytes_window.exchange(0) >> 20;
    if (upload_mb) {
      char b[64];
      std::snprintf(b, sizeof(b), "%suploads %llu MB", line.empty() ? "" : ", ",
                    (unsigned long long)upload_mb);
      line += b;
    }
''')
# the counter is defined after its first use? NoteSharedMemoryUpload sits after the fence line
# in the file, so declare the atomic near the other file-scope counters instead.
q = q.replace('''static std::atomic<uint64_t> g_upload_bytes_window{0};  // [perf] per fence-line window
void D3D12CommandProcessor::NoteSharedMemoryUpload''', '''void D3D12CommandProcessor::NoteSharedMemoryUpload''')
old = '''static std::atomic<uint32_t> g_resolve_splits{0};'''
assert q.count(old) == 1
q = q.replace(old, old + '''
static std::atomic<uint64_t> g_upload_bytes_window{0};  // [perf] uploads per fence-line window''')
open(Q, "w", encoding="utf-8", newline="").write(q)
print("patched: gpu_log_unknown_registers + uploads MB on the fence line")
