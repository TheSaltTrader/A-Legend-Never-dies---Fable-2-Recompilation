# Changes this project needs in the ReXGlue SDK source tree

The app links an **installed** SDK (`REXSDK`, default `..\RexBlue\win-amd64`),
but two of its features come from DLLs built out of the SDK **source** clone at
`..\rexglue-src`. That clone is a separate git repository, so nothing in it is
carried by this project's history — re-clone it and both features disappear
silently, because a stock DLL is a perfectly valid DLL that simply lacks the
thing. These files exist so that is recoverable.

## What is here

| file | what it is |
|---|---|
| `rexglue-fidelityfx-spatial-only.patch` | unlocks the committed FSR/CAS present effects (`src/ui/CMakeLists.txt`) |
| `rexglue-canary-gpu-ports.patch` | the running port of Canary's post-fork GPU work; see `docs/CANARY-PORTED.md` for which commits are in it |
| `rexglue-file-open-observer.patch` | `rex::kernel::xboxkrnl::SetFileOpenObserver` (`include/rex/kernel/xboxkrnl/io.h`): every guest NtCreateFile reported to the app, which is how per-region texture warming learns which region is loading (2026-09-11). The same tree also carries the NG2 port's runtime work - the stuck-wait watchdog (its log tag is `[watchdog]`, renamed from `[ng2]` here), the audio underrun credit, the ring-buffer epoch, `video_mode_explicit`, the content-hash texture pack - which the NG2 project's own notes track |
| `rexglue-texpack-content-addressed.patch` | the pack lookup matches on content hash + shape and ignores the texture's address (2026-09-12): Fable II streams the same texture through different addresses on every visit, so the id-keyed lookup made zero replacements from a 28,587-file pack. Index by hash, stage lists keyed by the served file, resolve-at-load and sizing on the same rule. Deployed pair: `rexgpu-xenos.dll` 6,546,944 B (23:22) with the unchanged `rexruntime.dll`; the previous pair is in `RexBlue\win-amd64\bin\dll_backup_20260912_contentaddr\` |
| `rexglue-texpack-created-once.patch` | the F9-while-loading crash fix (2026-09-11): a texture's pack decision is taken once at creation and stored on it (`D3D12Texture::SetTexpackReplacement`), the upload and the view read it from there, and the upload checks the resource fits before reading; plus the `guest_fps_x10` cvar the F8 counter shows. Exactly today's edits, reconstructed - the same files also carry the locked-copy pack-path reads and the `[swap]` pacing line from earlier in the day, which are not in it |
| `scripts/patch_*.py` | the exact old-text/new-text edits of every plugin change from 2026-09-11/12 (created-once decision, guest fps cvar, the upload-fit guard's correct bound, upload-once for pack textures, the re-upload diagnostic; `patch_waitpoll.py` = the wait-packet poll yields for its first 2 ms whatever vsync says; `patch_fencestats.py` = named fence-wait statistics, the `[gpu] fence waits in 5 s` line that found the memexport drains; `patch_dumptrigger.py` = switching dumping on drops every texture so the resident scene is written), runnable in order on a tree at the right state. Kept because the SDK tree is shared with the NG2 port, which edits the same files in the same hours - a diff of `texture_cache.cpp` is never one project's change, and the 0.0.13 patch file above predates the 0.0.14 edits |
| `scripts/patch_texpack_budget_dred.py` + `_fix.py` (run in that order) | 2026-09-13: pack uploads capped per frame (`texture_pack_upload_budget_mb`; the rest wait in `g_texpack_pending` and drain from `BeginSubmission`; ships 0 = off after the A/B measured 24 MB as a loss - `patch_texpack_budget_default_off.py` is that change) and DRED on its own switch (`d3d12_dred`, on) instead of only with the debug layer, so a lost device names the command. Touches `src/graphics/d3d12/texture_cache.cpp` and `src/ui/d3d12/d3d12_provider.cpp`; the provider edit is in the RUNTIME, so both DLLs of the pair were rebuilt and deployed together |
| `scripts/patch_dred_drawtags.py` | 2026-09-13: every draw is tagged with its shader hashes in the deferred command list (`DrawTagInfo`, not a GPU op); `Execute` counts the ops DRED's breadcrumbs count and keeps a ring of executed draws (`DrawRecord`); the device-loss handler prints the draws around the hung op. `include/rex/graphics/d3d12/deferred_command_list.h`, `src/graphics/d3d12/deferred_command_list.cpp`, `src/graphics/d3d12/command_processor.cpp` |
| `scripts/patch_texpack_dump_dedupe.py` | 2026-09-13: raw texture dumps keyed by (shape, content hash) instead of (address id, hash), the set seeded from the dump folder on first use, so a session never re-dumps content already on disk (an hour at 400% draw distance had written 67 GB). `src/graphics/d3d12/texture_cache.cpp` |
| `scripts/patch_readback_sync_budget.py` | 2026-09-13: `readback_resolve_sync_budget` - in the fast/some readback modes at most N (8) first-seen resolves per frame may drain the GPU before their copy; the rest are queued (`pending_resolve_readbacks_`) and copied to guest memory in BeginSubmission once their submission completed. Also caps a dump session at 4,000 raw files. `include/rex/graphics/d3d12/command_processor.h`, `src/graphics/d3d12/command_processor.cpp`, `src/graphics/d3d12/texture_cache.cpp` |
| `scripts/patch_readback_exact.py` | 2026-09-13: `readback_resolve = some` copies EVERY resolve to guest memory exactly - the first at an address synchronously (budgeted), later ones from BeginSubmission at a frame's opening submission once their own submission completed; a newer render into the same slot drops the older pending copy; readback buffers replaced or evicted while in flight are released after their submission (`readback_buffers_to_release_`, `DropPendingResolveReadbacks`). Fixes the flashing cullis-gate swirl. `include/rex/graphics/d3d12/command_processor.h`, `src/graphics/d3d12/command_processor.cpp` |
| `scripts/patch_runtime_provider.py` | 2026-09-13: RUNTIME - physical-memory data providers (read watches): `EnablePhysicalMemoryAccessCallbacks(..., enable_data_providers=true)` sets the pages no-access and flags them (`provide_on_access`); `PhysicalHeap::TriggerCallbacks` calls the registered providers first for any access (they may unlock/relock the global lock) then restores access (`RestoreProviderPages`); `Register/UnregisterPhysicalMemoryDataProvider`, `DisablePhysicalMemoryDataProviders`. Apply ONCE (its anchors survive a re-run and would duplicate). `include/rex/system/xmemory.h`, `src/system/xmemory.cpp` |
| `scripts/patch_plugin_provider.py` | 2026-09-13: PLUGIN - `CommandProcessor::CallInThreadSafe` (mutex-protected queue drained between passes, wakes the idle worker); the D3D12 command processor registers a data provider: a deferred resolve copy watches its range; on a CPU touch the worker submits, waits for that submission (`LandPendingResolveReadbacks`), lands the copy and releases the pages; landings and drops release too; `on-demand readback` in the 5 s fence line. `include/rex/graphics/command_processor.h`, `src/graphics/command_processor.cpp`, `include/rex/graphics/d3d12/command_processor.h`, `src/graphics/d3d12/command_processor.cpp` |
| `scripts/patch_hitch_census.py` + `patch_hitch_fix.py` + `patch_hitch_relative.py` (in that order) | 2026-09-13: the `[hitch]` line - a guest frame over 25 ms and over 1.8x the last window's median logs what it did (textures decoded + guest bytes, shared-memory upload bytes, pipeline-creation waits at submission end, synchronous resolve readbacks, draws); counters at file scope beside the swap reporter, fed by `NoteTextureLoad` / `NoteSharedMemoryUpload`. `include/rex/graphics/d3d12/command_processor.h`, `src/graphics/d3d12/command_processor.cpp`, `src/graphics/d3d12/shared_memory.cpp`, `src/graphics/d3d12/texture_cache.cpp` |
| `scripts/patch_readback_gate.py` | 2026-09-13: `readback_resolve_on_demand` (bool, false) gates the page watches of the on-demand readback (measured: 21 vs 58-60 fps in the market with them on); `readback_resolve_drain_small_kb` (int, 0) makes "some" wait for the GPU after any resolve of at most N KB - the experiment knob for the impostor flashes. `src/graphics/d3d12/command_processor.cpp` |
| `scripts/patch_readback_guard.py` + `_guard2.py` + `_guard3.py` + `_guard4.py` + `_guard5.py` (in that order) | 2026-09-13: every readback copy into guest memory goes through `D3D12CommandProcessor::CopyToGuestMemory`: under the global critical region the physical parent heap's page table (`Memory::physical_heap()`, a new runtime accessor) must show every page of the range allocated, else the copy is skipped and counted (`readback copies into freed memory skipped N`); the copy itself runs without the lock. Fixes the crash of a copy landing in memory the game freed during a stall. Earlier cuts, kept for the record: a structured exception handler never caught the fault; the 0xE0000000 view heap's table skipped nearly every copy; `VirtualQuery` over the guest views (thousands of small regions from the write watches) cost 22 fps per-copy and 4 fps per-chunk. `include/rex/system/xmemory.h`, `include/rex/graphics/d3d12/command_processor.h`, `src/graphics/d3d12/command_processor.cpp` |
| `scripts/patch_readback_protect.py` | 2026-09-13: a resolve whose CPU copy is still pending registers its byte range with the shared memory (`ProtectGpuRange` / `UnprotectGpuRange`); `D3D12SharedMemory::UploadRanges` copies a dirty page in pieces around protected bytes (`SplitAroundProtected`), so a CPU write beside a fresh render no longer uploads stale bytes over it. Fixes the white/purple impostor flashes. `include/rex/graphics/shared_memory.h`, `src/graphics/shared_memory.cpp`, `src/graphics/d3d12/shared_memory.cpp`, `src/graphics/d3d12/command_processor.cpp` |
| `scripts/patch_scaled_protect.py` | 2026-09-13: with draw resolution scaling, `TextureCache::ScaledResolveGlobalWatchCallback` keeps the "scaled resolved" marks of pages that hold a protected (pending-copy) resolve (`SharedMemory::IsGpuRangeProtected`), so a CPU write beside a fresh impostor no longer makes its texture load from the unscaled guest copy (the pool's magenta fill). `include/rex/graphics/shared_memory.h`, `src/graphics/shared_memory.cpp`, `src/graphics/pipeline/texture/cache.cpp` |
| `scripts/patch_plugin_scene_stats.py` | 2026-09-13: `gpu_frame_draws` / `gpu_frame_depth_draws` - the draw counts of the last presented guest frame, counted at the two draw sites in IssueDraw and published with REXCVAR_SET from IssueSwap before the presenter is handed the frame; the app's ultrawide presenter reads them to tell a frame that drew the 3D world from a 2D menu. `include/rex/graphics/d3d12/command_processor.h`, `src/graphics/d3d12/command_processor.cpp` |
| `build_vulkan.cmd` | the build script itself (untracked upstream, so not in the patch) |

Apply the FidelityFX patch first; they touch different files and do not
conflict, but the float16 one is the larger of the two and easier to review on
its own.

## What they buy

* **Vulkan.** `REXGLUE_USE_VULKAN` defaults `OFF` on Windows, so the stock
  `rexgpu-xenos.dll` contains no Vulkan support at all. Without this the
  "Graphics engine: Vulkan" setting cannot load and the app falls back to
  DirectX 12.
* **FSR and CAS upscaling.** The stock runtime advertises
  `present_effect="bilinear"` and nothing else, so the "Upscaling" setting has
  exactly one value. The FSR/CAS shaders are already compiled and committed in
  the SDK tree; only a build define stands between them and being usable. See
  the patch's own comment, and `bugreport/REXGLUE-BUG-fable2-blue-scene.md`.

## Reapplying after a fresh clone

```
cd ..\rexglue-src
git apply ..\fable2recomp\patches\rexglue-fidelityfx-spatial-only.patch
copy ..\fable2recomp\patches\build_vulkan.cmd .
build_vulkan.cmd
```

Then, **after** building the app (`tools\build.cmd`), stage the results:

```
python tools\stage_sdk.py
```

Order matters. `rexglue_setup_target` re-copies the installed SDK's DLLs on
every app build, so staging first accomplishes nothing. `stage_sdk.py --check`
reports whether what is next to `fable2.exe` is ours or stock, by reading
marker strings out of the binaries rather than trusting that a copy ran.
| `patch_readback_selfcopy.py` | s62: `SharedMemory::BeginSelfCopy/EndSelfCopy/AnyPageGpuWritten`; the plugin's own readback copy lands without invalidating the pages or firing the texture watches | apply once |
| `patch_readback_quietlog.py` | s62: the `[gpu]` window line reports `readback copies landed quietly N` | apply once |
| `patch_texpack_gpuwritten.py` | s62: the pack lookup (creation and resolve-at-load) and the dump skip textures whose pages the GPU wrote; logs `N texture loads left alone` | apply once |
