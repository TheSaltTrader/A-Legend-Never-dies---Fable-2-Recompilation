# Changelog

All notable changes to fable2recomp. Versions follow the project's own
numbering, not the game's.

## 0.0.7 — 2026-09-04

### The blue-scene bug: diagnosed, not fixed

The 3D scene turns flat blue a few minutes into play and never recovers, while
the UI keeps drawing correctly on top of it. This release does not fix it, but
it establishes what it is — and, importantly, what it is not.

- **Not a hang.** The presence heartbeat keeps ticking, ~510 APCs/second keep
  completing, the GPU keeps compiling pipelines, and there are no fatals, no
  unregistered functions and no failed asset loads. What stops is GPU *output*.
- **Not configuration.** Every GPU/kernel cvar where Xenia Canary differs from
  our defaults was aligned to Canary's values and tested. Still blue.
- **Not the community patches.** It predates them; they change its extent only.
- **Not backend-specific.** The plugin was rebuilt from source with Vulkan
  enabled and the same reproduction run on it: **the blue is identical, and
  character meshes stop drawing entirely as well**. Both backends failing the
  same way puts the defect in the **shared** GPU code.
- **Not the game.** Xenia Canary plays the same disc, same patches, straight
  through the point where ours fails (confirmed by playing it).

Our shared render-target cache is 1,388 lines against Canary's 1,757, and the
plugin is an older Canary fork — 19 Canary-only cvars present, plus 5 master-only
names Canary has since renamed — missing 37 Canary GPU flags.

### Added

- **Graphics engine picker** (Graphics → Graphics engine; `gpu_backend`,
  `vulkan`/`d3d12`). Defaults to **DirectX 12**, because Vulkan measured worse.
  Selecting Vulkan with the SDK's stock plugin logs the miss and falls back
  rather than failing to start, since that plugin is D3D12-only.
- `rexglue-src/build_vulkan.cmd` — builds the SDK from source with
  `-DREXGLUE_USE_VULKAN=ON`. No Vulkan SDK needed; Vulkan-Headers and glslang
  are vendored. Must use the project's own `win-amd64` preset: rolling the
  cmake line by hand drops `-march=x86-64-v2` and the SSSE3 intrinsics in
  `core/memory.cpp` fail to compile.
- `tools/config_diff.py` — diffs Canary's settings against our live cvar dump,
  for names both actually have.
- `tools/plugin_lineage.py` — fingerprints which Xenia branch the plugin came
  from, and lists what it lacks.
- `tools/blue_sweep.py` — reproduces the bug and sweeps settings against it.
  Reports **INCONCLUSIVE** rather than "clean" when a run never reaches the
  failing state.
- `tools/health.py`, `tools/clear_cache.py`, `tools/freeze_stacks.py`,
  `tools/ab_run.py`; `--exe`/`--exe-args` on `play_probe.py` so the same
  schedule and measurement can drive Xenia Canary for comparison.
- `bugreport/REXGLUE-BUG-fable2-blue-scene.md`.

### Notes

- **A reliable reproduction was the hard part.** Scripted input kept sticking on
  character select — those cards need a stick deflection that a stream of A
  presses never supplies — and three sweeps reported "clean" while sitting
  there. Loading a save via **Continue** skips it and lands directly in the
  world where the bug lives.
- **The v0.0.6 ROV claim was wrong and is retracted.** Canary plays this fine on
  RTV, and that measurement came from a run that never reached the failing
  state. Corrected in `Fable2Tuning::Fixed()`.

## 0.0.6 — 2026-09-04

### Fixed

- **The ~3m25s freeze.** It was the **render-target path**: the runtime
  defaulted to RTV (the shader cache file is named `4D5307F1.rtv.d3d12.xpso`),
  and `render_target_path_d3d12 = "rov"` is now a measured entry in
  `Fable2Tuning::Fixed()`.

  Two runs, identical 290 s input schedule, shader cache cleared before each,
  one variable changed:

  | path | last picture change | changed | identical |
  |---|---|---|---|
  | `rov` | 289s of 290s | 47 | 1 |
  | `rtv` | 253s of 290s | 38 | 10 |

  Confirmed with ROV as the default over a longer run: **473s of 480s, 59
  changed, 0 identical** — eight minutes clean, against a failure that used to
  arrive at three and a half.

