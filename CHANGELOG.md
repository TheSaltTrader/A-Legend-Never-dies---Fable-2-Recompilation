# Changelog

All notable changes to fable2recomp. Versions follow the project's own
numbering, not the game's.

## 0.1.8 — 2026-09-13 (branch `tu1`)

### Removed - the field-of-view slider (0.1.6, 0.1.7): it never changed the camera

The slider wrote the game's 60-degree constant at guest 0x82101034, and the
write was real (it read back as 90 degrees live, and the log said so). But
the proof was wrong: with the value set before the save loads, frames taken
after the load at 60 and at 90 degrees show identical framing. The constant
is not what the gameplay camera projects with; the early screenshot
difference that seemed to show it was a scene change. Both commits are
reverted, the game folder is back to what 0.1.5 shipped, and the finding is
kept in docs/DRAW_DISTANCE.md for the next attempt (the camera's own
projection is derived elsewhere).

## 0.1.5 — 2026-09-13 (branch `tu1`)

### Removed - "Accurate depth", because it breaks the game

With exact float24 depth on, the plugin failed to create 52 graphics
pipelines in one session across six vertex shaders; the draws behind them
were skipped, which showed as black bars on the loading screen and a
shadow smear that followed the hero, with or without the texture pack.
The option is gone from both settings screens, the tuning sends false
whatever an older settings file says, and the field remains only so those
files still parse. Rule adopted with it: a setting that breaks the game is
not offered as a choice.

### Fixed - the settings screens get the mouse back; mouse buttons bind

With the keyboard driver on, the cursor stayed captured for the camera
while the F10 settings were open, so nothing there could be clicked: the
port's foreground-only input rule had replaced the framework's clause that
releases input to ImGui. Input is now held while a settings screen or the
bindings screen is open. The mouse buttons join the defaults, left for X
(attack) and right for Y (ranged), and the bindings screen captures LMB,
RMB and MMB like any key.

## 0.1.4 — 2026-09-13 (branch `tu1`)

### Added - a Keyboard bindings screen

"Remap keys..." beside the Keyboard-and-mouse switch opens a screen with
all 25 controller actions the runtime's keyboard driver knows and the
keys bound to each. Set replaces the binding with the next key pressed,
Add adds an alternative, Clear empties it, Defaults restores the port's
defaults; Shift, Ctrl or Alt held while pressing become prefixes, Escape
cancels. The capture reads the window's own key events above ImGui, so
what is written is the exact key name the driver compares against. A
binding applies the moment it is set (the driver reads its cvars on every
poll) and is saved as `keybind_<action>=` in the settings file; the
tuning emits every action from the player's binding or the default. The
bindings used to live only on the SDK's F4 page as raw strings.

## 0.1.3 — 2026-09-12 (branch `tu1`)

### Fixed - the texture pack matches by content, not by address

The first session with the finished pack (28,587 files) made zero
replacements, which is why F9 showed no change and no warming bar ever
appeared. A pack file is named by the texture's id plus its content hash,
and the id carries the texture's memory address; Fable II streams, so the
same texture lands at a different address on every visit (in the dump,
13,080 of 35,545 contents had been filed under two or more ids). The GPU
plugin now matches on the content hash plus the shape bits of the id and
ignores the address; file names, the dump and the upscaler are untouched,
and the stage lists name the file that was served so warming reads real
files. Shared with the NG2 port, which keeps its collision safety and
gains the same independence. `docs/TEXTURE_PACK.md`, "The pack is
content-addressed"; `patches/rexglue-texpack-content-addressed.patch`.

### Fixed - the blank screen after the logos (11.6 s of sleeping)

With the logos skipped, a blank frame stayed for about twelve seconds
before the title screen. With a warm disk cache the game still spent
exactly 400 ms per sound bank, 29 banks, doing nothing after a one-
millisecond read: its "Front end audio loading" thread was 99.6 percent
inside the game's own Sleep wrapper (sub_82CC8880, milliseconds in r3),
found by profiling every guest thread (`FABLE2_PROFILE=all` is new; the
default two threads never showed the loader). A hook at that wrapper's
entry turns a 400 ms sleep into 1 ms on any thread whose name says it
loads audio, and logs every other sleep of 100 ms or more once per
thread and duration so the next such wait names itself. The banks now
load in 0.1 s. `fable2_fast_bank_load`, on;
`FABLE2_TUNE=fable2_fast_bank_load=false` is the A/B.

### Verified - the warming lists are written again

Two scripted loads on the content-addressed plugin wrote `stages/ch04.txt`
(Bower Lake, 320 textures) and `ch06.txt` (Bowerstone Market); the first
visit to a region writes its list, every visit after that shows the blue
warming bar. The first content-addressed run also served 1,000 pack
textures at the lake where the address-keyed lookup had served none.

## 0.1.2 — 2026-09-12 (branch `tu1`)

### Fixed - vector register 96 is shared (the flashing foliage)

At Bower Lake the trees flashed between their colours and a flat
blue-violet from one frame to the next, caught in two screen grabs half a
second apart. The register census had flagged v96 as read before it is
written in a block of 24 SIMD functions on both the disc and the update
image, and the shared list had been left as the disc's because the disc
build never reached a forest. v96 is now in `shared_vector_registers`
(`fable2_manifest.toml`), so the callee sees what its caller put there.
The magenta impostor cards in the deep forest are the same class if they
go with it; the player's next forest visit decides.

### Added - CPU in the readouts, and a switch of its own for the logos

The readouts gain a CPU line: this process across all cores, with the
same figure in cores beside it (a game thread flat out on one core is 100
percent of a core and about 3 percent of a 32-thread machine), and a bar
under it; both switchable. The bar checkboxes moved to a second line,
because the settings cell clipped whatever sat past "VRAM" on the first.

"Skip publisher logos" is its own setting now, default on, so the
Microsoft and Lionhead videos can be kept on purpose; "Skip intro videos"
is back to what it always did, the synthetic A through a chapter's
cinematic.

