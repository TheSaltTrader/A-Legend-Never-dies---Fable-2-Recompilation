"""s63b - the pack trace logs 'none' once per texture id and keeps 40,000
lines, so a run reaches the world before the cap (the first run spent its
4,000 lines on the menu and the loading screen). APPLY ONCE after
patch_texpack_trace.py."""
P = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\texture_cache.cpp"
s = open(P, encoding="utf-8").read()
old = '''  static std::atomic<uint32_t> lines{0};
  if (!TexpackTraceOn() || ++lines > 4000) return;
'''
new = '''  static std::atomic<uint32_t> lines{0};
  if (!TexpackTraceOn()) return;
  if (what[0] == 'n') {   // "none": once per id, or the loading screen fills the cap
    static std::mutex m;
    static std::set<uint64_t> said;
    std::lock_guard<std::mutex> lock(m);
    if (!said.insert(id).second) return;
  }
  if (++lines > 40000) return;
'''
assert s.count(old) == 1, s.count(old)
open(P, "w", encoding="utf-8", newline="").write(s.replace(old, new))
print("patched: trace cap 40000, none once per id")
