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

# --- DRED on its own switch, not chained to the debug layer -------------------
edit("src/ui/d3d12/d3d12_provider.cpp", [
    ('REXCVAR_DEFINE_BOOL(d3d12_debug, false, "UI/D3D12", "Enable Direct3D 12 and DXGI debug layer")\n',
     'REXCVAR_DEFINE_BOOL(d3d12_debug, false, "UI/D3D12", "Enable Direct3D 12 and DXGI debug layer")\n'
     'REXCVAR_DEFINE_BOOL(d3d12_dred, true, "UI/D3D12",\n'
     '                    "Record Direct3D 12 removed-device data (auto-breadcrumbs, page faults) "\n'
     '                    "so a GPU hang names the command that hung")\n'),
    ('    // Enable DRED (Device Removed Extended Data) for diagnosing GPU crashes.\n'
     '    {\n'
     '      Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred_settings;\n'
     '      if (SUCCEEDED(pfn_d3d12_get_debug_interface_(IID_PPV_ARGS(&dred_settings)))) {\n'
     '        dred_settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);\n'
     '        dred_settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);\n'
     '        REXLOG_INFO("DRED (Device Removed Extended Data) enabled");\n'
     '      } else {\n'
     '        REXLOG_WARN(\n'
     '            "Failed to enable DRED - device removal diagnostics will be "\n'
     '            "limited");\n'
     '      }\n'
     '    }\n'
     '  }\n'
     '  // Create the DXGI factory.\n',
     '  }\n'
     '  // Enable DRED (Device Removed Extended Data) for diagnosing GPU crashes.\n'
     '  // On its own switch, not chained to the debug layer: the layer is too slow\n'
     '  // to play under, and a device loss in play is exactly when the breadcrumbs\n'
     '  // are wanted - a Fable II session lost its device to DEVICE_HUNG with\n'
     '  // nothing recorded (2026-09-13). Breadcrumbs cost one write per\n'
     '  // command-list op; page-fault reporting costs nothing until a fault.\n'
     '  if (debug || REXCVAR_GET(d3d12_dred)) {\n'
     '    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred_settings;\n'
     '    if (SUCCEEDED(pfn_d3d12_get_debug_interface_(IID_PPV_ARGS(&dred_settings)))) {\n'
     '      dred_settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);\n'
     '      dred_settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);\n'
     '      REXLOG_INFO("DRED (Device Removed Extended Data) enabled");\n'
     '    } else {\n'
     '      REXLOG_WARN("Failed to enable DRED - device removal diagnostics will be limited");\n'
     '    }\n'
     '  }\n'
     '  // Create the DXGI factory.\n'),
])