### Fixed - eight fatal stubs the codegen had been emitting since the first TU1 build

Every build's codegen log carried `Unresolved b target` lines, and the
generated code carried `REX_FATAL("Unresolved call from ...")` at eight
branch sites in four functions - the game would have died the first time
any of them ran. The cause: the pointer scan's data channel had registered
22 entries pointed to from the C++ exception tables in `.rdata`
(0x8210AE00..0x8210B490). Those are catch funclets - blocks inside a
function that read the frame pointer the unwinder restores - and the
parent branches back into them on its normal path; registered as
functions they cut the parent in two. The continuation test now applies
to that channel as well, the 22 are gone, and the codegen log is clean.

### Changed - controller database shipped, play sessions log at info

`gamecontrollerdb.txt` (SDL's community mapping database, 869 Windows
pads) is copied beside the executable at build time; the runtime had
logged its absence at every launch and fallen back to SDL's built-in
mappings. `tools\run.cmd` launches at log level info: a play session at
debug wrote 25,000 debug lines an hour (APC deliveries, guest input,
user-context calls) for 12,000 of everything else, every one formatted
and flushed. Scripted runs keep debug; the crash record is critical either
way.

### Changed - one game at a time

`tools/play_probe.py` refuses to start while any Fable II process exists
and names the pid. A scripted check launched beside the player's own
session put a second window on their screen; they played in it, and the
probe's own stop at the end of its run looked exactly like a crash.

### Measured - where the CPU goes, and a hook that did not help

A profiled load of Bowerstone Market (`FABLE2_PROFILE=1`) says the port is
not CPU-bound: the game thread runs guest code 23 percent of the time and
yields the rest waiting for the frame, and the render thread spends 60
percent inside one function, the game's own "is the GPU still making
progress" poll, whose only pause is eight no-ops. That is the console's
design: the thread spins while the GPU finishes the frame. A hook that
yields the core in that loop (`fable2_gpu_wait_yield`, patch_hooks.cpp)
was built and measured against the same run with it off: 57 to 59 fps
either way, and the thread stayed on the CPU because a yield with nothing
else ready returns at once. It ships off, as a documented negative;
`FABLE2_TUNE=fable2_gpu_wait_yield=true` turns it on for an experiment.
The lever that moves frame rate remains the GPU: supersampling - and the
plugin's own warning, read with its numbers this time, says what that
buys: `swap source is unscaled (1280x720)`. The game presents from a
1280x720 texture it resolves itself, so the supersampled frame is folded
back to 720p before the window sees it; the extra scale is antialiasing
(and sharper shadow maps), not output resolution. 2x is the sweet spot on
this card; 3x paid nine times the pixels for the same 720p output.

Memory over a 21-minute idle session: private bytes 4990 to 4996 MB,
handles 1406 to 1394, threads 71 to 70, GPU memory 7.4 GB flat. No leak.

### Verified - saving works

A manual save from the game menu rewrote Hero000 (chaptersave, herosave,
mainsave, texturemorphs, publisher info). The earlier sessions had simply
never saved.

## 0.1.1 — 2026-09-12 (branch `tu1`)

### Fixed - a crash in play, and the class it belongs to

Fifteen minutes into the first play-test of the update build the game
died with `Call to invalid or unregistered function at 0x82DE2BA8`: the
fourth of ten callbacks a builder at 0x82DE2D48 puts into a table, all
of which the analyzer had absorbed into the function before them, and
all of which the pointer scan's "site and target in different functions"
rule had thrown away. The ten are registered, and `tools/scan_fnptrs.py`
now finds that class on its own: a label is recognised as the destination
of a direct branch rather than by who owns it, the `lis` window is 24
instructions, and three shape tests (a block that reads a non-volatile
register or the caller's frame before writing it; a run of `li; b` case
pairs; an address that feeds a `bctr` at its site) keep the wider net from
registering the middle of a function. The rewritten scan also dropped one
entry the first pass had registered, 0x82451E90, a continuation that reads
the frame pointer in its first instruction. README, "What a crash in play
taught channel 2".

### Changed - the logo videos are skipped, not pressed through

"Skip intro videos" now starts the game without the Microsoft and
Lionhead logos. They never answered to a button (the boot timeline was
17.1 s with and without the synthetic presses), and a video file that
fails to open stops the game, so neither the pad nor the files were the
way in. The game keeps its boot movies in a list and plays until the end
marker; a hook at 0x822F4EAC makes the first entry read as the end marker
and the game takes the path it already has for an empty list. The boot-time
arm of the synthetic controller is gone with them: with the title screen
up by 15 s it sat inside the 25 s arm, and a synthetic A on the main menu
is "New Game" (seen in a scripted run). The arm for a chapter's cinematic
is unchanged.

### Changed - readouts at every launch, with bars

The on-screen readouts are shown at every launch; F8 hides them for the
session only and is no longer saved. GPU load and video memory each get a
bar under the number, each with its own checkbox. `FABLE2_HUD=1` still
forces them for a scripted run.

### Open - magenta tree impostors in the forest

In Bower Lake the distant trees draw as bright magenta cards. Not the
texture pack (it was off) and not a dumped texture (11,158 textures the
session dumped hold no solid magenta), so the colour is made on the GPU
side: the impostor cards are rendered at run time and read back, and two
readbacks in this port are throttled - the resolve readback ("Black
texture fix", `some`) and the shader memory-export readback (off for the
60 fps lock). The scripted runs cannot walk to the forest, so the test is
yours: `tools\impostor_test_memexport.cmd` and
`tools\impostor_test_resolvefull.cmd` launch the game with one of the two
turned up for that process (`FABLE2_TUNE=name=value;...` is the seam).
Whichever clears the trees names the fix, which will then be made
selective rather than left at the expensive setting.

### Measured - what a region load costs (pre-cache, step 1)

