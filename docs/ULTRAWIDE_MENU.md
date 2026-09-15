# Ultrawide menus & the texture flash — state and handoff

Written 2026-09-15 for **v0.2.10**. Read this before touching the ultrawide
menu handling or the streaming texture flash. It records what is fixed, what is
still open, exactly what was tried, and the one thing that would unblock the
last item.

## TL;DR

| Item | State |
| --- | --- |
| White/magenta **streaming texture flash** | **FIXED** (plugin `readback_resolve_drain_large_kb=128`) |
| Intro / loading / main menus at ultrawide | 16:9 letterbox (correct) |
| Gameplay HUD at ultrawide | 16:9 band (correct, the c8 fix) |
| Pause / Up menu (steady) at ultrawide | full width, map reads as a wide oval (accepted trade) |
| Pause / Up menu **open/close dissolve** | **OPEN ISSUE**: the fade-in/out draws at 16:9, world shows through the sides |

The dissolve is the only menu item left. It is a **GPU-plugin 2D-draw** problem,
not app-side.

---

## How Fable II ultrawide works (the moving parts)

Three independent mechanisms, driven by the app's `PerfHudOverlay::OnDraw`
(`src/fable2_texnotify.cpp`) once per presented frame:

1. **3D FOV** — `fable2PatchFieldOfView` in `src/patch_hooks.cpp` widens the
   world projection to `fable2_display_aspect_x1000` (a plugin cvar) when it is
   `> 1800`; at `<= 1800` it leaves the game's native 16:9. The app publishes
   the window aspect into that cvar.
2. **Presentation** — `present_letterbox` (plugin cvar, in `rexglue-src`
   presenter): `true` = pillarbox to 16:9 with black bars; `false` = stretch the
   16:9 render edge to edge. In ultrawide gameplay it is `false`, and the wide
   FOV + the stretch **cancel** into a correct, wider picture.
3. **2D compression (c8)** — `fable2_uw_2d_k` (plugin cvar): when `> 0` the GPU
   plugin scales the pixel-to-clip constant **c8** of 2D draws back into a
   centred 16:9 band (`command_processor.cpp`, the `[uw-2d]` block, ~line 5080).
   This is how the HUD stays 16:9 while the world fills the screen. `Hud2DFactor`
   in the app returns `1777.8/aspect` (or `"0"` = off).

**Pause-menu detection** — guest byte `0x834B2467` (host = guest + `0x100000000`,
the mapping is flat). `== 1` while the pause (Start) or Up menu is open, `0` in
gameplay. Measured at ~10 kHz: rock-solid `0` in gameplay (never spikes), and in
a menu it is `1` but the game clears it for a sub-millisecond window each frame,
so a once-per-frame read catches an isolated 1-frame `0` on ~2–6% of frames.
`fable2::PauseMenuOpen()` reads it (`patch_hooks.cpp`). It also **lags the
visual by ~1 frame** on open/close (the game reveals/hides the world a frame
before it flips the flag).

## Current menu logic — "Option B" (v0.2.10)

In `OnDraw`, inside `if (st->ultrawide && aspect > 1800)`:

```
frontend  = !WorldCameraLive()                 // title / load: no world
pause_menu = !frontend && PauseMenuOpen()      // debounced 150 ms
gameplay   = !frontend && !pause_menu
present_letterbox = frontend ? "true" : "false"    // 16:9 ONLY for the front end
fable2_uw_2d_k    = gameplay ? Hud2DFactor(true) : "0"   // HUD 16:9 in play; menu full width
```

Rationale (learned the hard way, do not undo without reading below):
- The presenter is **never** switched between gameplay and the pause menu, so the
  swap chain never re-lays-out → no ghosted edges.
- The pause menu is left full width (c8 off), so the game's menu-dim covers the
  whole screen and the menu art is not squeezed → no centre-only overlay in the
  **steady** menu.
- No veil/fade → no menu flash.

### What was tried and rejected (do not repeat)
- **present_letterbox = 16:9 for the pause menu + fade** (v0.2.9): ghosted
  ultrawide edges during the re-layout, and the menu-dim squeezed to centre →
  the user's "Error 1/2" screenshots. Rejected.
- **A pre-veil that darkens during a close confirm**: darkened the steady menu
  on the flag's flicker → visible "menu flash". Rejected (removed).
- **c8 on during the pause menu** (compress the menu to 16:9 with black bars):
  the menu shrinks to centre with the world at the edges → "squishing". Rejected.
- **Fade the world's FOV to 16:9 during the menu** so a revealed world frame is
  pillarboxed not squished: works in capture, but the frozen menu camera keeps
  its wide FOV for the first reveal frame anyway. Superseded by Option B.
- **An open-only veil** to cover the 1-frame compression lag on open: did not
  change what the user sees. Removed.
- **Hunting the controller Start button** (to lead the lagging flag) in guest
  memory: not in the data segment (`0x82–0x85`) nor the mapped heap regions
  (`0x30/0x40/0x45–0x4B/0x6C/0x7F/0x90–0x96`) via a held-button diff. The game
  does not keep the raw button word where a memory diff sees it.

## THE OPEN ISSUE — the menu dissolve draws at 16:9

