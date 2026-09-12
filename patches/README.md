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
| `rexglue-texpack-created-once.patch` | the F9-while-loading crash fix (2026-09-11): a texture's pack decision is taken once at creation and stored on it (`D3D12Texture::SetTexpackReplacement`), the upload and the view read it from there, and the upload checks the resource fits before reading; plus the `guest_fps_x10` cvar the F8 counter shows. Exactly today's edits, reconstructed - the same files also carry the locked-copy pack-path reads and the `[swap]` pacing line from earlier in the day, which are not in it |
| `scripts/patch_*.py` | the exact old-text/new-text edits of every plugin change from 2026-09-11/12 (created-once decision, guest fps cvar, the upload-fit guard's correct bound, upload-once for pack textures, the re-upload diagnostic), runnable in order on a tree at the right state. Kept because the SDK tree is shared with the NG2 port, which edits the same files in the same hours - a diff of `texture_cache.cpp` is never one project's change, and the 0.0.13 patch file above predates the 0.0.14 edits |
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