The plugin's persistent shader storage is live (`Translated 221 shaders
from the storage in 26 milliseconds` at boot, pipelines rebuilt from it in
the background), so pipelines seen once do not stall again. A scripted
load of Bowerstone Market was bucketed by 5 s (`out/hitch_census` in the
port record): see docs/TU1_PORT.md for the numbers that decide what the
per-region pre-cache should hold.

## 0.1.0 — 2026-09-12 (branch `tu1`)

### Changed - compiled from the disc's title update

This build is the game at version 0.0.1.26: the disc's executable with
title update 1 applied at codegen time (`assets/default.xexp`) and at run
time (`default.xexp` beside `default.xex` in the game folder, plus
`update\data\tu1_data.bnk`, mounted as `update:\`). Every address moved -
5166 of the image's 5656 pages differ - so the registered helper
functions, the setjmp/longjmp pair, the six hook sites (60 fps at two
sites now, 1280 wide, MSAA, tick rate, texture morph) and the tick-rate
displacement were re-derived; `docs/TU1_PORT.md` records each value, the
tool that produced it and the check that it is right. The disc build's
configuration is kept beside each file (`*_disc.*`) and the last disc
executable as `out/fable2_base_0.0.15.exe`.

A save made on a console loads: Hero000 (version 393219, refused by the
disc build) reaches Bowerstone Market and plays at 51-55 fps in a scripted
run, where the disc build with the number rewritten died in a recursive
object walk. The importer no longer rewrites the version on this build;
the number it writes follows the build (393219 here, 805699586 on the
disc build). A save the disc build imported with the number rewritten
(Hero001) is now the older format, and the game says so.

The two executables are not interchangeable with one game folder: the
update build needs `default.xexp` beside the executable it loads, the disc
build must not see it (the runtime applies any sibling patch it finds).

### Added - readouts on demand

`FABLE2_HUD=1` shows the on-screen readouts (fps game/host, GPU, VRAM)
for that process whatever the settings say, and saves nothing;
`tools/play_probe.py` sets it for every scripted run, so the frames of a
test carry the numbers however the player last left F8.

### Tools

`tools/relocate.py` finds a disc address on the patched image by matching
an instruction window with branch and address fields masked (it
reproduces Xenia Canary's two known TU1 sites from the disc's, which is
the check that it works); `tools/vector_census.py` reads the shared
vector registers off the generated code; `tools/xex_image.py` reads a
flat image dump (`FABLE2_IMAGE`) written by the app (`FABLE2_DUMP_IMAGE`)
so every tool built on it sees the patched code. Without `FABLE2_IMAGE`
the tools decode the disc and size update addresses out of disc bytes.

## 0.0.17 — 2026-09-12

### Added - the title update, on the setup screen

A "Title update" section reads the executable in the game folder and says
what it is (version 0.0.0.26, media ID 716F0A0D on this disc), that saves
made on a console need the disc's title update and a build compiled with
it, and whether a title update file at hand is that update: the patch names
the executable it was built against by the SHA-1 of its signature, and one
for another pressing of the disc does not apply. "Choose title update
file..." takes the disc's update as a LIVE package (unpacked when the game
starts, the way saves are imported) or as its `default.xexp`, and keeps it
under `titleupdate\` beside the executable, staged for the build that is
compiled with it. Nothing is applied to a build that was not compiled for
it: that would run the old code against patched data. A build compiled
with the patch (`assets/default.xexp` present at codegen time) says so in
the same section.

## 0.0.16 — 2026-09-12

### Fixed - pressing Y in the intro killed the game

An abort with `Call to invalid or unregistered function at guest address
0x82DDC4D8`: a 28-byte helper the analyzer had absorbed into the function
before it, reached through a function-pointer table when Y was pressed.
Registered in `config/functions.toml` with `tools/add_function.py`, as the
others were; the codegen stamp had to be deleted for the codegen to notice.

### Changed - importing Xbox 360 saves: once, into a free slot, version-adjusted

Every launch re-imported every package in the save folder into the slot
named like the package - always Hero000 - so the slot the player had been
saving into was overwritten at the next launch, and a second package
replaced the first. Now each package is imported once (a manifest beside the
slots remembers which package went where), into the package's own slot if it
is free and otherwise the first free one, and a package already imported is
left alone. The setup screen offers the same folder pick as the settings, so
a save can be in place before the first launch, and the settings row gained
a Browse button.

The version number is adjusted on import. A save from a console carries the
title update's number (393219) and this build's game refuses it as "created
with a more up-to-date version"; the importer rewrites it to this build's
(805699586), which gets the save past that screen. It does not get it
loaded: see below.

### Found - the console save loads only under the title update

With the number adjusted, the save's Bowerstone Market began to load and the
game died two seconds later in a recursive object walk, reading offset 0x10
of a null pointer (`sub_82BF5470`, four levels of `sub_82BF59A0` /
`sub_82BF4C50` above it) - a serialised index into a table this build does
not have an entry for. The game folder that shipped this port is the disc
(version 26); the console that made the save had the disc's one title
update. That update exists and applies: media ID 716F0A0D, base version
0x1A, "Fable II v1" on xboxunity, 3 MB (`default.xexp` + `data/tu1_data.bnk`),
and the runtime confirms `XEX patch applied successfully: base version
0.0.0.26, new version 0.0.1.26` when the patch sits beside `default.xex` in
the game folder - the recompiler loads the executable through the same
runtime, so it would compile the patched game the same way. The patched
image differs from the disc's in 5166 of 5656 pages, i.e. the whole
executable moves: every guest address this port carries (471 registered
functions, 24 hook sites, setjmp/longjmp, the shared vector registers) has
to be re-derived for it. The tools that derived them exist; the work is a
regeneration and a re-verification, not a re-port from nothing. The disc's
patch is kept under `assets/tu1/` (not in the repository). A title update
for another disc (media ID 04BF96A1, version 5 to 0x405) was tried and does
not apply here; the check is the SHA-1 of the executable's signature
against the patch's source digest.

### Added - the crash log names the recompiled functions

The crash filter and the abort handler now log the stack with recompiled
frames as `sub_<guest address>+offset`, through the codegen's own table, and
the fault address the same way. `FABLE2_TRACE_OPEN=<substring>` logs the
named stack of every guest file open whose path contains the text - which is
how the version-file reader (`sub_822F27C0`) was found after a static search
for the string found only asserts. `FABLE2_DUMP_IMAGE=<file>` writes the
guest image as the runtime has it, title update applied, for the analysis
tools that decode the .xex themselves.

### Changed - the plugin drops every texture when dumping is switched on

NG2's finding: dumping switched on mid-stage wrote only what loaded
afterwards, because the dump runs per texture load. NG2 made dumping
restart-required. The pack path already had the better answer in the same
block of the plugin - a change drops every texture at the end of the frame -
so the dump settings get the same treatment, and the scene in front of the
player is written out from where they stand. NG2's other v1.0.13 change,
matching pack textures by the bytes in memory at load ("resolve-at-load"),
is in the shared plugin and on by default; this port runs it, and it removes
the last reason a streamed texture would show at its original resolution.

### Added - an icon

Drawn by `tools/make_icon.py` (a guild-seal disc with a gold "II"; no game
assets), linked into the executable from `resources/fable2.rc` for Explorer
and the desktop, stamped on the window at creation for the title bar, the
taskbar and Alt-Tab, and an AppUserModelID set before any window so the
taskbar groups and pins it as itself.

## 0.0.15 — 2026-09-12

### Fixed - the frame rate: 17 to 45 fps in town, now a locked 60

Three causes, found in order with the in-process sampler during play, and
the last one was the frame.

1. **Memexport readback drained the GPU five times a frame.** The game
   exports data from shaders (memexport) about five times per frame. The
   SDK's default reads every export back to guest memory, and on this title
   every one of those readbacks waited for the entire GPU queue to finish
   first. Named fence-wait statistics (new, `[gpu] fence waits in 5 s`)
   showed 220 to 260 such drains every five seconds, 1.9 to 3.9 seconds of
   every 5 spent in them, and the frame rate followed that number exactly:
   38% waiting was 45 fps, 75% was 17. The SDK's double-buffered "fast"
   path never applied, because it falls back to the drain whenever the
   previous frame's copy is not complete yet, which is always. The port now
   sets `readback_memexport=false`: a locked 60.0 in the same places, nothing
   visibly wrong in play. If this game turns out to read exported data on the
   CPU somewhere, the right fix is a one-frame-late copy, never the drain.
2. **The game's wait packets were polled with millisecond sleeps.** The GPU
   command thread services the game's "wait until this value changes"
   packets, and with V-Sync on it slept a millisecond between polls; with
   V-Sync off it yielded, which is why V-Sync off ran the same scenes at
   60 to 73 (V-Sync off also raises the fake vblank to 1000 Hz, so that is not
   a setting to play with). The poll is now a yield for the first two
   milliseconds of any wait, whatever V-Sync says; longer waits still sleep.
   Plugin change; the vblank stays at the refresh rate.
3. **Texture dumping hashes and writes every new texture on the render
   thread.** It was on in the settings file. In camera turns and loads, when
   new textures arrive, its CRC was 9-17% of the GPU-command thread and the
   file creates were on the same thread. Turned off in the settings; it is a
   tool for making a pack, not a setting to leave on.

Also: the process asks Windows for the 0.5 ms timer and opts out of timer
coalescing (it already had the 0.5 ms, so this changed nothing here, but a
machine without another requester would sleep in 15.6 ms steps). The
sampler now names who asked for each wait two levels up
(`waiting in: A, B`), which is what made the fence wait readable.

What "like NG2" would have taken, since it was asked: nothing in the
recompiled code. Both game threads sit at 100% of a core, but they are the
game's own frame waits (a sleep-and-poll in `sub_82CBD098`, a spin in
`sub_82B9BF90`); the frame was the runtime stopping to wait for the GPU. The
GPU itself sat under 50% the whole time because the pipeline kept draining.

## 0.0.14 — 2026-09-12

### Fixed - 0.0.13's upload guard skipped every pack texture of an odd width

The fit check added in 0.0.13 demanded rows x pitch bytes of upload buffer.
D3D12 sizes the buffer as (rows - 1) x pitch plus one row of pixels - the
last row is not padded - so every replacement whose width is not a multiple
of 64 (432, 368, 288 wide, five in the first minute of play) was rejected and
logged as "does not fit" although it fit exactly. The bound is now what the
read writes. Plugin rebuilt and deployed.

### Fixed - a pack texture was re-read and re-uploaded whenever the game touched it

The cache re-loads a texture whenever the game writes its guest memory. For a
pack replacement that re-read the .tex file and re-uploaded it, 1.3 ms of
render-thread time each, although the resource already held exactly those
pixels. Measured in play: about five re-uploads a frame, 9300 in one session.
A pack texture is now uploaded once per resource; later loads return at
once. A new `[texpack] re-uploads in N s: ...` line names the ids uploaded
most often, every five seconds, so the next churn has a name.

### Answered - "no way to play at 60 fps locked?"

Measured tonight, standing still in Bowerstone Old Town after a scripted
New Game, GPU shared with another job at 55-60% load:

| supersampling | pack | game fps (`[swap]`) |
|---|---|---|
| 2x | off | 60.0 steady, p50 16.6 ms |
| 2x | on | 60.0 steady, p50 16.6 ms |
| 3x | on | 42-55, p50 17-24 ms, uneven |

Your session ran at 3x (the change to 2x you made mid-session takes effect
at the next launch), with the pack on and while moving: 38. At 2x it holds
60 with the pack on. Whether it holds while moving through a crowd was not
measured - the scripted pad cannot walk. The two guest threads that sit at
100% of a core are not the limit: the in-process profiler (below) shows the
GameThread in the game's own sleep-and-poll frame wait (`KeDelayExecutionThread`
from `sub_82CBD098`, 60-80% of its samples) and the 3D Engine thread in an
eight-instruction spin at `sub_82B9BF90` (85-91%) or waiting on a mutex -
both waiting for the frame, neither computing. So the frame at 3x is the GPU's,
and no codegen or compiler flag changes that. The remaining recompiled-code
cost is real but hidden behind the wait; the profiler will show it the day the
GPU is not the limit.

### Added - a sampling profiler for the guest threads

`FABLE2_PROFILE=1` (or a list of thread names) samples the named guest
threads a thousand times a second from inside the process - suspend, read
the context, unwind with the OS's tables, resume - and every ten seconds logs
where the on-CPU samples landed: by module, the hottest leaf addresses, and
the hottest recompiled functions, named `sub_XXXXXXXX` through the codegen's
own guest-to-host table, so no PDB is needed. Samples where the thread had
not run since the last look are counted as blocked, not attributed. Nothing
allocates, logs or locks while a thread is suspended. Why in-process: the
runtime raises first-chance guest access violations constantly, so anything
attached as a debugger changes what it measures - the F9 crash never
reproduced under cdb or procdump - and the recompiled functions have no
symbols an external profiler could show.

### Added - a scripted pad for test runs

`FABLE2_PAD_SCRIPT="autoskip:20,30:right,32:a,..."` presses buttons on the
intro skipper's synthetic pad at the given seconds after boot, read by the
game exactly like a controller. Keystrokes through the window depend on focus
and on what the guest polls at that instant, which cost a night of runs that
landed on the wrong screen. `autoskip:N` keeps the skipper's A/Start
hammering until N seconds and stops it after, because it otherwise presses
A on the main menu too. The New Game path needs a d-pad press on the
character cards before A; Continue cannot be scripted here because the
imported saves do not load ("created with a more up-to-date version" /
"corrupted") - the play so far has all been New Game.

### Noted

- The build is a shared source tree with the NG2 port. Tonight the NG2
  session edited the same plugin file and rebuilt it while this one was
  measuring; the Fable 2 copies (`RexBlue\win-amd64\bin` and the build
  folder) are the build from this session's edits alone. The exact edits are
  in `patches\scripts\` because a diff of the shared file is never one
  project's change.
- Two Edge processes were burning a core each in the kernel with no user
  time during the evening's measurements, 5.6 million system calls a second
  machine-wide. Not the game, but not nothing.

## 0.0.13 — 2026-09-11

### Fixed - F9 while textures were loading killed the game

Reported from play: F9 while textures were still loading, and the game was
gone. Reproduced, dumped and fixed. The root cause is in the GPU plugin's
pack code, and it is a race with the scene loader, not with F9's timing:

The plugin decided "pack or not" for a texture THREE times with three fresh
lookups - at creation (which sizes the D3D12 resource), at upload and at view
creation. F9 changes the pack path live and the plugin drops every texture at
the END of the frame, so a texture created before the press could be uploaded
after it, and the upload's lookup then answered differently from the
creation's. With the pack switched ON mid-load, the upload read a 4x
replacement's rows into an upload buffer sized for the game's own texture: a
heap overrun on the GPU thread. The minidump (`fable2-20260911-234810.dmp`)
shows it exactly - an access violation in `memcpy` under
`std::istream::read`, called from the plugin's texture upload, on the frame
after "switch 1 -> ON" - and the other deaths seen in the same stress, exit
code `0xC0000409` with no dump, are the fast-fail a corrupted heap ends in
when the overrun lands on a neighbour instead of an unmapped page.

The decision is now taken once, at creation, and stored on the texture; the
upload and the view read it from there (`D3D12Texture::SetTexpackReplacement`
in `rexglue-src`, plugin rebuilt and deployed). The upload also checks that
the resource fits the replacement before reading, and logs a skip rather than
overrunning if it ever does not. Measured with the stress seam
(`FABLE2_TEXPACK_STRESS=<seconds>`: settings menu opened, then 20 switches
300 ms apart): the old plugin died in 2 of 5 runs at the main menu and on the
first switch of a game load; the fixed one survived 4 of 4 at the menu plus
the load run. Under a debugger it never died at all - the race needs the
loader's timing.

Two smaller things were fixed on the way and are kept. The plugin read the
pack path by unlocked reference on the render thread while the app's thread
reassigned it (the registry's locked copy, `rex::cvar::Query<std::string>`,
is used at all seven sites now). And the settings menu's Textures table drew
rows into a table whose `BeginTable` had returned false - the first crash dump
of the session, `ImGui::TableNextRow` on a null table - so every `BeginTable`,
`Begin` and `BeginChild` result in the menu is honoured, and
`tools/sweep_rowstart.py` flags an ignored one. On the app side F9 refuses a
second switch within three seconds of the first, and any switch while a
region's texture cache is warming, and says so in the log.

### Added - crash dumps, and a stack for aborts

`crashdumps/fable2-<timestamp>.dmp` next to the executable on any unhandled
exception, with a `CRASH:` line in the log naming it; and since a fast-fail
abort (`std::terminate`, a failed assert, a CRT invalid parameter) bypasses
that filter entirely, the process now also logs `ABORT:` with the aborting
thread's stack, the C++ exception in flight if any, and writes a dump from the
abort handler. The user-visible exit code alone was what made the
`0xC0000409` deaths above impossible to read until then.

### Changed - the FPS readout shows the game's frame rate

The F8 counter measured host presents: 170-200 a second on this machine,
whatever the game did. The plugin now publishes the game's own rate from its
swaps (`guest_fps_x10`, once a second) and the counter shows that, with the
host rate beside it, smaller. "FPS 60.0  host 202" reads as it should; the
old "FPS 202" over a game running at 30 was the "high fps but laggy" report
in one number.

### Added - frame-time statistics in the log

"High fps but laggy" cannot be judged from an average. Every five seconds
the log now carries `[perf] N fps  frame ms: p50 / p99 / worst / hitches`
(a hitch is a frame more than twice the median), measured per presented
frame with a steady clock. Ported from NG2's diagnostics.

### Noted - the lag, and what to try first

What the log measured this session: the game itself holds 60 at the main menu
(`[swap] 60.0 guest fps, p50 16.6 ms`) and drops to a locked 30 in stretches
(`p50 33.4 ms, p99 35.9`), while the host presents 170-200 frames a second
throughout - which is the number the old counter showed. A locked 30 with the
60 fps patch on is the game halving its rate because a frame took longer than
16 ms, and on this configuration the frame is expensive: Supersampling 3x
(1280x720 rendered at 3840x2160), FSR, FXAA extreme, the 4x texture pack, and
- as the settings file stands - **texture dumping ON**, which writes every
texture it first sees to disk from the render thread. The GPU was also
carrying 12.5 GB and 56% load from other work with the game closed. So, in
order: turn texture dumping off unless a dump is wanted; Supersampling 2x;
V-Sync off (applies live; the 144 Hz panel's variable refresh takes over);
then Frame rate 144. The new counter and the `[swap]` line say whether the
game holds 60 after each change. Separately, each texture the pack replaces
costs the render thread about 4 ms the first time a region is visited (2.6 ms
of it the file read); the per-region warming from 0.0.11 is what removes that
on later visits, and this session confirmed the region signal fires.

## 0.0.12 — 2026-09-11

### Fixed - quitting with Escape left the process running

Escape saved the settings, logged "Escape: quitting", and then nothing: the
window stayed, and the process was still alive thirty seconds later. The key
asked the runtime to quit gracefully, and on this runtime that path never
comes back - NG2's final notes for the same day say why, and a test seam here
(`FABLE2_QUIT_AFTER=<seconds>` fires Escape's code from a timer) measured it
on this title rather than assuming it carried over. The close button never
showed the problem because the SDK's close path terminates the title and
hard-exits, in 0.2 seconds in every log this project has.

Escape now releases what the app owns - settings written, a running texture
build stopped - and asks the window to close, which is exactly the close
button's path. A three-second watchdog sits behind it in case the request is
ever swallowed.

## 0.0.11 — 2026-09-11

Everything the NG2 port learned between 5 and 11 September, brought across.
Each item names where it came from; NG2's own changelog (v1.0.0 to v1.0.6)
carries the full story of each.

### Fixed - the whole picture stretched on any window that was not 16:9

Pick an ultrawide size and the game filled it edge to edge - HUD, menus and
all - with "Keep aspect ratio" on and doing nothing. Measured on the 6th at a
2400x1000 window: the character-select cards stretched wall to wall.

Two things were wrong. The guest was told the display was the window's size,
and the runtime treats a video mode equal to its default of 1280x720 as "not
configured" and substitutes the window size for it - so at any non-16:9
window the guest reported the window's own shape, the 16:9 frame "matched" it,
and the presenter, which pillarboxes only when the two differ, never added the
bars. The guest is now told the largest 16:9 box that fits the window, the
runtime's new `video_mode_explicit` option makes it take that value as set,
and the separate video-size settings that could be pointed at the window's
shape are gone. Verified: the same 2400x1000 window now shows the picture
pillarboxed, and the log reads `guest display 1776x1000`.

### Fixed - the spin-wait hint was translated to nothing

`db16cyc` is Xenon's spin-wait hint. The codegen tool this project was
generating with predated NG2's fix and emitted nothing for it, in six of the
recompiled files here. Rebuilt the tool from the shared SDK source, forced a
regeneration (codegen's stamp does not depend on the tool, so it reported
"554 unchanged" until the stamp was deleted), and the six now emit the
`PAUSE`-plus-yield the runtime provides. A correctness fix, not a visible one.

### Fixed - the cursor never hid

"Hide the pointer after" set a delay and left the window's cursor mode at
"always visible", so the delay was dead configuration. The mode now follows
the delay. (NG2 v1.0.5.)

### Changed - the texture pipeline, in full (NG2 v1.0.1, v1.0.2, v1.0.5)

- **Cancel cancels.** The tool is a tree - the Python launcher, the
  interpreter, the upscaler - and stopping the first left the rest running.
  The tree now lives in a Windows job object; Cancel terminates it within a
  moment, and so does quitting the game.
- **The run belongs to the app, not the menu.** Closing the settings menu used
  to destroy the run with it. It now lives for the life of the process; the
  menu shows its progress whenever it is open.
- **Two steps, two bars.** Decoding every dump and upscaling the art are
  reported as "Step 1 of 2" and "Step 2 of 2", each timed by itself, instead
  of one bar reaching 100% and starting again with a wrong estimate.
- **Only what is missing.** "Process N waiting textures" leaves the pack's
  existing textures alone; "Redo textures already in the pack" rebuilds
  everything, and is forced when the pack's own record (`pack/pack.txt`) says
  it was made with other settings or stopped halfway.
- **Honest counts.** The menu classifies the dump the way the tool does and
  reports what can be enhanced, what is in the pack and what is waiting -
  HUD, fonts, normal maps and video frames are never packed and are no longer
  counted as missing. Counted off the UI thread, at most every five seconds:
  on NG2 the per-frame folder walk produced a 2.3 s frame and a driver reset.
- **The AI upscaler ran at the wrong scale.** Real-ESRGAN x4plus is a 4x
  network; asked for 2x it produced shifted, mostly black tiles. It now runs
  at its own 4x and the result is resized to the scale asked for.
- **Dumping and the pack are one or the other.** Together they put a file
  write and a read on the GPU thread for every texture and starve the command
  stream. The menu cannot select both, and a settings file that has both
  loads with the pack off. **A file that had both on before this release
  will come up with dumping on and the pack off.**
- **Every k_8_8_8_8 texture was being dropped from the pack.** The tool's two
  float-format codes were 6 and 7 - the values from the render-target enum,
  not the texture enum - so `pack_reason()` called every k_8_8_8_8 texture
  (code 6) a "float format" and skipped it. Now 31 and 32, from `xenos.h`.
  Found by porting the rule to C++ against the enum.

### Added - per-region texture warming (NG2 v1.0.0 "warmed per stage")

The plugin records which pack textures each stage uses and pre-reads them
when the stage returns, so a region's second visit does not stutter on cold
reads. NG2 learns its stage from the chapter file the game opens; Fable II
opens no per-level file, so the runtime gained a file-open observer
(`SetFileOpenObserver`, `patches/rexglue-file-open-observer.patch`) and the
app maps the per-region audio bank the game opens to a stable region number
(`src/fable2_stage.cpp`). A blue bar top-left shows the warming and input is
held until it is done. **Not yet observed in play**: whether the game opens
those banks per region is what the first region change with this build will
show - every open is logged at debug as `[stage] open #N`.