# --- pack uploads on a per-frame budget ---------------------------------------
edit("src/graphics/d3d12/texture_cache.cpp", [
    # the switch
    ('                     "Files warmed so far in the current stage")\n',
     '                     "Files warmed so far in the current stage")\n'
     'REXCVAR_DEFINE_INT32(texture_pack_upload_budget_mb, 24, "GPU",\n'
     '                     "Pack texture bytes uploaded per frame before the rest wait for "\n'
     '                     "later frames (0 = no limit)")\n'),
    # the queue
    ('static std::vector<std::pair<uint64_t, Microsoft::WRL::ComPtr<ID3D12Resource>>>\n'
     '    g_texpack_uploads;\n',
     'static std::vector<std::pair<uint64_t, Microsoft::WRL::ComPtr<ID3D12Resource>>>\n'
     '    g_texpack_uploads;\n'
     '\n'
     '// [texpack] Uploads held back by the per-frame budget: textures whose\n'
     '// upscaled copy is in the pack but whose bytes would push this frame past\n'
     '// texture_pack_upload_budget_mb. The guest texture stays in use until\n'
     '// BeginSubmission drains them, a budget\'s worth per frame, so a region load\n'
     '// spreads its pack uploads over a few dozen frames instead of stacking them\n'
     '// into the frames the game happens to load textures in (Bowerstone Market:\n'
     '// ~1 GB of pack files in the first seconds, 2026-09-13). Entries are\n'
     '// D3D12Texture pointers kept as void* (the class is nested); a texture\n'
     '// removes itself in ~D3D12Texture. All on the GPU thread; the mutex is for\n'
     '// the destructor, which may run elsewhere.\n'
     'static std::vector<void*> g_texpack_pending;\n'
     'static std::mutex g_texpack_pending_mutex;\n'
     'static int64_t g_texpack_budget_left = 0;  // bytes still allowed this submission\n'
     'static bool g_texpack_draining = false;     // inside the BeginSubmission drain\n'),
    # the budget check, once the size is known
    ('  if (!w || !h) { retire_current(); return; }\n'
     '  const uint64_t completed = command_processor_.GetCompletedSubmission();\n'
     '  g_texpack_uploads.erase(\n',
     '  if (!w || !h) { retire_current(); return; }\n'
     '  // Per-frame upload budget (g_texpack_pending). The estimate is the pixel\n'
     '  // bytes; the placed footprint only adds row padding. Draining skips the\n'
     '  // check (the drain loop stops on its own when the budget is spent).\n'
     '  {\n'
     '    const int64_t budget_mb = REXCVAR_GET(texture_pack_upload_budget_mb);\n'
     '    const int64_t need = int64_t(w) * int64_t(h) * 4;\n'
     '    if (budget_mb > 0 && !g_texpack_draining && g_texpack_budget_left < need) {\n'
     '      retire_current();  // the guest data shows - right content, low res - until then\n'
     '      {\n'
     '        std::lock_guard<std::mutex> lock(g_texpack_pending_mutex);\n'
     '        void* const self = static_cast<void*>(&texture);\n'
     '        if (std::find(g_texpack_pending.begin(), g_texpack_pending.end(), self) ==\n'
     '            g_texpack_pending.end())\n'
     '          g_texpack_pending.push_back(self);\n'
     '      }\n'
     '      static std::atomic<uint32_t> deferred{0};\n'
     '      const uint32_t n = ++deferred;\n'
     '      if (n == 1 || n % 500 == 0)\n'
     '        REXLOG_INFO("[texpack] {} uploads held for a later frame by the {} MB/frame budget", n,\n'
     '                    budget_mb);\n'
     '      return;\n'
     '    }\n'
     '    g_texpack_budget_left -= need;\n'
     '  }\n'
     '  const uint64_t completed = command_processor_.GetCompletedSubmission();\n'
     '  g_texpack_uploads.erase(\n'),
    # the drain, at the start of every submission
    ('void D3D12TextureCache::BeginSubmission(uint64_t new_submission_index) {\n'
     '  TextureCache::BeginSubmission(new_submission_index);\n',
     'void D3D12TextureCache::BeginSubmission(uint64_t new_submission_index) {\n'
     '  TextureCache::BeginSubmission(new_submission_index);\n'
     '\n'
     '  // [texpack] A fresh upload budget, then the uploads held back earlier, as\n'
     '  // many as it covers. The command processor reset the deferred command list\n'
     '  // just before calling this, so the copies land in this submission.\n'
     '  g_texpack_budget_left = int64_t(REXCVAR_GET(texture_pack_upload_budget_mb)) << 20;\n'
     '  {\n'
     '    std::vector<void*> pending;\n'
     '    {\n'
     '      std::lock_guard<std::mutex> lock(g_texpack_pending_mutex);\n'
     '      pending.swap(g_texpack_pending);\n'
     '    }\n'
     '    size_t done = 0;\n'
     '    g_texpack_draining = true;\n'
     '    for (; done < pending.size() && g_texpack_budget_left > 0; ++done) {\n'
     '      auto* texture = static_cast<D3D12Texture*>(pending[done]);\n'
     '      ApplyTexpackResolve(*texture, texture->key());\n'
     '    }\n'
     '    g_texpack_draining = false;\n'
     '    if (done < pending.size()) {\n'
     '      // The leftovers keep their place; anything queued meanwhile goes behind.\n'
     '      std::lock_guard<std::mutex> lock(g_texpack_pending_mutex);\n'
     '      pending.erase(pending.begin(), pending.begin() + ptrdiff_t(done));\n'
     '      pending.insert(pending.end(), g_texpack_pending.begin(), g_texpack_pending.end());\n'
     '      g_texpack_pending.swap(pending);\n'
     '    }\n'
     '  }\n'),
    # a destroyed texture leaves the queue
    ('D3D12TextureCache::D3D12Texture::~D3D12Texture() {\n'
     '  auto& d3d12_texture_cache = static_cast<D3D12TextureCache&>(texture_cache());\n',
     'D3D12TextureCache::D3D12Texture::~D3D12Texture() {\n'
     '  {\n'
     '    // [texpack] An upload still waiting for its frame must not find a dead\n'
     '    // texture (g_texpack_pending holds raw pointers).\n'
     '    std::lock_guard<std::mutex> lock(g_texpack_pending_mutex);\n'
     '    g_texpack_pending.erase(std::remove(g_texpack_pending.begin(), g_texpack_pending.end(),\n'
     '                                        static_cast<void*>(this)),\n'
     '                            g_texpack_pending.end());\n'
     '  }\n'
     '  auto& d3d12_texture_cache = static_cast<D3D12TextureCache&>(texture_cache());\n'),
])
print("done")