### Added

- **`tools/clear_cache.py`** and a **Clear shader cache** button on the setup
  screen. Stale cached shaders are a known cause of Fable II's black-texture
  bug lingering, and a cache built by an older build of this project is exactly
  that hazard. Both are deliberately narrow: they enumerate what they will
  remove, refuse anything outside `cache/`, and never touch the save games,
  installed DLC or profile that share the same root.
- The `OnPostSetup` readback now covers the render-target path, readback mode
  and the fetch-constant flag, so all three are confirmed rather than assumed:
  `render target path 'rov', readback 'some', allow_invalid_fetch_constants true`.

### Notes

- Two earlier attempts at the freeze measurement were wrong and are recorded as
  such: counting `[gpu]` log lines (they only exist at `debug`, where ~510 APC
  lines a second rotate the transition out of the log), and running the A/B
  without driving input (so neither arm ever reached the state that freezes —
  both "passed" and proved nothing).
- Scripted input schedules are stateful: once save games exist the main menu
  grows a "Continue" row, and a schedule tuned before that no longer lands on
  the same entries.

## 0.0.5 — 2026-09-04

### Added

- **Community game patches** from Xenia Canary's patch file for 4D5307F1
  (Margen67, Guy), as midasm hooks rather than memory patches — the immediates
  are already C++ constants by the time anything could patch memory. All off by
  default, all in the settings menu: **60 fps**, **1280 wide** (the game
  renders 1120 and upscales), **disable MSAA**, **30 Hz tick rate** (from 15,
  for in-game UI smoothness and input delay) and **disable texture morphing**.
- **`readback_resolve`** as a Graphics setting (`none`/`fast`/`some`/`full`).
  This is the real fix for Fable II's black-texture bug — the hero's and the
  dog's textures turning black once the hero grows up — and `some` is the
  selective readback the unofficial Xenia fork for this game hand-builds.
- **`gpu_allow_invalid_fetch_constants = true`** in `Fable2Tuning::Fixed()`,
  which is now non-empty for the first time. The community reports this title
  emits fetch constants the strict path rejects, and that leaving it off drops
  textures (missing grass). It has a citation, which is the bar for that list.
- `tools/freeze_stacks.cmd` — breaks in *during* the hang and dumps every
  thread's stack. The freeze is not a fault, so there is no exception for a
  debugger to catch and the crash-time approach does not apply.
- `tools/ab_run.py`, and `--freeze-report` / `--extra` on `play_probe.py`.

### Verified

- Every patch address was checked against our own image before use, and all
  four active hooks then confirmed themselves against the live values at
  runtime: `frame divider 2 -> 1`, `MSAA samples 2 -> 1`,
  `render width 1120 -> 1280`, `tick rate 15 Hz -> 30 Hz (guest 0x83319510)`.
- The patch file demonstrably matches this disc: the tick-rate patch presets a
  `.data` double, and our image holds exactly **15.0** there, which the patch
  makes exactly **30.0** — matching its description to the bit. The store it
  NOPs targets precisely that address.

### Not shipped

- **Unlock Collectors Edition Content** does not apply to this build: its value
  `li r9, 1` lands immediately before `mtctr r9; bctrl`, so it would make the
  game call address 1. The real CE package installs properly instead.
- **Unlock Website Items** is coherent here but is a content unlock, not a fix.

### Diagnosis of the freeze (not yet fixed)

Much sharper than "rendering stops":

- The guest is **not** deadlocked. `XGIUserSetContextEx` still ticks once a
  second — the presence heartbeat — and ~510 APCs/second keep completing,
  perfectly steady, indefinitely.