### Added - overlays that the settings already promised

The F8 readouts (FPS, GPU, video memory) had switches and a key and nothing
that drew them; the F9 pack toggle changed the picture with no indication of
which set was on screen. Both now draw (`src/fable2_texnotify.cpp`, from NG2
v0.5.4), and the window title carries the port's version.

### Added - one button for a bug report (NG2 v1.0.0)

"Copy diagnostics to a file", on both settings screens, gathers this
session's log, the settings and what the machine is into one text file under
`diagnostics\`, copies its path and opens the folder. `FABLE2_DIAGNOSTICS=1`
writes the same file during startup. It looks at `--log_file` first, so a
scripted run does not bundle an unrelated log.

### Added - a Lodestone census (NG2 v1.0.0)

`tools/lodestone_census.py` takes its subjects from the artefact: every field
in `fable2_settings.h`, every cvar that mirrors one, every emission in
`fable2_tuning.h`, every tool the C++ looks up. Each must be reachable,
documented in the README's new settings reference, round-trip through save
and load, and be delivered under its own condition - or be declared in
`tools/settings-ledger.json` with a reason. Green on first passing run after
the README reference was written. `tools/sweep_rowstart.py` is the companion
that catches a `RowStart` outside a table, which took NG2's process down.

### Also

- Input is held while we are not the foreground window (NG2), and the d-pad
  is on the plain arrow keys.
- The runtime pair is the SDK's 2026-09-11 build: stuck-wait watchdog (tag
  `[watchdog]`, was `[ng2]`), audio underrun credit, ring-buffer epoch,
  `video_mode_explicit`, content-hash pack, file-open observer. The installed
  SDK's import library and headers were refreshed to match; the previous pair
  is in `dll_backup_20260911_preobserver/`.
- README: the status table no longer says no character has ever rendered.

## 0.0.10 — 2026-09-11

### Texture pack: ids now carry a content hash (ported from NG2)

The GPU plugin's texture id is built from the texture's memory address, format,
size and pitch, so two different textures that the game streams through the
same memory share one id and the pack served whichever was dumped first - on
Ninja Gaiden II a shop window rendered as a violet normal map. Pack files are
now `<id>-<hash>.tex` with a CRC-32 of the raw guest bytes, the plugin hashes
guest memory before opening a file, a mismatch falls back to the original and
is logged once per id, and the dump keeps both textures of a shared address.
`tools/upscale_textures.py` migrates an existing dump and pack in place
(`fable2tex2`: 338 files renamed, nothing re-upscaled). The rebuilt
`rexgpu-xenos.dll` and its matching `rexruntime.dll` are deployed to the build
folder and to `../RexBlue/win-amd64/bin`; the previous pair is kept in
`dll_backup_20260911_prehash/`. See `docs/TEXTURE_PACK.md`.

Also: the plugin's dump cvars are now hot-reloadable and `ApplyLiveSettings`
pushes them, so ticking "Dump while playing" writes the current scene at once
instead of at the next launch (the settings screen's note said as much and
was right, until now). The app side of this needs a rebuild of fable2recomp
to take effect; the plugin side is already deployed.

## 0.0.9 — 2026-09-04

### Supersampling now goes to the runtime's real maximum

The cvar range is 1-8 (`draw_resolution_scale_x`); the menu offered 1-3 while
`Clamp()` allowed 1-8, so a config holding 4 displayed as "3x" and was written
back as 3. All eight are now offered, with the pixel count shown next to the
control from 4x up — the cost is the square of the number, and 8x at
this guest's 1280x720 is 10240x5760.

### Ported from Canary: extended-range float16

**The Xbox 360's float16 has no Inf and no NaN.** Exponent 31 holds finite
values, up to 131008, where IEEE binary16 reads Inf. Our fork predates Canary's
fix for this and still carried `TODO(Triang3l): Use extended range conversion.`
in three places and `Xenos extended-range float16.` in two more, converting HDR
render targets with the plain hardware instruction and clamping them to 65504.

Ported in full:

- `Float32ToF16ExtendedRange` / `Float16ExtendedRangeTo32` (DXBC, D3D12)
- `PackFloat16x2ExtendedRange` / `UnpackFloat16x2ExtendedRange` (SPIR-V, Vulkan)
- wired into the ROV colour pack and unpack on both backends, and into all six
  memexport float16 cases
- **then** the shared `GetPSIColorFormatInfo` clamp widened 65504 -> 131008

That order is load-bearing, and the port script enforces it: widening the clamp
before both backends can encode the range sends everything above 65504 into a
plain IEEE conversion, which turns it into Inf — worse than
clamping. The script refuses to widen unless it finds both encoders defined and
called.

**This did NOT fix the flat-blue scene.** A 400 s reproduction run after the
port still scores `BLUE` — 34 frames, peak blue 100%, 11 blue
frames. Characters are still missing from scenes that should have them. The port
is correct on its own merits and matches Canary, but it is not this bug.

### How to find Canary's fixes: read TODOs, not diffs

Function- and line-count deltas rank Canary's **performance** work above its
fixes. The largest apparent gap in `draw_util` is `GetScissorTmpl`, 59 lines we
"lack", which is our own `GetScissor` arithmetic rewritten in SSE4 with a scalar
fallback that matches ours line for line. There are 165 such functions.

`tools/canary_todos.py` compares TODOs instead. Both trees inherit the same
comments from the same author, so one still in our copy and gone from Canary's
marks work finished after the fork: 88 ours, 155 theirs, **11** in that state.
The float16 cluster was five of them, and after porting it the tool no longer
lists it — the check validates itself.

One trap, found by chasing a false lead: the trees are formatted to different
column limits, so a TODO that wraps differently looked like two different TODOs
and was reported resolved when Canary had merely re-wrapped it. The tool now
joins each comment block and compares a fixed prefix, and four candidates
disappeared. **A tool that reads comments reads formatting as well as meaning
— check a candidate against the code before porting it.** Canary
had also simply deleted some TODOs without implementing anything.

`tools/canary_survey.py` is the census half: every Canary GPU source file is
mapped to one of ours or explicitly ignored with a reason, so a file nobody has
compared reads as UNMAPPED rather than being silently skipped.

### Also

- The Canary checkout was a **shallow clone** — one commit, no
  history. `git fetch --unshallow` before any archaeology.
- `build_vulkan.cmd` anchors itself to its own directory.

## 0.0.8 — 2026-09-04

### Upscaling: FSR and CAS, which this runtime had all along

Fable II renders at 1120x720 (1280x720 with the community resolution patch), so
on any modern display the picture is **always** being upscaled. Until now the
only filter available was bilinear, and this menu said so in as many words.

That turned out to be wrong, and wrong in an instructive way. The FidelityFX
FSR 1.0 (EASU/RCAS) and CAS shaders are **already compiled and committed** to
the ReXGlue SDK tree, for both backends. They are gated behind
`REX_HAS_FIDELITYFX_SDK`, which the SDK only defines after
`REXGLUE_ENABLE_FIDELITYFX=ON` performs a full clone of the FidelityFX SDK and
builds `ffx-api` — even though the spatial shaders need
nothing whatsoever from it. One define was gating two unrelated features, and
the expensive one decided for both. Only the *temporal* `fsr2`/`fsr3` path
genuinely needs the library, and it is guarded separately by
`REX_HAS_FIDELITYFX_RUNTIME` with an arithmetic fallback at every call site.

Our SDK build now sets the define directly, via a new
`REXGLUE_FIDELITYFX_SPATIAL_ONLY` option: no fetch, no `ffx-api` target, no
extra DLL, no shader compiler. Measured on a live cvar dump, `present_effect`
went from `bilinear` to `bilinear cas fsr fsr2 fsr3`, and the registered cvar
count from 153 to 192.

- **New setting: Upscaling** — Bilinear (soft) / FSR 1.0
  (sharp) / CAS (sharpen only). Defaults to **FSR**, which is the right answer
  for a 720p guest on a 1080p or 4K window.
- **New setting: sharpness**, appearing only for the filter it belongs to. FSR's
  slider is inverted against the underlying cvar, which is a sharpness
  *reduction* in stops — so right is sharper, the way
  round a person expects.
- `fsr2`/`fsr3` are deliberately **not** offered. They are a temporal upscaler
  needing real depth and motion vectors; this runtime synthesizes them, warns
  that it does, and falls back to spatial FSR anyway. Listing them would be
  three names for one filter.

The menu reads the allowed values from the live runtime, so against a stock SDK
build the row correctly collapses to Bilinear alone rather than offering
something that would not work.

### Fixed: saving settings wiped the community patches

Found by watching a test run rewrite the settings file, not by reading the code.

`Save()` wrote 23 keys. `Apply()`, which loads them, understood 30. The seven in
the gap could be read from the file but were never written back to it:

    gpu_backend           the graphics engine selector
    readback              the black-texture fix
    patch_60fps           |
    patch_720p            |
    patch_disable_msaa    | every community patch
    patch_disable_texture_morph
    patch_high_tick_rate  |
    cursor_hide_seconds

So changing any setting in the menu rewrote the file *without* them, silently
resetting the player's patches, their black-texture fix and their graphics
engine to defaults. Nothing failed and nothing was logged — the
file was simply shorter afterwards.

Both lists are now checked against each other and agree at 31 keys each, with a
comment at `Save()` saying why that matters. A field added to one and not the
other is the whole bug.

### Keeping the SDK changes recoverable

Two features now depend on DLLs built from the SDK **source** clone, which is a
separate repository this project's history does not carry. Re-clone it and both
vanish silently — a stock DLL is a perfectly valid DLL
that simply lacks the thing.

- **`patches/`** — the SDK diff, the build script, and how
  to reapply both. The patch is verified to apply cleanly to a pristine tree.
- **`tools/stage_sdk.py`** — copies our DLLs over the
  stock ones. `rexglue_setup_target` re-stages the installed SDK's DLLs on
  *every* app build, so this is the step **after** building, never before —
  a trap that has cost this project twice. `--check` reports whether what sits
  beside `fable2.exe` is ours or stock by reading marker strings out of the
  binaries, rather than trusting that a copy ran.

### Also

- The upstream bug report gains a section on the FidelityFX packaging defect,
  alongside the existing one on Vulkan not being built on Windows. Both are the
  same class of problem: a working feature switched off in the shipped package.
- `build_vulkan.cmd` now anchors itself to its own directory. It relied on the
  caller's working directory, and PowerShell's `Set-Location` does not change
  what a child process inherits.

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
  select → the opening cinematic (a PRE-RENDERED video, not the engine - this
  entry originally claimed otherwise) → **Old Bowerstone**, with snow,
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
  select → the opening cinematic (a PRE-RENDERED video, not the engine - this
  entry originally claimed otherwise) → **Old Bowerstone**, with snow,
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
