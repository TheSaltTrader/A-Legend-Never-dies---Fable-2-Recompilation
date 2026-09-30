# The recipe: taking a new Xbox 360 game from disc to a shipped PC port

Written 2026-09-29 from two ports done on this machine with the same toolchain:
**Fable II** (`fable2recomp`, this repository, public as *A Legend Never Dies*) and
**Ninja Gaiden II** (`ng2recomp`, public as *Ninja's Dawn*). Everything below is
something one of the two projects actually did, in the order it turned out to
matter. Where a step has a file, the file is named; where a lesson cost a day,
the day is named. Read it top to bottom once, then use Part 9 as the checklist.

Companion documents, all in this repository unless stated:
`README.md` (this port's full account and settings reference), `docs/NG2_LESSONS.md`
(every NG2 finding and whether it applied here), `docs/TU1_PORT.md` (the title-update
port log), `docs/TEXTURE_PACK.md`, `docs/UPDATER.md`, `docs/ULTRAWIDE_MENU.md`,
`docs/DRAW_DISTANCE.md`, `docs/native_gpu/HANDOVER.md` (the native renderer, start
there), `docs/HANDOFF_2026-09-29.md` (the flash hunt), and in the NG2 repository
`docs/ISSUES_AND_FIXES.md` (every problem met on NG2 and its fix, by area) and
`docs/DEVELOPMENT_JOURNAL.md`. The porting kit for the native renderer is
`C:\users\renoi\claudecode\NATIVE_GPU_MIGRATION_KIT\MIGRATION_GUIDE.md`.

---

## Part 0 - What the thing is, in one paragraph

This is **static recompilation**, not emulation. `rexglue codegen` (ReXGlue SDK
0.10.0, the toolchain re:Blue uses for Blue Dragon) translates the game's PowerPC
executable (`default.xex`) into C++ - one source line per guest instruction -
which clang compiles into an ordinary x86-64 program. The SDK supplies the Xbox
360 kernel, file system, audio and (originally) a Xenos GPU emulator around the
translated code; this port has since replaced the GPU emulator with its own
Direct3D 12 renderer. The player supplies their own disc; the repository and the
release contain no game data of any kind (`assets/`, `game/` and every disc file
are gitignored, and the packager refuses anything that looks like game data).

---

## Part 1 - Toolchain and layout

**Install once per machine**

- The ReXGlue SDK: `tools\build.cmd` looks for `%REXSDK%`, then
  `<project>\..\RexBlue\win-amd64`. Each project keeps its own copy beside it
  (`Fable 2 Recompile Xbox\RexBlue`, `Ninja Gaiden 2 Xbox360\RexBlue`).
- The SDK **source** (`rexglue-src`, LOCAL ONLY, never pushed): the GPU plugin
  and the runtime DLLs for both ports are built from `Fable 2 Recompile Xbox\
  rexglue-src`; its path is baked into the DLLs. Never rewrite its commits
  from e9834c4 on (NG2 re-bases on it).
- LLVM/clang 20+, Ninja, CMake 3.25+, MSVC Build Tools (Windows SDK headers and
  libs; `vcvars64.bat`), Python 3.12 with `capstone`. No vcpkg: SDL3, fmt,
  spdlog, utf8cpp and DXC ship inside the SDK.
- `gh` (GitHub CLI) authenticated, for releases.

**Project layout (copy this shape for a new game)**

```
<game>recomp/
  assets/default.xex           the XEX alone, what codegen reads     (never committed)
  game/                        the whole extracted disc              (never committed)
  <game>_manifest.toml         the codegen manifest (setjmp/longjmp, flags, includes)
  config/functions.toml        function-boundary overrides (hand-found, then generated)
  config/fnptr_exclude.txt     addresses scan_fnptrs.py must never register
  config/hooks/patches.toml    the hook table (60 fps, FOV, draw distance, ...)
  generated/default/           codegen output (500+ files, ~300-560 MB; never committed)
  src/                         the host application: settings, menus, updater, tools
  src/native_gpu_xlat/         the vendored renderer backend (rtc_d3d12/...)
  patches/                     this port's changes to the SDK source, as scripts
  tools/                       build.cmd, make_release.py, analysis and test scripts
  tools/test_legs/             scripted test runs (untracked; see Part 5)
  docs/                        the port log and feature notes
  VERSION, CHANGELOG.md        the release's two sources of truth
  out/build/win-amd64-*/       build output (an update stages itself in update/ beside the exe)
```

**Build and run**

```
tools\build.cmd Release          # codegen FIRST, then CMake + Ninja (~3 min Fable, ~6 min NG2)
tools\run.cmd
```

`build.cmd` runs codegen as its own step before CMake on purpose: codegen
rewrites `generated/default/<game>_pch.h`, and inside one ninja run the
precompiled header can be built from the old copy, after which every unit
fails with "file has been modified since the precompiled header was built".
`Release` strips debug info from the ~500 recompiled units; build
`RelWithDebInfo` before chasing a crash, because codegen emits one source line
per guest instruction and a fault then resolves to the exact PowerPC
instruction. A bare `--flag` does **not** set a boolean cvar; write `--flag=true`.
A version bump (`VERSION`) triggers a near-full rebuild; grep the ARTIFACT for
the version string afterwards (`Build version needs reconfigure`).

---

## Part 2 - Bring-up: from disc to the first rendered frame

**Day 1 order that worked (Fable II reached the world on day one; NG2 took two
days to a menu, mostly on the items below that were unknown then).**

1. **Extract the disc** (`tools/extract_disc.py`; the runtime mounts a folder,
   not an image). Put `default.xex` in `assets/`, the disc in `game/`.
2. **Write the manifest** (`<game>_manifest.toml`): `game_root = "assets"`,
   the entrypoint XEX, the output directory, the includes
   (`config/functions.toml`, `config/hooks/patches.toml`).
3. **Find `setjmp`/`longjmp` before running anything** (`tools/find_setjmp.py`).
   Leaving them unset is quiet and catastrophic: on NG2 it turned "abort this
   resource load" into "return normally with invalid state" and crashed minutes
   later on a garbage pointer, nowhere near the cause. Fable II: `0x83000200` /
   `0x82CA9260`.
4. **Codegen flags**: `non_volatile_as_local`, `skip_lr`, `ctr/xer/cr/
   reserved_as_local` (what re:Blue ships and what NG2 needed - the defaults
   share one `PPCContext` and a callee can lose its caller's r14-r31; on NG2 that
   was a real crash). Verify `skip_lr` is safe by counting `bl $+4` PC-capture
   idioms (Fable: zero in 4.5 M instructions).
5. **Iterate function-boundary overrides to a fixpoint** (`tools/resolve_calls.py`).
   MSVC emits no `.pdata` for small helpers (thunks, getters, forwarders), so a
   helper in the padding after a real function is absorbed into it and a branch
   into it fails validation ("target not in any function"). Registering one
   exposes the next; Fable needed 12 by iteration, 168 in total.
6. **Find missed functions in bulk** (`tools/scan_fnptrs.py`, two channels:
   code pointers stored as data in runs of two or more, and pointers built
   inline by `lis/addi`), requiring the instruction before the target to be one
   control cannot fall through. Bulk static discovery by decoding `.text`
   does NOT work (pointer tables decode as plausible `lwz`). Expect a few more
   at runtime as `[FATAL] Call to invalid or unregistered function`; ~3 minutes
   per rebuild is why the bulk scan exists. Note `b $+4` is not a terminator.
7. **Runtime configuration that is invisible until it is wrong** (from
   `docs/NG2_LESSONS.md`): the GPU plugin is a `RuntimeConfig` FIELD, not a cvar
   (unset, the runtime "renders natively" and ignores every `Vd*` call - black
   window); window cvars are read in `SetupPresentation`, before `OnPreSetup`,
   so set them in `OnConfigureParams`; NG2's tasks were fibers and had to be
   mapped to the SDK's host implementations; the ROV render-target path is the
   default and the RTV path was broken on both games; `PostMessage(WM_KEYDOWN)`
   does nothing (SDL3 uses raw input), drive input with `SendInput` or the
   in-process pad (below); call `SetProcessDpiAwareness(2)` before any screen
   coordinate.
8. **First boot triage**: `tools/boot.py` / `boot_loop.py` (boot, capture,
   compare), `tools/health.py`, `tools/freeze_stacks.cmd` (stacks of a frozen
   process). A picture that stops updating while the heartbeat ticks is not a
   hang (Fable's 3.5-minute "freeze" was the RTV path).
9. **Hooks, not patches to generated code**: everything that changes behaviour
   (60 fps, field of view, draw distance, skips) is a hook in
   `config/hooks/patches.toml` or code under `src/`, so it survives
   regeneration. NG2 kept two fixes outside the generator's reach as scripts
   applied after regeneration (`local\diag\patch_*.py`); prefer hooks.
10. **Title update**: apply the game's TU (`default.xexp`) at codegen AND at
    launch from `game/default.xexp` (`docs/TU1_PORT.md`); the setup screen says
    when the player's TU differs from the one the build read.

---

## Part 3 - The host application (what players touch)

Each port carries the same host layer under `src/`; port it file by file
(the NG2 -> Fable port took a day per feature, mostly settings wiring):

- **Setup screen and settings** (`<game>_menu.cpp`, `<game>_settings.h`): first
  launch takes the player's disc image or extracted folder, reads the title ID
  and refuses the wrong game, runs hardware detection for defaults, imports
  Xbox 360 saves and DLC. Every setting is a cvar the build registers; a
  setting shown to break the game leaves every settings screen; restart-bound
  settings are marked. Shift at launch reopens setup.
- **The in-process controller** (`<game>_autoskip.cpp`): `FABLE2_PAD_SCRIPT`
  presses at fixed seconds; `pad_script.txt` beside the exe is read and deleted
  within 0.1 s while the game runs (buttons, sticks `l:x,y[:s]`, `wait:s`,
  `release`); the game writes `pad_script.accepts` with its PID. This is how
  every scripted test drives the game without focus or a driver.
- **The updater** (`<game>_update.cpp`, `docs/UPDATER.md`): asks the releases
  page at launch (3 s at most), offers Update now / Not now / Skip; the tag and
  the `-win-amd64.zip` asset name are the contract; nothing installs without a
  click; the player's game folder, saves, settings and pack are never touched.
- **DLC and title update**: `<game>_dlc.cpp`, `<game>_titleupdate.cpp`; the GOTY
  disc carries its expansions, so `--license_mask` was not needed here.
- **Video**: Bink plays. Xbox 360 `.wmv` is VC-1 Advanced Profile + WMA Pro;
  ffmpeg cannot encode VC-1 and the demuxer is strict - replacements need
  Expression Encoder 4 (`memory: xbox360-video-replacement`).
- **Textures** (`docs/TEXTURE_PACK.md`, `<game>_textool.cpp`, `tools/ai_upscale.py`,
  the bundled Real-ESRGAN engine in `tools/upscaler/`): dump at load, decode,
  upscale, pack, replace at load with GPU-generated mip chains. THE lesson:
  the pack key must be `<address>-<content hash>` - an address-keyed key is
  fine inside a process and wrong as a disk key across scenes (streamed
  chapters collided: violet shop windows on NG2). Verify a dump wrote
  something before building a pack; verify the bundled engine is actually
  looked at (NG2 v1.0.14 shipped one nobody called).
- **Ultrawide and FOV** (`docs/ULTRAWIDE_MENU.md`, NG2's `ng2-ultrawide-fov`):
  scale projection column 0 by render/display aspect; detect scenes by draw
  count; the HUD and every "solid layer" (mist, fades, shop overlays) must be
  compressed with the HUD or they bleed to the sides; every fill <-> pillarbox
  switch fades, never cuts (user rule). Test full screen at the display's
  resolution, because a 16:9 window cannot show any of it (user rule).
- **60 fps, draw distance, LOD** hooks: `docs/DRAW_DISTANCE.md` (four fields in
  `globals.gdb`; much pop-in lives outside them).
- **Crash dumps** (`<game>_crashdump.cpp`): a minidump plus a symbolised stack
  in the log on every fault. Read the STACK, not the file name; tonight five
  crashes at one address were counted as one until the logs were grepped.

---

## Part 4 - The graphics stack

The path both ports walked, and where each stands:

1. **The SDK's Xenos GPU plugin** (Xenia-derived, `rexgpu-xenos.dll`): works,
   CPU-bound in towns (the plugin's GPU thread 100 % busy at ~37 fps in Fairfax
   castle; the card 47 % busy). Fixes ported from Xenia Canary as needed
   (`README.md`, "Porting fixes from Xenia Canary"). Get a value into the plugin
   through its cvars via the tuning file at launch; a live toggle needs the
   plugin's own mirror.
2. **Lockstep census, then a native backend** (`docs/native_gpu/*`,
   `NATIVE_GPU_MIGRATION_KIT`): count every surface the plugin touches
   (registers, shaders, fetch constants, resolves - the census enumerates its own
   subjects so what is NOT covered is counted); compile the SDK's DXBC shader
   translator into the app and prove it faithful by differential against the
   plugin's own dump (406/406); lift the untile and blend decoders with
   synthetic and real differentials; run the plugin and the native backend in
   LOCKSTEP fed by the same PM4 stream and compare pictures (a 0.001 floor);
   then offload. Judge by the lake AND the town. The transplanted D3D12 backend
   (`src/native_gpu_xlat/rtc_d3d12`: command processor, texture cache, render
   target cache = the EDRAM model, pipeline cache, shared memory) is game-
   agnostic and also builds as `ngpu_backend.dll`.
3. **The native front end** (`src/fable2_native_gs.cpp`, `src/fable2_p2_census.cpp`):
   the game's own graphics system - each kick is flattened on the game's thread,
   decoded and drawn on a recording thread, and the side effects (fences, read
   pointers, swaps, interrupts) go back through an executor; the runtime's
   vblank is a timer. The CP's classic primary-ring path is unused (an
   instrument that read its pointers measured nothing all evening). The decode/
   draw split (`ngpu_opt_split`) bought headroom and admitted a flash; it ships
   off since 1.3.6.
4. **Where time goes** (measured with xperf, `docs/native_gpu/FULL_NATIVE_PLAN.md`):
   ~22 % PM4 parse and bookkeeping, ~78 % issuing draws (UpdateBindings 26 %,
   sampler parameters 20 %, texture requests 10 %); the game's render thread
   waits ~60 % on this thread. "Taking the game's D3D calls instead of the
   command buffer" was estimated as multi-day for a small gain and HALTED by the
   user (09-28); do not reopen without asking.
5. **Renderer facts that cost a day each**: a read from a mapped upload heap
   stalls ~700 ns (mapped pointers are write-only); the scaled resolve buffer is
   guest tiled offset x scale^2 with each guest texel's 3x3 host texels
   contiguous; bindless and bindful both work and both showed the flash; the
   texture cache reloads triple-buffered CPU tables every frame and hashes them
   ("SKIP UNCHANGED"); the game's per-card impostor data lives in 8-byte
   vertices plus per-card lookup textures, not in a vertex record.

---

## Part 5 - Testing and measurement discipline

This is the part that decides whether a port ships something true. All of it
was learned by paying for its absence.

**Machine hygiene**

- One game at a time. Check `tasklist` for the game processes IMMEDIATELY
  before every launch, by process, not by a lock read a minute earlier (the
  shared-gate race was eight seconds tonight). The user's own game never writes
  a lock line; a game under the user's install means they are playing.
- The claim lock (`~/.game-test-lock`, `tools/test_legs/claim.sh`): advisory
  between sessions; write your own claim, re-read it after writing, never
  overwrite another session's line; a claim is honoured only while the game it
  names is alive (90 s with no game process = stale, logged, ignored). Stopping
  a leg's shells leaves its claim written - release it before relaunching.
- Never edit a running script; copy to a new name. Kill only PIDs you started,
  from PowerShell with a Name filter, never with a command-line pattern that
  appears in the command doing the killing (a process search by substring finds
  the search - it killed the caller twice tonight). Stopping a chain leaves its
  children alive; a leg's shells own the game's console pipe, its claim and its
  capture: to hand a leg to the user, write `release` to `pad_script.txt` and
  leave the launcher alive. A capture started for 900 s outlives a 4-minute run
  and photographs the desktop; a leg whose game crashed scores a perfect zero on
  an empty screen. Run `subjectcheck.py` (foliage, luminance, motion) before
  reading any number, and grep the log for `CRASH:` first.
- Running the game writes the user's saves (the content root IS the save
  folder): back up by content hash before the first launch that loads a save,
  keep two copies, write a restore manifest. Test in the user's configuration
  (full screen at the display's resolution), and read the reference's settings
  first (the plugin ran the pack at 2x).
- Stamp times from `date` in the same action that writes them; never estimate.
  Name the log line or drop the number.

**The leg** (`tools/test_legs/`): `arleg6.sh TAG MODE [TUNE] [SCALE]` takes an
atomic `mkdir launch_locks/TAG` first (a second launcher on the same tag dies in
milliseconds - two launchers once passed one tasklist gate within a second and
doubled the pad route), then `uwpansfr2.sh` claims the machine, launches
`arm_leg_tune.sh` (loads a save card, waits for the region), sends the timed pan
route through the pad file, records a 200-second 480x200 capture plus full
frames on trigger (`burst3.ps1`), and kills its own PID. Legs run fullscreen at
the display's resolution and internal scale 3 (the user's configuration). The
route is time-indexed; check that arms kept step (both did tonight, 290 s).

**Readers**: `runscore2.py` (big flashes: abrupt violet share >= 0.10 %,
counted as RUNS with a peak, not one-frame spikes), `tinyflash2.py` (small
events: the pinned rule, 8-connected, colour-filtered to the defect's own
chromaticity, raw AND steady-state after 40 % of the burst, largest event),
`legregime.py` (was the GPU saturated - the defect only occurs there),
`subjectcheck.py` (is the world on screen), `oraclefire.py` (did a picture
oracle change the picture), `rlread2.py` (instrument correlations with onsets,
with denominators and 2,000 control windows).

**Method rules, each with the day it was learned**

- Verify the arm before the burst: read a new instrument's own log line minutes
  in, while the run can still be saved (four dead instruments in one day; the
  ring-lag instrument read "0 of 0" for a whole leg).
- A single clean leg is no evidence: the defect arms once per session (one in
  four to one in six sessions clean by luck); four single cleans were believed
  and undone by their repeats in one day. Three gated legs, and prefer
  positive-outcome experiments (a chosen colour appearing) over absences.
- A dirty leg is decisive; a clean one is a candidate. Count the failure
  before explaining it; give the instrument its own control arm (the probe
  that cured what it measured); a discredited instrument reopens what it killed.
- Pre-register the reading before the data exists, both branches, and write the
  denominator with every result (records examined, frames, controls). Two
  independently written readers agreeing to the decimal is the standard for
  anything that gates a release (a 4-connected/8-connected mismatch was 7-18 %).
- A pooled rate can sum two populations: split by magnitude and by colour, then
  check where in the burst each kind sits. The "quiet floor" of small events
  was arrival pop-in; filtered to the defect's colour the floor was literally
  zero over 40,000 frames and the verdict on an earlier treatment reversed.
- A picture must respond to its draws: an oracle that silences one layer while
  another layer keeps the same pixels on screen changes nothing (every impostor
  is drawn by two passes over identical vertex ranges).
- A mechanism must be capable of the symptom (a table of neutral greys cannot
  make magenta); a mechanism that "arrives on cue" must also predict the healthy
  case; a value from one population is a hypothesis about another; measure the
  noise floor first (n >= 3, quote the spread).
- Compare like with like: same route, same duration, same instrument load (a
  lighter instrument is a weaker provocation; say so beside its zero). A user-
  driven run is an acceptance test reported in the user's words, never one of
  the gated legs - and in the one such run tonight the residue the fixed route
  never showed appeared, so the fixed route may be an easier case than play.
- A correction must reach the headline, not just the paragraph; consent is to a
  described state (when the description changes materially, re-ask - the user's
  "ready for release" was re-asked after five crashes came to light); never
  invent a user quote; a peer cannot stand in for the user.

---

## Part 6 - Cutting a release (and correcting one)

```
VERSION                  bump (semver by content)
CHANGELOG.md             a section for the version, dated, honest about what was measured and not
tools/make_release.py    KNOWN_ISSUES is ONE list rendered into README.txt AND RELEASE_NOTES.md
python tools/make_release.py [--build]     -> ../Releases/vX.Y.Z/ and fable2recomp-vX.Y.Z-win-amd64.zip
                                              (README.txt, RELEASE_NOTES.md, SHA256SUMS, provenance; refuses
                                              game data, a stale exe, a non-deployed SDK DLL pair)
git add -u; git commit; git tag -a vX.Y.Z; git push origin <branch>:main; git push origin vX.Y.Z
gh release create vX.Y.Z <zip> --title ... --notes-file <RELEASE_NOTES.md>
verify the asset from GitHub by size and sha256; the updater reads the tag and the zip name
python tools/make_release.py --update "D:\<Game> Portable"   (after backup_pre_<ver>)
```

The audit before publishing (the supervisor's checklist, kept): the version
string inside the exe; the shipped exe byte-identical to the one the
verification legs ran (pin the build hash BEFORE deploying, so a substitution
anywhere on the chain shows); every experiment cvar (`ngpu_exp_*`) at its
default 0 in the program's own run; a shipped default is injected into source
by a script, so read it back behaviourally from a log line, not from the
source; archive checksums; one new tag and no other moved; `rexglue-src` not
in the commit; the diff reviewed before the push. Nothing ships from the
experiment namespace: promote a setting to a considered name, a behaviour help
string and a deliberate default, keep the experiment cvar for legs.

The notes distinguish "this makes it stop and we cannot yet say why" from "we
fixed it", give costs at the scales measured (1x AND 3x; the mean hides frame
pacing - give p99 and hitches only when the two arms covered the same ground),
and state the limits of the measurement. A published note found overstated is
corrected IN PLACE with a dated line (the user's call), and the correction is
repeated in the next version's notes.

Release gotchas from all projects: a green build is not a delivered release
(assert on what a reader opens first); validate paths and notes before you
tag; `gh release create` wants `--notes-file` on PowerShell 5.1; a build step
can be non-deterministic (a 14-byte diff is the same code); grep the artifact
for the version after a bump.

---

## Part 7 - Debugging a rendering fault: the worked example

The one-frame violet tree-impostor flash (Fable II, 2026-09-14 to 09-29) is
the template for a defect that is intermittent, session-armed and GPU-side.

1. **Characterise before instrumenting**: it is the card's own impostor, same
   place and shape, one fixed violet colour with the texel's correct alpha
   (chromaticity constant to 3 % across 307 flashes; luminance varies 9x more
   than hue), only when the GPU cannot hold the rate (3x uncapped), armed per
   session.
2. **Read every input at the flash with positive controls**: the atlas content
   (resolve range and texture, mips 0-3), the lookup tables (host == guest), the
   float constants (CPU == GPU on 326,684 draws), the system constants and
   pipeline keys, the fetch through the draw's own descriptor (a compute probe,
   validated by an all-green fill). All correct at every onset; the fill proved
   the colour does not come from the atlas at all.
3. **Notice when the instrument is the treatment**: the sampling probe removed
   the big class in 7 of 7 legs while a no-dispatch control stayed dirty - a
   compute dispatch before each impostor draw "prevents arming"; useful, not a
   fix (its residue sat above the quiet legs until the metric was corrected).
4. **Kill hypotheses by pre-registered tests, not by argument**: the game-
   thread run-ahead race (kick-time hash against record-time hash: 0 of 161
   onsets), the per-mip cure, the extrapolated haze blend, the fresh zero-filled
   texture (the cache's own churn counters read zero), the tf17 ramp as the
   colour source (near-neutral). Each retired in minutes because the refutation
   was written before the data.
5. **Correct the metric when two populations hide in one rate** (Part 5).
6. **Ship an empirical cure honestly**: the kick pacing (the game's hand-over to
   the recorder waits for the queue) read zero flash-coloured events in three
   scripted legs and the user's own drive, free at 3x and at 1x; the cause of
   the flash is still not identified and the notes say so. Open: what the vertex
   stage of the impostor shader produced for that draw - the one object no
   instrument has read; the o3-only oracle (`ngpu_exp_ps_texel_zero=2`, which
   skips the far pass for masked species) is built and unrun.

Instruments built for it, all `ngpu_exp_*` and default off: atlas readback
bits A/T/V/K/S/P with `_file`, `_amask`, `_tmask`, `_pmask`, `_probe_mode`,
`_probe_lod`; `ngpu_exp_runlag` (kick age, watched-range hashes - whose
unsynchronised vector crashed five legs in one evening; now locked);
`ngpu_exp_kick_throttle`; `ngpu_exp_ps_texel_zero`. Readers in the session
scratchpad; the patches in `docs/native_gpu/*.patch`.

---

## Part 8 - Working with two sessions and a supervisor

The pattern that produced tonight's result: one session builds and runs, a
supervisor session (claudecode-76) audits every number from the artefacts, not
from reports, and both pre-register readings. Rules that held: the numbers of
record are the builder's, but nothing gates a release until two readers agree;
every retraction is sent within the hour and reaches the user's page headline;
a partial-sample rate is never quoted as a rate; "needs your call" is relayed
to the user, never answered by a peer; the user's mid-turn messages are the
authority and are quoted verbatim, never paraphrased; both sessions keep the
release word as the user's alone.

---

## Part 9 - The checklist for a new game

**Week 1 - boot**: SDK beside the project; extract the disc; manifest with
setjmp/longjmp and the codegen flags; overrides to a fixpoint; bulk function-
pointer scan; `RelWithDebInfo`; first boot with the pad file driving the menus;
health and boot loops; NG2_LESSONS table walked line by line and each row
marked applied / not needed / pending.

**Week 2 - play**: hooks for 60 fps, FOV, draw distance; the host layer ported
(setup screen, settings, saves, DLC, updater); video checked; crash dump wired;
a first public release with honest known issues (one list, two outputs).

**Week 3 - graphics**: the plugin's fixes from Canary; the lockstep census; the
native backend transplanted and proven by differential; textures dumped, packed
(address + content hash) and upscaled; ultrawide with the HUD and every solid
layer handled and every switch faded; performance measured with the frame
trace (fence waits versus CPU busy; worst frame and hitch count, not p99).

**Always**: one game at a time by process check at the moment of launch; legs
fullscreen at the user's resolution; three gated legs before a claim; the
denominator with every number; two readers before a release gate; the
supervisor's audit against the artefact; the user's word on publishing, re-asked
when the description changes; and the handover page current before the night
ends, with the withdrawn claims removed from its headline.
