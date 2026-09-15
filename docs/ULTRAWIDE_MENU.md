# Ultrawide menus & the texture flash — state and handoff

Written 2026-09-15 for **v0.2.10**, updated the same day for **v0.2.11**. Read
this before touching the ultrawide menu handling or the streaming texture
flash. It records what is fixed, how it was found, what was tried, and the
tooling that finally made the transitions visible.

## TL;DR

| Item | State |
| --- | --- |
| White/magenta **streaming texture flash** | **FIXED** (v0.2.11: plugin `readback_await_before_texture_upload`; the v0.2.10 drain is off) |
| Frame rate with the flash fix | **FIXED** (v0.2.11: the v0.2.10 drain cost 28–38 guest fps in town) |
| Intro / loading / main menus at ultrawide | 16:9 letterbox (correct) |
| Gameplay HUD at ultrawide | 16:9 band (correct, the c8 fix) |
| Pause / Up menu (steady) at ultrawide | full width, map reads as a wide oval (accepted trade) |
| Pause / Up menu **open/close dissolve** | **FIXED** (v0.2.11: plugin `[uw-menu]` rule, see below) |

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
3. **2D compression** — `fable2_uw_2d_k` (plugin cvar): when `> 0` the GPU
   plugin scales 2D draws back into a centred 16:9 band: the pixel-to-clip
   constant **c8** of the 2D HUD (`command_processor.cpp`, the `[uw-2d]` block,
   ~line 5090) and the x scale of the **perspective HUD widgets** — draws with
   the depth test off through the UI's hard-coded 16:9 projection
   `c0 = (6.303,0,0,0)`, `c1 = (0,0,11.2,0)`. This is how the HUD stays 16:9
   while the world fills the screen. `Hud2DFactor` in the app returns
   `1777.8/aspect` (or `"0"` = off).

**Pause-menu detection** — guest byte `0x834B2467` (host = guest + `0x100000000`,
the mapping is flat). `== 1` while the pause (Start) or Up menu is open, `0` in
gameplay. Measured at ~10 kHz: rock-solid `0` in gameplay, and in a menu it is
`1` but the game clears it for a sub-millisecond window each frame, so a
once-per-frame read catches an isolated 1-frame `0` on ~2–6% of frames.
`fable2::PauseMenuOpen()` reads it (`patch_hooks.cpp`). It **lags the visual**:
after the Start press the world keeps drawing for ~4 frames and the flag flips
~0.4 s later; on close the world is back ~190 ms after the press and the flag
drops ~45 ms after that (menu_probe, 45 fps, 2026-09-15).

## Current menu logic — "Option B" (v0.2.10, unchanged in v0.2.11)

In `OnDraw`, inside `if (st->ultrawide && aspect > 1800)`:

```
frontend  = !WorldCameraLive()                 // title / load: no world
pause_menu = !frontend && PauseMenuOpen()      // debounced 150 ms
gameplay   = !frontend && !pause_menu
present_letterbox = frontend ? "true" : "false"    // 16:9 ONLY for the front end
fable2_uw_2d_k    = gameplay ? Hud2DFactor(true) : "0"   // HUD 16:9 in play; menu full width
```

A `[uwstate]` log line records every change of that decision with the values
applied (v0.2.11). Rationale (learned the hard way, do not undo):
- The presenter is **never** switched between gameplay and the pause menu, so the
  swap chain never re-lays-out → no ghosted edges.
- The pause menu is left full width (compression off), so the game's menu-dim
  covers the whole screen and the menu art is not squeezed.
- No veil/fade → no menu flash.

### What was tried and rejected (do not repeat)
- **present_letterbox = 16:9 for the pause menu + fade** (v0.2.9): ghosted
  ultrawide edges during the re-layout, and the menu-dim squeezed to centre.
- **A pre-veil that darkens during a close confirm**: darkened the steady menu
  on the flag's flicker → visible "menu flash".
- **c8 on during the pause menu**: the menu shrinks to centre with the world at
  the edges → "squishing".
- **Fade the world's FOV to 16:9 during the menu**: the frozen menu camera keeps
  its wide FOV for the first reveal frame anyway.
- **An open-only veil** to cover the compression lag on open: no visible change.
- **Hunting the controller Start button** in guest memory: not where a
  held-button diff can see it.

## The menu dissolve — RESOLVED in v0.2.11 (what it really was)

**Symptom** (user): opening the pause/Up menu, the picture (the hero) was
"compressed to 16:9" for a moment; closing it, a semi-transparent menu layer
stayed in a centred 16:9 band with the world at the ultrawide sides.

**Ground truth** came from a **per-draw dump** of a real open and close (plugin
cvar `gpu_draw_dump_frames`, armed by the pad command `dump:40` on the line
before `start:0.35`; analysed with `analyze_dump.py`):

