"""s62 part 1b - the per-window [gpu] line reports how many readback copies
landed quietly (BeginSelfCopy engaged: no invalidation, no texture reload).
Requires patch_readback_selfcopy.py. APPLY ONCE."""
import os
P = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
s = open(P, encoding="utf-8").read()
old = '''    const uint32_t faults = g_guest_copy_faults.exchange(0);
    if (faults) {
      char b[96];
      std::snprintf(b, sizeof(b), "%sreadback copies into freed memory skipped %u",
                    line.empty() ? "" : ", ", faults);
      line += b;
    }
'''
new = '''    const uint32_t faults = g_guest_copy_faults.exchange(0);
    if (faults) {
      char b[96];
      std::snprintf(b, sizeof(b), "%sreadback copies into freed memory skipped %u",
                    line.empty() ? "" : ", ", faults);
      line += b;
    }
    const uint32_t quiet = g_guest_copy_quiet.exchange(0);
    if (quiet) {
      char b[96];
      std::snprintf(b, sizeof(b), "%sreadback copies landed quietly %u",
                    line.empty() ? "" : ", ", quiet);
      line += b;
    }
'''
assert s.count(old) == 1, s.count(old)
assert "landed quietly" not in s
open(P, "w", encoding="utf-8", newline="").write(s.replace(old, new))
print("patched: quiet copies reported")
