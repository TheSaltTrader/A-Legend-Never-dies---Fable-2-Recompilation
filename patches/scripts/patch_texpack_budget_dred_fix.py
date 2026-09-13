import os, sys
ROOT = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"

def edit(rel, pairs):
    p = os.path.join(ROOT, rel)
    d = open(p, "rb").read(); crlf = b"\r\n" in d
    t = d.decode("utf-8").replace("\r\n", "\n")
    for old, new in pairs:
        if t.count(old) != 1:
            sys.exit("%s: %d matches for %r" % (rel, t.count(old), old[:70]))
        t = t.replace(old, new)
    open(p, "wb").write((t.replace("\n", "\r\n") if crlf else t).encode("utf-8"))
    print("patched", rel)

# The define macros end without a semicolon: the source chains .lifecycle(...)
# and terminates there. The new defines must come after that chain and carry
# their own lifecycle + terminator.
edit("src/ui/d3d12/d3d12_provider.cpp", [
    ('REXCVAR_DEFINE_BOOL(d3d12_debug, false, "UI/D3D12", "Enable Direct3D 12 and DXGI debug layer")\n'
     'REXCVAR_DEFINE_BOOL(d3d12_dred, true, "UI/D3D12",\n'
     '                    "Record Direct3D 12 removed-device data (auto-breadcrumbs, page faults) "\n'
     '                    "so a GPU hang names the command that hung")\n'
     '    .lifecycle(rex::cvar::Lifecycle::kInitOnly);\n',
     'REXCVAR_DEFINE_BOOL(d3d12_debug, false, "UI/D3D12", "Enable Direct3D 12 and DXGI debug layer")\n'
     '    .lifecycle(rex::cvar::Lifecycle::kInitOnly);\n'
     'REXCVAR_DEFINE_BOOL(d3d12_dred, true, "UI/D3D12",\n'
     '                    "Record Direct3D 12 removed-device data (auto-breadcrumbs, page faults) "\n'
     '                    "so a GPU hang names the command that hung")\n'
     '    .lifecycle(rex::cvar::Lifecycle::kInitOnly);\n'),
])
edit("src/graphics/d3d12/texture_cache.cpp", [
    ('REXCVAR_DEFINE_INT32(texture_warm_done, 0, "GPU",\n'
     '                     "Files warmed so far in the current stage")\n'
     'REXCVAR_DEFINE_INT32(texture_pack_upload_budget_mb, 24, "GPU",\n'
     '                     "Pack texture bytes uploaded per frame before the rest wait for "\n'
     '                     "later frames (0 = no limit)")\n'
     '    .lifecycle(rex::cvar::Lifecycle::kHotReload);\n',
     'REXCVAR_DEFINE_INT32(texture_warm_done, 0, "GPU",\n'
     '                     "Files warmed so far in the current stage")\n'
     '    .lifecycle(rex::cvar::Lifecycle::kHotReload);\n'
     'REXCVAR_DEFINE_INT32(texture_pack_upload_budget_mb, 24, "GPU",\n'
     '                     "Pack texture bytes uploaded per frame before the rest wait for "\n'
     '                     "later frames (0 = no limit)")\n'
     '    .lifecycle(rex::cvar::Lifecycle::kHotReload);\n'),
])
print("done")