- What stops is **GPU submission**: after about 3m25s there is zero `[gpu]`
  activity, permanently. No fatals, no ring-buffer failure, and the only failed
  file opens are language packs we do not ship and a title update that does not
  exist.
- So nothing is crashing or unregistered — the game is alive and running its
  loop, and something makes it stop submitting work.

### Notes

- A first A/B on the render-target path was **invalid and is recorded as such**:
  neither run was driven with input, so neither ever left the title screen and
  reached the state that freezes. Both "passed" 240 s, which proved nothing.
  `play_probe.py` now drives input *and* measures the freeze directly, by
  comparing raw frame buffers — a frozen picture repeats byte for byte.
- An earlier version of that measurement counted `[gpu]` log lines, which was
  also worthless: those log at `debug`, and at debug the ~510 APC lines a
  second rotate the transition out of the log entirely.

## 0.0.4 — 2026-09-04

### Added

- **A settings menu, in two surfaces**, ported from `ng2recomp`:
  a **pre-boot setup screen** (game folder, install from an ISO, all display
  and graphics settings) on `OnFinalizePaths`, and an **F10 overlay** over the
  running game. F4, the SDK's own cvar browser, is left alone and linked to.
- `src/fable2_settings.h` (the saved file), `src/fable2_tuning.h` (the
  deferred-cvar transport), `src/fable2_menu.{h,cpp}`, `src/fable2_disc.{h,cpp}`
  (ISO inspection and extraction), `src/fable2_platform.{h,cpp}` (Win32 file
  pickers, Shift detection).
- **`FABLE2_DUMP_CVARS=<path>`** writes all 153 registered cvars with values,
  defaults, allowed values and ranges. The menu was designed from that dump
  rather than from the SDK headers.
- Audio rows (mute, buffering) and mouse-look rows, which the dump showed the
  runtime supports and ng2recomp's menu does not expose.
- `FABLE2_*` environment overrides, including `FABLE2_NO_SETUP=1` so a scripted
  run never stops at a dialog.

### Verified

- The setup screen appears, is responsive, and correctly refuses to start with
  a bad game path (Play disabled, reason on screen).
- A configured launch boots straight to the title screen, and the `OnPostSetup`
  readback confirms the deferred config reached the plugin:
  `GPU: internal scale 2x2, swap_post_effect 'fxaa', vsync true`. Supersampling
  is the setting ng2recomp initially wrote off as impossible.
- The mouse rows grey out until keyboard control is on.

### Notes

- **A borrowed warning, measured and dropped.** ng2recomp warns that its game
  paces logic off the reported refresh (above 60 Hz it speeds up; V-Sync off
  speeds it up). Measured here with the refresh change confirmed in the log: a
  30 Hz and a 60 Hz run reach the same point in the boot sequence at the same
  wall-clock second, so Fable II does not appear to do this. The menu says what
  was measured and marks above-60 untested, instead of repeating NG2's claim.
- `Fable2Tuning::Fixed()` is deliberately empty. ng2recomp ships several Xenia
  compatibility flags there; none of them are about this game.
- No DLC page, since the expansions are on the disc. The About section says so
  and points at `--dlc_root` for anything that genuinely is not.

## 0.0.3 — 2026-09-04

### Verified

- **Both Fable II expansions are already on the GOTY disc**, so the standalone
  packages for them are redundant. `data/levels.bnk` holds their level data
  (`Worlds\Albion\DLC2\*` for *See the Future*,
  `Worlds\Albion\MysteryIsland` for *Knothole Island*), `scenarios.list`
  registers the four DLC2 levels, `fasttravellist.txt` has the Knothole Island
  entry, and the executable itself contains `KnotholeIslandSeasonManager`, the
  `QD010_KnotholeIsland` quest chain and the expansion achievement text.
  Of the three packages supplied, only the 12 KB *Collectors' Edition Content*
  (a single `.txt` token) is not on the disc.
