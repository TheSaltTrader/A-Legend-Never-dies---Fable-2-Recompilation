# What ng2recomp learned, and where each lesson stands here

`ng2recomp` (Ninja Gaiden II, same SDK, same machine) took two days to reach a
rendered start menu. Most of that time went on a handful of problems that are
invisible until they are not. This file is the checklist: every one of those
findings, and whether it has been applied, ruled out, or is still waiting.

Status key: **applied** · **not needed here** · **pending** · **unknown**

---

## Codegen and correctness

| Lesson | Status |
|---|---|
| `.pdata`-less helpers get absorbed into their neighbour and must be registered by hand | **applied** — 166: 15 hand-found via `resolve_calls.py`/`boot_loop.py`, plus 151 from `scan_fnptrs.py` (19 excluded) |
| `setjmp`/`longjmp` must be named in the manifest, or an error path returns instead of unwinding and corrupts memory far from the crash | **applied** — `0x83000200` / `0x82CA9260`, found up front by `find_setjmp.py` |
| `non_volatile_as_local` + `skip_lr`/`ctr`/`xer`/`cr`/`reserved_as_local` — defaults share one `PPCContext`, so a callee can lose its caller's r14–r31 | **applied**; `skip_lr` verified safe (zero `bl $+4` PC-capture idioms) |
| When replacing a guest function, check whether anything branches into the **middle** of it | **pending** — NG2 hit this three times (`_setjmp` inside `longjmp`, `SwitchToFiber`'s fast-path second entry, `__savegprlr`). Nothing has needed replacing here yet. |
| **`b $+4` is not a terminator.** MSVC emits it as a no-op; it is an unconditional `b` that falls through. Splitting a function after one orphans the body from the prologue that set `r31`, and `non_volatile_as_local` then gives the orphan a zeroed `r31` — a clean null read at the exact offset the code uses | **applied** — cost one regression (guest `0x54`), found by `tools/bisect_fnptrs.py` in eight rebuilds |
| Bulk static discovery of missed functions does not work — `.text` holds pointer tables and a pointer starting `0x82…` decodes as a plausible `lwz` | **applied** — `scan_missed.py` is diagnostic only. But walking vtables in `.rdata`/`.data` DOES work: `scan_fnptrs.py` found 151 in one pass, from two channels (pointer tables in `.rdata`/`.data`, and addresses materialised in code with `lis`/`addi`). But the trap is real and it bit: channel 2 returned nine switch **jump tables** for every real function until they were rejected by reading the target as data rather than as code. |

## Runtime setup

| Lesson | Status |
|---|---|
| `config.gpu_plugin = "xenos"` is a `RuntimeConfig` **field**, not a cvar. Unset, the runtime runs "native rendering mode" and ignores every `Vd*` call — black window, no error | **applied** in `Fable2App::OnPreSetup` |
| Use the ROV render-target path (the default). On NG2 the RTV path was effectively broken: 3128 EDRAM resolves vs 22 over the same 30 s | **not changed** — ROV is the default; do not switch without measuring |
| NG2's tasks are **fibers**, and a guest fiber switch cannot work under static recompilation — the family had to be mapped to the SDK's `rexcrt` host implementations | **not needed here** — measured: only three `std r1` / `ld r1` sites against a non-`r1` base exist in 4.5 M instructions, and two of them are the `setjmp`/`longjmp` pair itself. There is no guest fiber machinery in Fable II, and that cross-check independently confirms the setjmp/longjmp identification. |
| Low guest memory had to be committed (page 0 upward, read-only zeroes) because BSS slot records index off address 0 | **not applied** — deliberately. It masks real null dereferences, so it should only go in if a fault actually lands in low memory. |
| `--license_mask` fakes DLC entitlement for LIVE-signed STFS content | **probably not needed** — the GOTY disc carries both expansions on-disc |
| A bare `--flag` does **not** set a boolean cvar; it is silently ignored. Write `--flag=true` | **applied** — noted in `run.cmd` |
| Window cvars (`fullscreen`, size) are read when the window is created, in `SetupPresentation`, which runs **before** `OnPreSetup` — they must be set in `OnConfigurePaths` | **pending** — no settings UI here yet |
| `PostMessage(WM_KEYDOWN)` does nothing: SDL3 takes input from raw input and ignores synthesised window messages | **applied** — `play_probe.py` uses `SendInput` and checks the window actually reached the foreground first |
| `tools/ui_probe.py` must call `SetProcessDpiAwareness(2)` first, or every coordinate is off by the display scale | **applied** in `play_probe.py` |
| Do not guess at controller bindings — pin them. Fable II's character select reads the **left stick**, not the d-pad, and guessing produced a confident wrong conclusion (`right` looked like it worked when `tab`, two presses earlier, had) | **applied** — `play_probe.py` passes explicit `--keybind_*` SDL key names, and a per-key frame-delta measurement identifies which key did what |

## Video

NG2's remaining blocker is its intro video, and the reason is worth stating
because **Fable II is in a different and much better position**.

NG2 links Microsoft's XMEDIA WMV decoder, whose decode path is *GPU* DXVA:
detiling, residual reconstruction and range reduction all run as shaders inside
`rexgpu-xenos.dll`. That put the bug somewhere the project has no source for,
and re-encoding the source video only ever partially helped.

Fable II has `BINK`, `BINKBSS` and `BINKDATA` sections — RAD's Bink, statically
linked and **decoded on the CPU**. A CPU decoder is just more PowerPC for the
recompiler to translate. **Confirmed working**: the Microsoft and Lionhead logo
videos play correctly on the first run, with no artifacts and no work done.

If they do misbehave, the NG2 escape hatch still applies in principle: the XEX
does not validate video length, size or hash, so the source file is a variable
under our control. See `ng2recomp/README.md` §"Fixing the video by re-encoding
it" — but note the trap recorded there, that a scoring metric which rewards
"fewer artifacts" also rewards a half-empty frame.

## Debugging method

| Lesson | Status |
|---|---|
| Build anything but `Release` — codegen emits one source line per guest instruction, so a fault resolves to the exact PowerPC instruction | **applied** in `build.cmd` |
| `cdb -g` makes the first prompt *the crash*, so a `-c` script sets its breakpoints after the fault. Tell: *"Continuing a non-continuable exception"* | **pending** |
| `cdb ba` watchpoints are **per thread** (debug registers). Arm them from a breakpoint already on the thread you care about | **pending** |
| Absence of logging is not absence of behaviour — the runtime does not log a successful file open. Break on `NtCreateFile_entry` instead of inferring from the log | **applied** — recorded here so it is not re-learned |
| Never screenshot by capturing the screen region under a window; it photographs whatever is actually on top. `PrintWindow` cannot read a D3D12 swapchain either | **applied** — `tools/capture.py` and `tools/play_probe.py`, Windows Graphics Capture with the HWND resolved from the PID we launched |
| Never stop a process by matching a window title — kill the PID you started | **applied** in `boot.py` and `boot_loop.py` |
| Validate any new scanner against a known answer first. NG2's `xrefs.py` shipped three bugs that each gave confident wrong answers | **applied** — `find_setjmp.py`'s answer was verified by checking that setjmp and longjmp use the same buffer offsets |
