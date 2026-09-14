"""s77 - the deferred-command-list counters (s75) cost frames: two atomic
increments per replayed command at ~2.2 million commands a second took the
market from 57 to 46 fps. They now run only while gpu_dcl_census (bool,
false) is on, checked once per replay; the fence line prints "dcl ..." only
then. APPLY ONCE (requires patch_dcl_counts.py)."""
D = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\deferred_command_list.cpp"
Q = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
d = open(D, encoding="utf-8").read()
assert "gpu_dcl_census" not in d, "already applied"
old = '''std::atomic<uint32_t> g_dcl_command_counts[64];
std::atomic<uint32_t> g_dcl_command_total{0};
'''
assert d.count(old) == 1
d = d.replace(old, old + '''REXCVAR_DEFINE_BOOL(gpu_dcl_census, false, "GPU/D3D12",
                    "Count the Direct3D 12 commands replayed per kind (costs frames; diagnostic)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
''')
old = '''  const uintmax_t* stream = command_stream_.data();
  size_t stream_remaining = command_stream_.size();
'''
assert d.count(old) == 1
d = d.replace(old, old + '''  const bool count_commands = REXCVAR_GET(gpu_dcl_census);
''')
old = '''    g_dcl_command_counts[size_t(header.command) & 63].fetch_add(1, std::memory_order_relaxed);
    g_dcl_command_total.fetch_add(1, std::memory_order_relaxed);
'''
assert d.count(old) == 1
d = d.replace(old, '''    if (count_commands) {
      g_dcl_command_counts[size_t(header.command) & 63].fetch_add(1, std::memory_order_relaxed);
      g_dcl_command_total.fetch_add(1, std::memory_order_relaxed);
    }
''')
if "#include <rex/cvar.h>" not in d:
    d = d.replace("#include <atomic>", "#include <atomic>\n#include <rex/cvar.h>", 1)
open(D, "w", encoding="utf-8", newline="").write(d)
print("patched: dcl counters gated by gpu_dcl_census")