- All three packages are licensee `FFFFFFFFFFFFFFFF` — unrestricted, not bound
  to a console or profile — so there is no entitlement to fake. `license_mask`
  is deliberately left alone.

### Added

- **`tools/stfs_info.py`** — identifies any CON/LIVE/PIRS package from its
  header: title, display name, content type, size and the licence table.
- **DLC installation** (`src/fable2_dlc.{h,cpp}`, `--dlc_root`,
  `tools/install_dlc.cmd`) via the SDK's `ContentManager::InstallContent`.
  Deliberately not wired into `run.cmd`, so nothing extracts a gigabyte of
  redundant expansion by accident.

### Fixed

- The duplicate-package guard was ordered wrong: the already-installed check
  returned *before* the display name was recorded, so a second copy of an
  already-installed package skipped the duplicate check and installed anyway —
  which for an expansion means extracting 557 MB twice. Found by putting two
  copies in a folder and looking at what happened, then fixed and re-verified.
- `tools/stfs_info.py`'s content-type table had several labels wrong; it now
  matches the SDK's own `XContentType` enum. (The one that mattered,
  `0x2 = MarketplaceContent`, was already right.)

### Notes

- Each guard in the installer was verified by making it fire: a package with a
  patched title id (`DEADBEEF`) is refused, a second launch skips what is
  already installed, and two copies of one package install once.

## 0.0.2 — 2026-09-04

### Added

- **`tools/scan_fnptrs.py`** (was `scan_vtables.py`) gained a **second
  discovery channel**: function pointers **materialised in code** with
  `lis`/`addi`, which never appear as data anywhere in the image. The first
  address it found this way, `0x822142D0`, is a three-instruction getter whose
  value occurs nowhere in the XEX at any alignment — channel 1 could not
  possibly see it — and it was the crash that stopped the game at character
  select.
- **`tools/bisect_fnptrs.py`** — bisects a quarantined set of registrations by
  rebuilding and relaunching, because reading fifty candidate splits to guess
  which one broke the game is not a method.
- `tools/play_probe.py`: explicit controller bindings, a `--hold` duration, and
  a retry around Windows Graphics Capture's `start()`.

### Fixed

- **`b $+4` is not a terminator.** `is_terminator()` treated every
  unconditional `b` as "control cannot fall through", but MSVC emits `b $+4` as
  a no-op and it plainly does fall through. Registering the instruction after
  one split a function in half; the orphaned half read `0x54` off `r31`, the
  frame pointer its parent's prologue had set up, and under
  `non_volatile_as_local` it got its own zeroed `r31` — producing
  *"Unhandled guest access violation: read of guest 0x00000054"*, the fault
  address verbatim. Found by bisecting 51 registrations over eight rebuilds to
  `0x82FFD258`. With the one-line fix the entire channel-2 set goes back in.
- `0x822142D0` and `0x82988ED8` registered — both were channel-2 finds the
  runtime then independently demanded, which is the strongest evidence the
  channel is real.
- `tools/play_probe.py` no longer loses a whole run when the window exists but
  Windows Graphics Capture will not yet accept it
  (*"Failed to convert item to `GraphicsCaptureItem`"*). It retries.

### Verified

- **The game reaches gameplay.** Boots → title → main menu → New Game character
  select → the opening cinematic in-engine → **Old Bowerstone**, with snow,
  brazier fire, particles and the tutorial hint up. A 300-second run logs
  **zero fatals** and 9,314 lines.
- 168 → 166 function-boundary overrides (15 hand-found, 151 generated,
  19 excluded).

### Known issues

- **Rendering freezes ~3.5 minutes in.** The presented frame goes static and
  blue and never changes, while the guest keeps running and the GPU keeps
  compiling pipelines. No ring-buffer failure is logged, unlike NG2's
  superficially similar symptom. This is the top blocker.
