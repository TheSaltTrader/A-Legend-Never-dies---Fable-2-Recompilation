"""Runtime diagnostics: d3d12_gpu_based_validation (bool, false) turns on
Direct3D 12 GPU-based validation (and synchronized command queue validation)
when the debug layer is on. The plain debug layer validates API use only; GBV
checks resource states and missing UAV barriers on the GPU timeline - the
class of hazard behind the white impostor flashes (2026-09-14). Very slow;
a diagnostic run only, through FABLE2_TUNE=d3d12_debug=true;d3d12_gpu_based_
validation=true. APPLY ONCE."""
P = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\ui\d3d12\d3d12_provider.cpp"
s = open(P, encoding="utf-8").read()
assert "d3d12_gpu_based_validation" not in s, "already applied"

def rep(old, new):
    global s
    assert s.count(old) == 1, ("expected 1, found %d: %s" % (s.count(old), old[:90]))
    s = s.replace(old, new)

rep('''    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(d3d12_dred, true, "UI/D3D12",''', '''    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(d3d12_gpu_based_validation, false, "UI/D3D12",
                    "With d3d12_debug: GPU-based validation (resource states, UAV hazards on the GPU timeline); very slow")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(d3d12_dred, true, "UI/D3D12",''')

rep('''    if (SUCCEEDED(pfn_d3d12_get_debug_interface_(IID_PPV_ARGS(&debug_interface)))) {
      debug_interface->EnableDebugLayer();
      debug_interface->Release();
''', '''    if (SUCCEEDED(pfn_d3d12_get_debug_interface_(IID_PPV_ARGS(&debug_interface)))) {
      debug_interface->EnableDebugLayer();
      if (REXCVAR_GET(d3d12_gpu_based_validation)) {
        ID3D12Debug1* debug1 = nullptr;
        if (SUCCEEDED(debug_interface->QueryInterface(IID_PPV_ARGS(&debug1)))) {
          debug1->SetEnableGPUBasedValidation(TRUE);
          debug1->SetEnableSynchronizedCommandQueueValidation(TRUE);
          debug1->Release();
          REXLOG_INFO("Direct3D 12 GPU-based validation enabled (diagnostic; very slow)");
        } else {
          REXLOG_WARN("Direct3D 12 GPU-based validation unavailable (no ID3D12Debug1)");
        }
      }
      debug_interface->Release();
''')
open(P, "w", encoding="utf-8", newline="").write(s)
print("patched: d3d12_gpu_based_validation")
