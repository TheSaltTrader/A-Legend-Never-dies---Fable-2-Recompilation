"""s75 diagnostics - the deferred command list counts what it replays into
the Direct3D 12 command list, per kind, and the [gpu] fence-waits line every
5 s gets "dcl N (k:count ...)" - total commands and the six most frequent
kinds (enum indices of DeferredCommandList::Command: 8 draw indexed, 15
vertex buffers, 19 barriers, 23 root constants, 25 root CBV, 27 root
descriptor table, 35 pipeline state, 41 draw tag) - plus "in C copies" after
the upload megabytes. The D3D12 runtime + driver were 20% of the command
thread in the market (2026-09-14); this says how many calls a frame that is.
APPLY ONCE (requires patch_gpu_perf1.py)."""
D = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\deferred_command_list.cpp"
Q = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"

d = open(D, encoding="utf-8").read()
assert "g_dcl_command_counts" not in d, "already applied"
old = "namespace rex::graphics::d3d12 {"
assert d.count(old) == 1
d = d.replace(old, '''// [perf] What the replay hands the Direct3D 12 command list, per kind
// (global scope: command_processor.cpp declares them extern outside the namespace).
std::atomic<uint32_t> g_dcl_command_counts[64];
std::atomic<uint32_t> g_dcl_command_total{0};

''' + old, 1)
old = '''    const CommandHeader& header = *reinterpret_cast<const CommandHeader*>(stream);
    stream += kCommandHeaderSizeElements;
    stream_remaining -= kCommandHeaderSizeElements;
'''
assert d.count(old) == 1
d = d.replace(old, old + '''    g_dcl_command_counts[size_t(header.command) & 63].fetch_add(1, std::memory_order_relaxed);
    g_dcl_command_total.fetch_add(1, std::memory_order_relaxed);
''')
if "#include <atomic>" not in d:
    d = d.replace("#include <", "#include <atomic>\n#include <", 1)
open(D, "w", encoding="utf-8", newline="").write(d)

q = open(Q, encoding="utf-8").read()
assert "g_dcl_command_total" not in q
# counters at file scope, beside the split counter
old = '''static std::atomic<uint64_t> g_upload_bytes_window{0};  // [perf] uploads per fence-line window'''
assert q.count(old) == 1
q = q.replace(old, old + '''
static std::atomic<uint32_t> g_upload_copies_window{0};  // [perf] upload copies per window
extern std::atomic<uint32_t> g_dcl_command_counts[64];
extern std::atomic<uint32_t> g_dcl_command_total;''')
old = '''void D3D12CommandProcessor::NoteSharedMemoryUpload(uint64_t bytes) {
  g_frame_upload_bytes += bytes;
  g_upload_bytes_window.fetch_add(bytes, std::memory_order_relaxed);
}
'''
assert q.count(old) == 1
q = q.replace(old, '''void D3D12CommandProcessor::NoteSharedMemoryUpload(uint64_t bytes) {
  g_frame_upload_bytes += bytes;
  g_upload_bytes_window.fetch_add(bytes, std::memory_order_relaxed);
  g_upload_copies_window.fetch_add(1, std::memory_order_relaxed);
}
''')
old = '''      std::snprintf(b, sizeof(b), "%suploads %llu MB", line.empty() ? "" : ", ",
                    (unsigned long long)upload_mb);
'''
assert q.count(old) == 1
q = q.replace(old, '''      std::snprintf(b, sizeof(b), "%suploads %llu MB in %u copies", line.empty() ? "" : ", ",
                    (unsigned long long)upload_mb, g_upload_copies_window.exchange(0));
''')
old = '''    const uint64_t upload_mb = g_upload_bytes_window.exchange(0) >> 20;
'''
assert q.count(old) == 1
q = q.replace(old, '''    {
      const uint32_t dcl_total = g_dcl_command_total.exchange(0);
      if (dcl_total) {
        std::pair<uint32_t, uint32_t> kinds[64];
        for (uint32_t k = 0; k < 64; ++k) kinds[k] = {g_dcl_command_counts[k].exchange(0), k};
        std::sort(kinds, kinds + 64, [](const auto& a, const auto& b) { return a.first > b.first; });
        char b[200];
        int n = std::snprintf(b, sizeof(b), "%sdcl %u (", line.empty() ? "" : ", ", dcl_total);
        for (int k = 0; k < 6 && kinds[k].first; ++k)
          n += std::snprintf(b + n, sizeof(b) - size_t(n), "%s%u:%u", k ? " " : "", kinds[k].second,
                             kinds[k].first);
        std::snprintf(b + n, sizeof(b) - size_t(n), ")");
        line += b;
      }
    }
''' + old)
open(Q, "w", encoding="utf-8", newline="").write(q)
print("patched: deferred command list counts + upload copies on the fence line")