- The **3,856 `BaseHeap::AllocFixed ... already reserved range` errors** are
  **not** blocking and have been demoted. They are one burst at boot, from
  Lionhead's own `CDynamicMemoryPageAllocator<CPhysicalAlloc>` /
  `<CVirtualAlloc>` (the names are in `.rdata`), and the game then loads
  6.5 GB of assets, renders 3D and saves a game on top of them. Upstream
  replaced the guest's physical allocator with dlmalloc, but it was working
  from a far earlier failure point; there is no evidence here that this is
  worth the invasiveness.

### Notes

- **Character select is driven by the left stick, not the d-pad.** Found by
  pressing ten candidate keys on a schedule and measuring the frame-to-frame
  delta in the card region: `d` (lstick right) scored 34 against ~11 for
  everything else, and the picture went static afterwards because the enlarged
  card covers the animated background. Guessing had already cost a 220-second
  probe that sat on an unhighlighted card, and a false conclusion that `right`
  worked when in fact `tab`, two presses earlier, had done it.
- `play_probe.py` now pins every binding explicitly rather than relying on the
  SDK defaults. The values are SDL key names.

## 0.0.1 — 2026-09-03

First day. Project created at `Fable 2 Recompile Xbox/fable2recomp`, carrying
over the toolchain and the hard-won lessons from `ng2recomp` (Ninja Gaiden II).

### Added

- **Disc and XEX pipeline.** `tools/extract_disc.py` parses the GOTY ISO as
  XGD2 (451 files, 6.5 GB) and extracts it to `game/`; `tools/xex_image.py`
  decrypts and decompresses `default.xex` to a flat guest image.
- **Codegen with zero analysis errors.** `fable2_manifest.toml` plus 13 manual
  function-boundary overrides in `config/functions.toml`.
- **`tools/resolve_calls.py`** — runs codegen, registers every function it
  reported as an unresolved call, and repeats to a fixpoint. Registering one
  forwarder exposes the next, so this cannot be done in a single pass.
- **`tools/find_setjmp.py`** — locates `_setjmp` and `longjmp` by shape in a
  stripped image. Found `_setjmp = 0x83000200` (no `.pdata` entry of its own)
  and `longjmp = 0x82CA9260`, verified as exact mirrors, and named both in the
  manifest. On NG2 this pair was the single biggest fix and it was found only
  after days of chasing a crash far from its cause.
- **`tools/boot.py`** — launches the build for a fixed time, stops it *by PID*,
  and summarises progress markers and failure signatures from the log.
- **`tools/boot_loop.py`** — the run → crash → register → rebuild cycle,
  automated.
- **`tools/capture.py`** and **`tools/play_probe.py`** — photograph the window
  with Windows Graphics Capture, resolving the HWND from the PID we launched,
  and drive it with real `SendInput` keystrokes. Both NG2 traps are avoided by
  construction: a screen-region grab photographs whatever is on top, and
  `PostMessage` does nothing because SDL3 reads raw input.
- **`tools/scan_fnptrs.py`** — finds missed functions in bulk by walking the
  vtables and dispatch tables in `.rdata`/`.data`, rather than one runtime
  crash per three-minute rebuild. Converged on **103** of them, taking the
  total to 118 (17 further candidates were excluded along the way). It carries its own `--check` (against the hand-found answers)
  and a `--prune` fixpoint loop with an exclusion list at
  `config/fnptr_exclude.txt`, because the first version broke things three
  ways: unclamped sizes made codegen reject the manifest while the old
  executable stayed in the build directory and looked like a success; four
  splits severed a `b` from its target; and reading
  `codegen.partition.json` raw made the tool treat its own previous output as
  the analyzer's opinion, silently collapsing 121 candidates to 13.
- `tools/build.cmd` / `tools/run.cmd`, carrying NG2's two build lessons:
  codegen runs as its own step before CMake (or the PCH is built from a stale
  copy and every TU fails), and only `Release` strips the line tables that make
  a fault resolve to a single guest instruction.
- `src/fable2_app.h` selecting the Xenos GPU plugin — a `RuntimeConfig` field,
  not a cvar; without it the runtime silently ignores every `Vd*` call.
