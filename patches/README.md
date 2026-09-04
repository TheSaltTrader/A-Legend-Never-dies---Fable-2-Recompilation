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
| `rexglue-fidelityfx-spatial-only.patch` | tracked change to `src/ui/CMakeLists.txt` |
| `build_vulkan.cmd` | the build script itself (untracked upstream, so not in the patch) |

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
