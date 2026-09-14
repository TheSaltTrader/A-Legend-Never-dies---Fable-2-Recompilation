"""s72 - the submission boundary after small deferred resolves becomes the
default: readback_resolve_submit_small_kb 0 -> 64. Measured on the user's
own session at Bowerlake (2026-09-14): 5 white impostor flashes a minute
with it off, none in 60 s with it on; the render-target re-bind alone did
not help, so the cure is the command-list boundary itself (a full GPU
barrier). Cost: about 2,000 extra submissions a second among many impostor
resolves (60 -> 56 fps at the lake), 3 a frame in the market (nothing).
The explicit UAV barrier variant stays opt-in (readback_resolve_uav_barrier)
for the next test. APPLY ONCE (requires patch_readback_split_experiments.py)."""
P = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
s = open(P, encoding="utf-8").read()
old = '''REXCVAR_DEFINE_INT32(readback_resolve_submit_small_kb, 0, "GPU/D3D12",
                     "Experiment: end the submission (no wait) after every deferred resolve of at most this many KB (0 = never)")'''
new = '''REXCVAR_DEFINE_INT32(readback_resolve_submit_small_kb, 64, "GPU/D3D12",
                     "End the submission (no wait) after every deferred resolve of at most this many KB (0 = never); "
                     "a command-list boundary is a full GPU barrier and stops the white impostor flashes")'''
assert s.count(old) == 1, "anchor (already applied?)"
open(P, "w", encoding="utf-8", newline="").write(s.replace(old, new))
print("patched: submit split default 64 KB")