- `README.md` with the measured facts about this build and why the upstream
  Fable2Recomp config does not apply to it.

### Verified

- **The game reaches its New Game character-select screen.** `fable2.exe`
  builds (78 MB, 285 TUs, about 3 minutes), boots through the Microsoft and
  Lionhead logo videos, shows the Fable II title screen, accepts a button
  press, opens the main menu, and goes on to character select **with the 3D
  boy and girl models rendering on their cards** — so it is loading and drawing
  game assets, not just front-end art. Photographed with `tools/capture.py` and
  `tools/play_probe.py`; shots in `out/shots/`.
- **Input works** through the SDK's keyboard-to-controller emulation
  (`--mnk_mode=true`; a bare `--mnk_mode` is silently ignored).
- **The Bink videos decode correctly.** No artifacts. This is the failure that
  `ng2recomp` is still stuck on, and the reason is structural: NG2's WMV
  decoder runs as *GPU* shaders inside the plugin, while Bink is CPU code the
  recompiler simply translates.
- **Fable II has no guest fiber machinery**, which was NG2's single biggest
  structural problem. There are only three `std r1`/`ld r1` sites against a
  non-`r1` base in 4.5 M instructions, and two of them are the `setjmp` /
  `longjmp` pair itself — which independently confirms that identification.
- The GPU is genuinely rendering: 21 MSAA colour and depth render targets out
  of emulated EDRAM, 16 graphics pipelines compiled, D3D12 on the RTX.
- `skip_lr` is safe for this image — zero `bl $+4` PC-capture idioms in
  4,525,361 instructions.

### Known issues

- **3,856 `BaseHeap::AllocFixed attempting to reserve an already reserved
  range` errors**, all in one burst at boot. The guest's own allocator is
  reserving ranges the SDK heap already holds. Upstream Fable2Recomp replaced
  the game's physical allocator with dlmalloc (`src/heap.cpp`) — that is
  probably the same problem, and is the first thing to look at next.
- One `CommandProcessor::WriteRegister index out of bounds: 28685`. NG2 saw a
  storm of these when its command stream was misparsed; here it is a single
  occurrence and rendering is unaffected.
- `update:\` is not mounted, so the game's probe for a title update fails
  harmlessly. Correct for a disc with no TU.

### Verified

- **The game reaches gameplay.** Boots → title → main menu → New Game character
  select → the opening cinematic in-engine → **Old Bowerstone**, with snow,
  brazier fire, particles and the tutorial hint up. A 300-second run logs
  **zero fatals** and 9,314 lines.
- 168 → 166 function-boundary overrides (15 hand-found, 151 generated,
  19 excluded).

### Known issues

- **Rendering freezes ~3.5 minutes in.** The presented frame goes static and
  blue and never changes, while the guest keeps running and the GPU keeps
  compiling pipelines. No ring-buffer failure is logged, unlike NG2's
  superficially similar symptom. This is the top blocker.
- The **3,856 `BaseHeap::AllocFixed ... already reserved range` errors** are
  **not** blocking and have been demoted. They are one burst at boot, from
  Lionhead's own `CDynamicMemoryPageAllocator<CPhysicalAlloc>` /
  `<CVirtualAlloc>` (the names are in `.rdata`), and the game then loads
  6.5 GB of assets, renders 3D and saves a game on top of them. Upstream
  replaced the guest's physical allocator with dlmalloc, but it was working
  from a far earlier failure point; there is no evidence here that this is
  worth the invasiveness.

### Notes

- This disc is the base GOTY build `0.0.0.26`, **not** TU1. The upstream
  Fable2Recomp repo targets GOTY TU1, and its 923 function overrides were
  checked against this image and rejected: 164 of them land past the end of our
  `.text`, and its `setjmp`/`longjmp` addresses decode as unrelated
  instructions here.
- Codegen's *"Function 0x82242ED0 is 2433590 bytes"* warning is about the size
  of the emitted C++, not the guest function. That function is a real 109 KB /
  27,000-instruction routine; it compiles.