- Fable II's whole UI is **one shader pair** (vs `7B22146D51E0E4F5` / ps
  `0542FFDF2B118509`) drawing 6-vertex quads through the hard-coded 16:9
  perspective above. The gameplay HUD is 17 such quads, depth test **off**,
  |x| ≤ 6.4. The steady pause menu is 256 of them, depth test **on**, in
  menu-only frames (553 draws; the world's 3,568 are not drawn at all).
- The **transition** is the last 4 gameplay frames after the Start press and the
  first 3 world frames after a close: the world is drawn, plus **five** of those
  quads with the depth test **off** — one spanning `x -7.6..7.6` (the entire
  16:9 frame; its texture is a 1024x512 GPU-written capture of the menu,
  alpha-blended = the dissolve) and four leather side pieces at `±1.6..±7.7`.
- During those frames `fable2_uw_2d_k` is still `> 0` (the flag lags), so the
  s79 perspective-widget rule compressed the five quads into the centred band
  while the world stayed edge to edge. That band **is** the "compressed hero"
  on open and the "layer not reaching the edges" on close. Not the presenter,
  not the c8 path, not app timing.

**Fix** (plugin s91, `patches/scripts/patch_uw_persp_edge.py`): in the
perspective-widget rule, a quad whose vertex x extent reaches the frame edge
(`max(|x|) >= 7.0`, read with `C8QuadSpan`) keeps its full width; HUD widgets
never reach it. Log: `[uw-menu] transition layer quad kept full width: x ...`
(first 12). Verified with the 45 fps probe: world → menu → world with no band.

## Texture flash — the v0.2.11 fix (s96) and how the earlier ones were wrong

**Mechanism** (found 2026-09-15 by reading the upload path after every other
theory failed to survive the counters): with `draw_resolution_scale` 2 a resolve
writes only the **scaled** resolve buffer. The **unscaled** shared-memory buffer
keeps whatever the CPU last uploaded there — for the impostor pool, its fill. A
texture object reads the unscaled copy when its key has `scaled_resolve = 0`,
which is decided at creation (`FindOrCreateTexture`: any page scaled → 1) and
whose page bits `ScaledResolveGlobalWatchCallback` clears on a CPU write beside
the render. `UploadRanges` skips pending-readback pages (`SplitAroundProtected`),
so those pages of the unscaled buffer stay at the fill → the white/magenta
flash of every canopy at once for one frame.

- **v0.2.10 drain** (`readback_resolve_drain_large_kb=128`): the synchronous CPU
  copy got uploaded over the fill on the next invalidation → no flash, but a full
  queue wait ~1,100×/s → 28–38 guest fps in town, 49–53 at the lake.
- **s90/s92 wait-before-upload**: placed in `LoadTextureDataFromResidentMemoryImpl`,
  which runs AFTER `RequestTextures` has already uploaded the texture's invalid
  pages (`shared_memory.RequestRanges` comes first) — so it always saw "all pages
  valid, 0 awaited" and never did anything. The hook now lives in the upload.
- **s93 boundary after every resolve / s95 split before a load**: ~4,200 resp.
  ~2,700 submissions/s = 27–41% of the GPU command thread (s94 counters:
  `submissions N x T ms`), the game thread spinning on it. Kept as experiment keys.
- **s96 mirror** (`patch_mirror_unscaled.py`, `readback_resolve_mirror_unscaled`,
  default on): the readback path's 1x downscale is also copied into the unscaled
  shared-memory buffer on the GPU (`UseAsCopyDestination`), right after the
  readback copy. ~4,100 copies/s at the lake for ~1–2% of the thread; lake 55–60.
- Lake fps by variant (sweep, vsync 60): all boundaries 49–53; split-before-load
  ≈ same; UAV barrier + ≤64K boundaries 53–58; no barrier 57–60; mirror 55–60.
- **Still true**: never re-enable `ShouldDeferTextureUpload` (black sky).
- Cost counters on the fence line: `landings N x T ms (lock L ms)` (readback
  copies into guest memory: ~4,000/s, ~22 µs each, no lock contention),
  `submissions N x T ms (S splits before a load), M mirror copies`.

## Tooling & workflow

- **Build app**: scratchpad `build_renamed.cmd <N>` (renames a running exe aside
  with the Windows `findstr`, not Git's `find`; links a fresh one; log
  `out/build_tu1_<N>.log`). `src/fable2_texnotify.cpp` is CRLF: a patch script
  must match `\r\n` or assert.
- **Build plugin**: scratchpad `build_plugin.cmd <tag>` → `deploy_pair.ps1 -Tag
  <tag>` (game closed; keeps the previous pair as `dll_prev_<tag>`).
- **Launch Hero 1 into the forest**: `launch_hero.ps1 -Pad "14:a,19:down,21:a,25:a"`
  (the main menu starts on New Game: title A, DOWN, A = Continue, A = left slot).
  If it lands on the title screen, drive it with the eye MCP's `hand_padscript`.
- **Draw dump**: write `dump:40` then `start:0.35` to `pad_script.txt` (the eye
  tool's `hand_padscript` refuses unknown commands, so write the file directly);
  `draw_dump_<n>.txt` lands beside the exe; `analyze_dump.py` lists the shader
  pairs that come and go across frames and a per-frame census.
- **See the transition**: `tools/diag/menu_probe.py <outdir> 7` (45 fps GDI
  capture + pause-flag read + Start presses at 1.3 s and 4.2 s on one clock),
  then `tools/diag/montage.py <outdir> <ms_lo> <ms_hi> sheet.png 4`.
- **Never** close a process by title or blind-kill; use `close_game.ps1`
  (CloseMainWindow by PID). Coordinate the machine via `~/.game-test-lock`.