**Symptom**: opening the pause/Up menu, the menu fades in inside a centred 16:9
band (a brief "squish"); closing, the menu fades out the same way, leaving a
semi-transparent 16:9 layer with the world showing through the ultrawide sides.
The **steady** menu is full width; only the **dissolve** is 16:9.

**Ground truth from a per-frame diagnostic** (a temporary `[menuk]` log of
`frontend/pause_menu/gameplay/rawflag/k2d`): during the entire close dissolve
`fable2_uw_2d_k` is **`0`** (full-width mode) — it does not flip to compression
until ~340–400 ms after the press, long after the dissolve. So the dissolve at
16:9 is **not** caused by the c8 compression or by app-side timing. The steady
menu is full width with `k2d=0`; the dissolving menu is 16:9 with the same
`k2d=0`. That means the game draws the menu **transition** with a different
draw/shader that the presenter's edge-to-edge stretch does not reach — a
GPU-plugin 2D-path detail.

**Where to look next** (needs the GPU plugin, `rexglue-src`):
- Identify the menu fade-in/out draw. The `[uw-2d]` c8 census logs distinct 2D
  draws (vs/ps hashes, textures, span). During a menu open/close, find the draw
  that renders the dissolving menu and whether it is a c8 pixel-scale draw, a
  full-screen quad, or drawn to a separate target/after the stretch.
- The steady menu is full width because `present_letterbox=false` stretches the
  16:9 render. If the dissolve is drawn to a different surface or with different
  coords, it is not stretched. Likely fixes: stretch that draw in the plugin, or
  exempt it the way the NG2 solid-fill scene-fade is left full width
  (`command_processor.cpp`, the `solid_fill`/`full_width` logic near the c8
  block).
- RenderDoc on an open/close frame would identify the draw immediately.

## Texture flash fix (done)

`readback_resolve_drain_large_kb` (new plugin cvar, default **128**) in
`rexglue-src/src/graphics/d3d12/command_processor.cpp`. In the resolve
`sync_now` decision (~line 3768) a resolve of `>= drain_large_kb` KB (default 128) is landed
**synchronously** (wait for the GPU, copy into guest memory now) instead of
deferred. So a large render-to-texture target (sky, water, distant impostor) is
valid in guest memory before any texture reads it — no stale/empty bytes, no
flash. Small resolves stay async (cheap).

- **Why sync, not defer**: an earlier `ShouldDeferTextureUpload` that deferred
  prior-submission uploads **black-screened the sky** — the sky is re-resolved
  every frame, so its readback is always pending, so it deferred forever and
  never uploaded. Syncing is what the miss path already does and cannot
  black-screen. That defer code is reverted; do not re-enable it.
- **Cost**: ~15% fps in a heavy-streaming scene (~175 fps at Bower Lake, GPU had headroom), 0
  hitches, sky never black. Tune the threshold up for less cost / down to catch
  smaller flashes.
- The mechanism (GPU-written texture uploaded while its resolve readback is in
  flight) is named in the `[diag] reload frame ... GPU-written READBACK-PENDING`
  log line.

## Tooling & workflow

- **Build app**: scratchpad `build_renamed.cmd <N>` (renames the running exe
  aside, links a fresh one; log `out/build_tu1_<N>.log`, grep for `error`).
- **Build plugin**: scratchpad `build_plugin.cmd <tag>` → `deploy_pair.ps1 -Tag
  <tag>` (game must be closed; keeps the previous pair as `dll_prev_<tag>`).
  Back up the working pair first (`RexBlue/win-amd64/bin/dll_backup_*`).
- **Launch Hero 1 into the forest**: `FABLE2_PAD_SCRIPT="12:a,16:down,18:a,22:a"`
  (LEFT save slot). Live input: write to
  `out/build/win-amd64-Release/pad_script.txt` — `start:0.35`, `up:0.3`,
  `l:0,1:0.5` (stick fwd), `r:1,0:0.5` (pan), `release`.
- **Read the pause flag / drive the menu on one clock**: scratchpad
  `menu_probe.py` (GDI StretchBlt capture + `ReadProcessMemory` of `0x834B2467`
  at host `0x100000000+0x834B2467` + pad file, classifies each frame
  ULTRAW/BARS169/BLACK). `close_burst.py`, `open_burst.py`, `cl98`-style close
  captures for full-res transition frames.
- **Never** close a process by title or blind-kill; use `close_game.ps1`
  (CloseMainWindow by PID). Coordinate the machine with the NG2 session via
  `~/.game-test-lock`.

## The verification limitation (important)

The menu-transition artifacts are **single frames at ~170 fps**. The GDI screen
captures here sample at ~40 fps and land between them, and the in-game scene even
changes between captures (evening vs morning light), so the capture repeatedly
disagreed with what the user sees — every menu fix read as "same" to them. The
real unblock is the user's **AI Vision `eye` MCP server** (`eye_*` vision +
`hand_*` input, `hand_padscript` proven on Fable II) **attached to the coding
session**, so the transient can be seen directly. It was not connected here.
When it is, drive the menu with `hand_padscript`, capture the open/close with
`eye_*`, and identify the dissolve draw.
