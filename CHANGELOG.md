# Changelog

All notable changes to fable2recomp. Versions follow the project's own
numbering, not the game's.

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
- **`tools/scan_vtables.py`** — finds missed functions in bulk by walking the
  vtables and dispatch tables in `.rdata`/`.data`, rather than one runtime
  crash per three-minute rebuild. Converged on **103** of them, taking the
  total to 118 (17 further candidates were excluded along the way). It carries its own `--check` (against the hand-found answers)
  and a `--prune` fixpoint loop with an exclusion list at
  `config/vtable_exclude.txt`, because the first version broke things three
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

### Notes

- This disc is the base GOTY build `0.0.0.26`, **not** TU1. The upstream
  Fable2Recomp repo targets GOTY TU1, and its 923 function overrides were
  checked against this image and rejected: 164 of them land past the end of our
  `.text`, and its `setjmp`/`longjmp` addresses decode as unrelated
  instructions here.
- Codegen's *"Function 0x82242ED0 is 2433590 bytes"* warning is about the size
  of the emitted C++, not the guest function. That function is a real 109 KB /
  27,000-instruction routine; it compiles.
