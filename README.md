# fable2recomp

Static recompilation of **Fable II (Game of the Year Edition)** from Xbox 360
to native PC, on the **ReXGlue SDK 0.10.0** — the same toolchain used by
re:Blue (Blue Dragon) and by this machine's `ng2recomp` (Ninja Gaiden II).

The user owns the disc. Nothing here is redistributable: the build embeds the
game's own translated code, and `assets/`, `game/` and every disc file are
gitignored.

---

## Status

| | |
|---|---|
| Disc parsed | ✅ XGD2, 451 files, 6.5 GB |
| XEX decrypted and analysed | ✅ |
| `rexglue codegen` | ✅ **zero analysis errors** |
| `setjmp` / `longjmp` located | ✅ `0x83000200` / `0x82CA9260` |
| Native build | ✅ `fable2.exe`, 78 MB, ~3 min |
| Runs | ✅ **boots → menus → character select → intro video → Old Bowerstone**, and since 2026-09-05 **characters render** (Sparrow, Rose, the player in third person) - the `shared_vector_registers` fix, see the changelog |
| Intro videos | ✅ Bink decodes correctly, no artifacts |
| Input | ✅ keyboard-to-controller (`--mnk_mode=true`) drives the menus |
| Freeze at ~3.5 min | ✅ **fixed** - it was the RTV render-target path; ROV runs 8 min clean |

Reached on 2026-09-03 (first day) and 2026-09-04. The game boots through its
Bink logo videos, shows the title screen, accepts input, opens the main menu,
gets through New Game character select, plays the **opening cinematic** (which is a PRE-RENDERED video, not the engine)
(the sparrow on the pillar, with depth of field and real-time lighting) and
arrives in **Old Bowerstone with the tutorial hint up** — snow, brazier fire,
particles, the lot. A full 300-second run logs **zero fatals**.

For scale: `ng2recomp` took two days to render a start menu, and its intro video
is still wrong.

Screenshots in `out/shots/`.

### The freeze, and what fixed it

The picture used to stop updating about 3m25s in while the game carried on
running. It was **not** a hang: the presence heartbeat (`XGIUserSetContextEx`)
kept ticking once a second and ~510 APCs/second kept completing, steadily,
forever. Nothing crashed, nothing was unregistered, and the only failed file
opens were language packs we do not ship. What stopped was **GPU submission** -
zero `[gpu]` activity, permanently.

The cause was the **render-target path**. The runtime was defaulting to RTV -
the shader cache file is literally named `4D5307F1.rtv.d3d12.xpso`. Measured,
two runs, identical 290 s input schedule, shader cache cleared before each so
no pipeline could carry over, one variable changed:

| `render_target_path_d3d12` | last picture change | changed | identical |
|---|---|---|---|
| **`rov`** | 289s of 290s | 47 | **1** |
| `rtv` | 253s of 290s | 38 | **10** |

Confirmed over a longer run with `rov` as the default: **473s of 480s, 59
frames changed, 0 identical** - eight minutes with no freeze at all, against a
failure that used to arrive at three and a half.

`ng2recomp` reached the same conclusion for its own title (3128 EDRAM resolves
on ROV against 22 on RTV) but that was never evidence for this one. This is.

Measured with `tools/play_probe.py --freeze-report`, which compares **raw frame
buffers**: a frozen picture repeats byte for byte. Two earlier attempts at this
measurement were wrong and are worth recording:

1. Counting `[gpu]` log lines. Those only exist at `debug`, and at debug the
   ~510 APC lines a second rotate the transition out of the log entirely.
2. Running the A/B **without driving any input**, so neither arm ever left the
   title screen and reached the state that freezes. Both "passed" 240s, which
   proved nothing at all.

### Still open

- The hero's and the dog's textures can still go black once the hero grows up.
  That is Fable II's own well-known emulation bug; set **Black texture fix** to
  `some` in Graphics (`readback_resolve`).
- Clear the shader cache after a build change - `tools/clear_cache.py --yes`,
  or the button on the setup screen. A cache built by an older build is a known
  cause of that texture bug lingering, and it never touches saves or DLC.

## The game

Measured from the disc, not looked up:

| | |
|---|---|
| Title ID | `4D5307F1` |
| Media ID | `716F0A0D` |
| Version | `0.0.0.26` (base build — **not** TU1) |
| Build date | 2009-06-09 18:43:08 UTC |
| Image base | `0x82000000` |
| Entry point | `0x82CBB970` |
| `.text` | 18,101,444 B = **4,525,361 instructions** |
| `.pdata` functions | **46,072** |
| Imports | `xam.xex` (94) + `xboxkrnl.exe` (173) — **no guest DLLs** |
| Instruction mix | 4.8 % float, 1.5 % VMX128 |
| Disc | 1 of 1 |

Sections worth knowing about:

```
.rdata   0x82000600    1,090,376
.pdata   0x8210AA00      368,576
BINKBSS  0x82164A00       10,504
.text    0x82170000   18,101,444
BINK     0x832B3600       62,524
.data    0x832D0000    2,274,796
.lhmem   0x834FB600           12      <- Lionhead
.XBMOVIE 0x834FB800           12
.tls     0x834FBA00           25
.lhtrc   0x834FBC00            8      <- Lionhead trace
BINKDATA 0x834FBE00       15,688
.edata   0x83500000        7,453      <- NOT exports; an XDBF/SPA achievement blob
.idata   0x83510000        1,186
.XBLD    0x83520000          192
.reloc   0x83520200    1,030,968
```

**The `BINK*` sections are the good news.** Fable II plays its 20 videos
through RAD's Bink, statically linked and decoded on the CPU. Ninja Gaiden II
instead linked Microsoft's XMEDIA WMV decoder, whose *GPU* DXVA path lives
inside the GPU plugin and is still producing colour artifacts there. A CPU
decoder is just more PowerPC to translate, so Fable II's video path should come
out of the recompiler working. This has not been tested yet.

`.edata` is not a PE export directory. It starts `XDBF`, then `XACH`
(achievements) and `XCXT` — it is the SPA resource. `rexglue init achievements`
can extract it.

## Relationship to the upstream Fable2Recomp project

`../reference-Fable2Recomp` is a clone of
[github.com/Fable2Recomp/Fable2Recomp](https://github.com/Fable2Recomp/Fable2Recomp),
kept for reference. It is the same idea on the same SDK, and its
`Fable2_config.toml` carries **923 hand-found function overrides**.

**None of those addresses apply here, and this was checked rather than
assumed.** That repo targets *GOTY TU1*; this disc is the base GOTY build
`0.0.0.26`. Against our image:

- their `setjmp_address = 0x83006C90` lands mid-instruction inside a VMX128
  block, and `longjmp_address = 0x82CAFA30` decodes as `beq +0x14`;
- only 1 of their 8 `parent =` addresses is a `.pdata` function start here;
- **164 of their 923 overrides land in our `BINK` section**, i.e. past the end
  of our `.text` entirely — their build's code segment is simply longer;
- of 56 sampled 8-byte overrides (which should all be `addi rX,rX,N; b target`
  adjustor thunks), exactly 1 has that shape here.

So the two projects share an approach, not a coordinate system. Adopting their
config would have silently mis-registered hundreds of function boundaries.

It also turns out not to matter much: under SDK 0.10.0 this XEX needed **12**
function overrides to make codegen validate, and 118 in total once the runtime
had its say - not 923 (see below).

## Is the DLC already on the disc? Yes.

Both expansions ship on the Game of the Year disc. This was checked, not
assumed, and it matters because the standalone packages are about a gigabyte
each:

- **`data/levels.bnk` contains their actual level data**, not just references:
  `Worlds\Albion\DLC2\{Colosseum,Past,Present,Future}` is *See the Future*,
  and `Worlds\Albion\MysteryIsland` - with storm/summer/winter heightfields
  and totems, which are Knothole Island's seasons mechanic - is
  *Knothole Island* under its internal name.
- **`data/scenarios.list`** registers all four `albion\dlc2\*` levels.
- **`data/miscellaneous/fasttravellist.txt`** has the Knothole Island travel
  entry (`TEXT_DLC1_REGION_KNOTHOLE_ISLAND`).
- `data/audio/` carries `atmos_dlc2_future` and
  `region_specific_dlc2_{past,present,future}`.
- **The executable itself has the expansions built in**:
  `KnotholeIslandSeasonManager`, `KnotholeIslandShop`,
  `KnotholeIslandWardrobeChanges`, the `QD010_KnotholeIsland` quest chain and
  the expansion achievement text are all in `.rdata`/`.reloc`.

So of the three packages in the `DLC/` folder here, two are redundant:

| package | size | on the disc? |
|---|---|---|
| Fable II - Knothole Island | 557 MB | **yes** |
| Fable II - See the Future | 507 MB | **yes** |
| Collectors' Edition Content | 12 KB | no - it holds one file, `Collectors Edition Content.txt` |

(Each of the two expansions is present twice in that folder, as copies that
differ only in six signature bytes.)

`tools/stfs_info.py` prints all of this for any CON/LIVE/PIRS package. All
three are licensee `FFFFFFFFFFFFFFFF` - unrestricted, not bound to a console or
profile - so there is no entitlement to fake. If content ever does read as
locked, `--license_mask=-1` is the lever; nothing here sets it, because the
build works without it.

## Installing content packages anyway

For anything that genuinely is not on the disc:

```
tools\install_dlc.cmd                       # defaults to ../DLC/4D5307F1/00000002
tools\install_dlc.cmd path	o\packages
```

or `--dlc_root <folder>` on any run. It is deliberately **not** wired into
`run.cmd`, so nothing extracts a gigabyte of redundant expansion by accident.

`src/fable2_dlc.cpp` reads each package's STFS header and then applies three
guards, each of which was verified by making it fire:

- **wrong title** - a package whose header says another title id is refused
  (tested by patching a header to `DEADBEEF`);
- **already installed** - matched against the runtime's own `ListContent`, so a
  second launch does not re-extract;
- **duplicate** - two copies of one package install once.

That third one had a real ordering bug: the already-installed check returned
*before* the display name was recorded, so a second copy of an
already-installed package sailed through and installed anyway. Caught by
putting two copies in a folder and looking, not by reading the code.

## Settings

Two surfaces over one settings file, ported from `ng2recomp` (see
`docs/NG2_LESSONS.md` for what carried over and what did not).

**A setup screen before the guest boots.** It runs from `OnFinalizePaths` - the
one hook where the window and the ImGui drawer are live but the runtime has not
been constructed - so the game path it returns is the one that actually gets
mounted. It opens on the first run, whenever Shift is held at launch, and
whenever the configured folder has gone missing (a moved drive should offer the
picker, not a fatal error). It can also extract an ISO to a folder, because the
runtime mounts a directory and rejects a file.

**F10 over the running game**, with the same settings. Anything the presenter
or the plugin re-reads per frame applies immediately; anything latched at
startup is shown disabled and marked `(restart)` rather than accepted and
silently ignored. **F4 is the SDK's own cvar browser** and is left alone - the
menu links to it, because it enumerates the registry and so cannot fall behind
the build.

### Settings reference

Every row the two screens offer, in the words they use. `tools/lodestone_census.py`
checks that this list and the settings header agree, so a setting cannot quietly
go undocumented.

**Keys while playing:** F10 opens these settings; F8 shows or hides the
on-screen readouts; F9 switches the texture pack on and off without opening a
menu; Escape quits and saves the settings; F4 is the runtime's own cvar browser.

**Frame rate.** With the 60 fps patch on, this port holds a locked 60 in town
at 2x supersampling on a 5090 (0.0.15). What made it 17 to 45 before was not
the game's code: the runtime drained the whole GPU queue for every shader
memory export, five times a frame, and slept in millisecond steps while
polling the game's own wait packets. Both are fixed in the plugin this port
ships. Two settings still cost frames if left on: texture dumping (a tool for
making a pack, it hashes and writes every new texture on the render thread)
and 3x supersampling. The F8 counter's first number is the game's own rate;
the log's `[swap]` line has the frame-time percentiles, and `[gpu] fence
waits` says who waited on the GPU and for how long.

Display
- **Fullscreen** - borderless fullscreen on the chosen monitor.
- **Monitor** - which display to open on, listed with each display's size.
- **Resolution** - the window size. The game renders 16:9 whatever the window
  is, and is told a 16:9 display of the window's height, so an ultrawide
  picture is pillarboxed with Keep aspect ratio on and stretched with it off.
- **Frame rate** - the refresh rate the guest is told. 60 is what the console
  ran; above 60 is untested on this title.
- **V-Sync** - caps presentation to the display.
- **Keep aspect ratio** - letterbox instead of stretching the image to the window.
  Ignored while Ultrawide is on.
- **Picture width** - 16:9 (the default) or Ultrawide, offered only on a display
  wider than 16:9. Ultrawide: the world is
  projected at the window's aspect (the field-of-view hook derives the
  horizontal angle from it) and the frame is shown edge to edge, so the 3D
  picture has correct proportions and a wider view. The title screen, the main
  menus and 2D screens such as the loading map keep 16:9 with bars: a frame
  is stretched only while a world camera was built behind it (its projection
  already made it right), everything else is letterboxed; the presenter
  follows the scene the cameras describe (loading map, title menus, world)
  and, in the world, whether the frame drew the 3D world at all: the pause
  menu and the shop (2D only) show in 16:9 with bars, a chest popup or a
  dialogue with the world behind it stays wide, and each switch falls on
  the game's own scene cut. The HUD and text drawn over the world are in
  the 16:9 frame and come out stretched with it. Live.
- **Hide the pointer after** - seconds of mouse stillness before the pointer
  hides; 0 keeps it visible.
- **Field of view** - how wide the camera sees, in degrees (vertical); the
  game runs at 60. Live: a hook in the game's one perspective builder scales
  the vertical angle every frame and re-derives the horizontal one, so the
  aspect ratio is untouched. Every world camera (far plane 1000 or more,
  16:9) is scaled by the same amount, so cutscenes and zoomed shots keep
  their framing; the title screen, the main menu, the loading map and the
  small camera the HUD and menu panels are seen through are left alone.
- **Draw distance** - how far away buildings, trees and props are still
  drawn, as a percentage of the game's own distances (10-400, 100 = as
  shipped). The distances are values in the game's `data\globals\globals.gdb`,
  read once at start-up; off 100 the port mirrors `data\globals` next to the
  executable (`shadow\globals\`: the scaled file plus hard links to the
  other files there) and serves that folder in place of the original
  through the runtime's file system. The game folder is never touched.
  Restart-bound, and more distance costs GPU time.
- **Keyboard and mouse** - drives the guest controller from the keyboard
  (Enter is Start, Space is A, WASD the left stick, arrows the d-pad).
  **Remap keys...** opens the Keyboard bindings screen: every controller
  action with the keys that press it; Set replaces with the next key you
  press, Add adds an alternative, Clear empties it, modifiers held while
  pressing become Shift+/Ctrl+/Alt+ prefixes. Applied live, saved as
  `keybind_<action>=` lines in the settings file.
- **Mouse look** / **Mouse sensitivity** - the mouse drives the right stick,
  and how far it deflects per unit of movement.

Audio
- **Mute** - silences the guest's audio.
- **Audio buffering** - how many frames of audio are queued ahead; fewer is
  less delay, more risk of crackling.

Enhancements
- **Quality preset** - sets supersampling, antialiasing and texture filtering
  together; changing any of them reads Custom.
- **Supersampling** - renders the game's framebuffer at a multiple of its size
  (1 to 8) and filters it back down; the cost is the square of the number.
- **Import Xbox 360 saves** - a folder of Fable II save packages (or one
  package) imported into the profile at the next launch.
- **Skip publisher logos** - starts without the Microsoft and Lionhead logo
  videos (17 seconds no button shortens): a hook makes the game's boot-movie
  list read as empty, and the game takes its own empty-list path. Off, the
  logos play as on the console.
- **Skip intro videos** - presses A through a chapter's cinematic with a
  synthetic controller; any genuine input disarms it.
- **On-screen readouts** - FPS, CPU (this process, across all cores, with
  the same figure in cores), GPU load and video memory in the corner, each
  switchable, with a bar under the CPU, GPU and VRAM numbers (each
  switchable too). Shown at every launch; F8 hides them for the session and
  is not remembered. FPS is the game's own frame rate (frames it finished), with
  the host's present rate beside it, smaller; the window repaints far more
  often than the game draws, and only the first number says whether the game
  is keeping up.
- **Fuzzy alpha test** - the plugin's approximate alpha test, its fix for
  alpha flicker on NVIDIA cards.
- **Texture cache** - host memory the GPU may hold textures in; larger means
  fewer evictions while streaming, not a sharper picture.
- **NaN constant repair** - a diagnostic that substitutes zero or an identity
  row for NaN in the vertex shader constants. Off, because the bug it worked
  around is fixed and left on it turns the scene black.
- **Upscaling** - the presenter's output filter: bilinear, FSR 1.0 or CAS.
- **FSR sharpness** / **CAS sharpness** - the sharpening of the filter chosen
  above; each is only sent while its filter is selected.
- **Antialiasing** - none, FXAA or FXAA extreme, the post-process the plugin
  applies to the swap image.
- **Anisotropic filtering** - leave the game's own samplers alone, or force a level.
- **Graphics engine** - DirectX 12 or Vulkan; Vulkan needs a plugin built with it.
- **Black texture fix** - the graduated readback (none / fast / some / full;
  full waits for the whole GPU on every resolve and halves the frame rate at
  the lake - a diagnostic, not a setting to play with)
  for the hero and dog turning black at adulthood; `some` is the fix: every
  render-to-texture result is copied back exactly once its GPU work is done,
  without waiting for it.
- **Dither the output** - dither the 10 bpc output down to 8 bpc.

Textures
- **Folder** - where dumped and upscaled textures are kept.
- **Dump while playing** - writes every texture the game loads, for the pack
  tool. Dumping and the pack are one or the other, never both.
- **Use the upscaled textures** - loads the finished pack instead of the game's
  own textures. F9 switches it during play without changing this setting.
- **Upscale factor** - 2x, 4x or 8x; 2x is the measured recommendation.
- **Method** - Lanczos (a plain resize) or Real-ESRGAN AI.
- **Detail strength** - how much of the model's fine detail is laid over the
  original; tone and colour always stay the game's.
- **Process N waiting textures** - decodes and upscales only what is not in the
  pack yet; **Redo textures already in the pack** rebuilds everything, and is
  forced when the pack's own record of its settings differs from the ones chosen.
- **Live cost** - CPU, GPU and video memory bars beside the switches that cause
  the cost, switchable with the on-screen readouts' menu-bars option.

Community patches (Xenia Canary's patch file for this title; all off by default)
- **60 fps**, **Render at 1280 wide**, **Disable MSAA**, **30 Hz tick rate**,
  **Disable texture morphing** - see the section below for what each does and
  how each address was verified against this disc.

Content and diagnostics
- The **game folder** is chosen on the **setup screen** (hold Shift at launch
  to get it back); the ISO installer lives there too.
- **Copy diagnostics to a file** - this session's log, the settings and what
  the machine is, in one text file under `diagnostics\` beside the game, with
  its path on the clipboard. `FABLE2_DIAGNOSTICS=1` writes the same file during
  startup, for a launch that never reaches a menu.
- **Xbox 360 saves and the title update** (setup screen and settings) - a
  folder of console save packages is imported at launch, each once, into a
  free save slot, with the save's version number adjusted to this build's.
  Saves made on a console still need the game at the console's version: the
  setup screen's "Title update" section reads the executable's version and
  media ID, names the update this pressing takes, checks a chosen update
  file against the executable, and stages it for a build compiled with it.
  This build is compiled from the disc; see the 0.0.16 changelog for what
  loading such a save does without the update.
- **Profiling and scripted runs** (environment variables, for development):
  `FABLE2_PROFILE=1` samples the game's own threads from inside the process
  and logs, every ten seconds, the hottest recompiled functions by name;
  `FABLE2_PAD_SCRIPT="autoskip:20,30:right,32:a"` presses buttons on a
  synthetic controller at the given seconds after boot; `FABLE2_TUNE="name=value;..."`
  overrides tuning entries for one process (the A/B seam;
  `tools\impostor_test_*.cmd` use it); `FABLE2_HUD=1` shows
  the on-screen readouts for that process whatever the settings say (and
  saves nothing), so a test run's frames carry the numbers;
  `FABLE2_TEXPACK_STRESS` and `FABLE2_QUIT_AFTER` are the crash and quit
  reproductions. See the changelog for 0.0.13 and 0.0.14.

### What actually renders, and what never has

Stated plainly because this project got it wrong for a long time, in the README
and the changelog both:

- **Static world geometry renders.** Old Bowerstone's architecture, snow, fire,
  particles, water - all of it, and it looks right.
- **No character rendered in-engine on the disc build.** Not NPCs, not the
  hero, not the dog, in any build up to 0.0.17, all of which played the
  opening from New Game. **On the title-update build (0.1.0, branch `tu1`)
  loading a console save, they do:** the hero and the townspeople of
  Bowerstone Market are drawn, with their quest markers (`out/shots/tu1hud_05.png`,
  2026-09-12). Whether the opening renders its characters on that build is
  untested.
- **The opening "cinematic" is a pre-rendered video**, not the engine. Frames
  from it show a street full of people, a cart, a bird - and reading those as
  engine output is exactly the mistake that kept this hidden. A frame full of
  characters proves the Bink decoder works; it says nothing about the renderer.

So the signature is narrow and specific: **skinned/animated geometry never
draws, while static geometry does.** That is a much sharper thing to chase than
"characters are sometimes missing", and it is probably related to the flat-blue
scene rather than separate from it.

When judging a screenshot from this title, establish whether it is video or
engine BEFORE drawing any conclusion from it.

### Every row is a cvar this build actually registers

`FABLE2_DUMP_CVARS=<path>` writes all 192 registered cvars with their values,
defaults, allowed values and ranges. **Design the menu from that file, not from
the SDK headers** - guessing from headers is exactly how ng2recomp's menu ended
up offering FSR and CAS sharpening that its presenter does not implement.

What the dump settled here:

- `swap_post_effect` declares `none / fxaa / fxaa_extreme`. That is the whole
  of the antialiasing this runtime has.
- `present_effect` declares `bilinear / cas / fsr / fsr2 / fsr3`, so there is a
  real upscaling filter to choose. The menu offers the first three; see below
  for why not the last two.

### But a dump describes a BUILD, not a runtime

That `present_effect` line used to read `bilinear`, and nothing else, and this
README used to conclude from it that the runtime had no upscaling filter at
all. That conclusion was wrong, and the dump was not lying - it was faithfully
reporting a build in which FSR and CAS had been **compiled out**. The shaders
were sitting in the SDK tree the whole time, already built, behind a define
that is only set when an unrelated and much more expensive dependency is
fetched.

So the rule survives, with a second half:

> Design from the dump, never from the headers. But when the dump says a
> capability is *absent*, that is a fact about this build - go and find out
> whether it is switched off before recording it as a limitation.

The tell was cheap and was there to be found: a `REXGLUE_ENABLE_FIDELITYFX`
option in the SDK's `CMakeLists.txt`, defaulting `OFF`. The fix is
`patches/rexglue-fidelityfx-spatial-only.patch`; the reasoning is in
`bugreport/REXGLUE-BUG-fable2-blue-scene.md`.

`fsr2` and `fsr3` are declared but deliberately not offered: they are a
temporal upscaler needing real depth and motion vectors, which this runtime
synthesizes, warns about, and falls back to spatial FSR from anyway. Three
names for one filter is not a choice.

### Porting fixes from Xenia Canary

`rexgpu-xenos` is a fork of Canary, old enough to be missing 33 of its GPU
cvars. Canary plays this disc through the point where ours fails, so its
history is the obvious place to look for what we lack. The hard part is telling
a FIX from a REFACTOR.

**Function and line counts do not do it.** By that measure the biggest gap in
`draw_util` is `GetScissorTmpl`, 59 lines we "do not have" —
which turns out to be the same arithmetic as our `GetScissor`, written in SSE4,
with a scalar fallback in the same file that matches ours line for line. Canary
has done a lot of performance work, and it swamps the ranking.

**`tools/canary_todos.py` does.** Both trees inherit the same comments from the
same upstream author, so a `TODO` still in our copy and gone from Canary's marks
work finished after the SDK forked. Of 88 TODOs in our GPU tree, 11 are in that
state — a list small enough to read.

That is how the float16 defect was found. Our tree carried
`TODO(Triang3l): Use extended range conversion.` in three places and
`Xenos extended-range float16.` in two more. **The Xbox 360's float16 has no Inf
and no NaN: exponent 31 holds finite values, up to 131008**, where IEEE binary16
reads Inf. We were converting HDR render targets with the plain hardware
instruction and clamping them to 65504, so the top of every such surface was
being crushed. Canary implements the encoding on both backends. Now so do we.

**The order was load-bearing.** Widening the clamp is only safe once both
backends can encode the wider range; do it first and values above 65504 reach a
plain IEEE conversion and become Inf, which is worse than clamping. The port
script refuses to widen unless it finds both encoders defined and called.

`tools/canary_survey.py` is the census half: every Canary GPU source file is
either mapped to one of ours or explicitly listed as ignored with a reason, so a
file nobody has compared shows up as UNMAPPED instead of being quietly missed.

One caveat worth knowing, because it cost a false lead: the two trees are
formatted to different column limits, so a TODO that wraps differently used to
look like two different TODOs and got reported as resolved. The tool now joins
each comment block and compares a fixed-length prefix. **A tool that reads
comments is reading formatting as well as meaning — check a
candidate against the actual code before porting anything.**

### Getting a value into the GPU plugin

The plugin's cvars do not exist when the app starts - `rexgpu-xenos.dll`
registers them as it loads, after `OnPreSetup` and before `OnPostSetup`. So
setting them directly fails in the first hook and is *too late* in the second,
because the plugin latched the value at GPU init. On ng2recomp that is what
made internal resolution scaling look impossible.

`cvar::LoadConfig` is the one path that survives the gap: it defers values for
unregistered cvars and applies them at registration. `fable2_tuning.h` writes
`cache/fable2_tuning.toml` and loads it from `OnPreSetup`, and `OnPostSetup`
**reads the values back** - which is the only honest confirmation they arrived:

```
GPU: internal scale 2x2, swap_post_effect 'fxaa', vsync true
```

### A borrowed warning that turned out to be false here

ng2recomp warns that its game paces logic off the reported refresh, so above
60 Hz it runs faster rather than smoother, and V-Sync off speeds it up. That is
real for Ninja Gaiden II (it is in Xenia's compatibility entry for that title)
and it was tempting to copy.

Measured instead: a 30 Hz run and a 60 Hz run reach the same point in the boot
sequence at the same wall-clock second, with the refresh change confirmed in
the log rather than assumed. **Fable II does not appear to tie its pacing to
the reported refresh**, so the menu does not repeat the warning. The frame-rate
row says what was measured, and says plainly that above 60 is untested.

`Fable2Tuning::Fixed()` is empty for the same reason: ng2recomp ships several
compatibility flags there, and copying another title's flags because they are
sitting in a neighbouring project is how a working build gets broken.

### There is no DLC page

Deliberately. Knothole Island and See the Future are already on the GOTY disc,
so there is nothing to install - see above. For a package that genuinely is not
on the disc, `--dlc_root <folder>` or `tools/install_dlc.cmd` still work, and
the About section says so.

## Community patches

From [Xenia Canary's patch file for 4D5307F1](https://github.com/xenia-canary/game-patches)
(Margen67, Guy). Off by default; the settings menu turns them on.

Xenia applies them as guest **memory** patches, which cannot work here: the
values are immediates inside instructions, and those are already C++ constants
by the time anything could patch memory. So each is a **midasm hook** that
rewrites the register the immediate lands in - same effect, one comparison when
off. `config/hooks/patches.toml` carries the disassembly for every address.

| patch | what it does | verified as |
|---|---|---|
| 60 fps | frame divider 2 → 1 | `li r11, 2` in a 3/2/1 selector |
| 1280 wide | render width 1120 → 1280 | `li r11, 0x460` = 1120, as the patch's own note says |
| Disable MSAA | sample count 2 → 1 | `li r9, 2` in the same render setup |
| 30 Hz tick | 15 Hz → 30 Hz simulation | see below |
| Disable texture morphing | skips the morph path | `beq cr6` forced by zeroing the value the compare tests |

**Every address was checked against our own image before being used.** That is
not ceremony - the upstream Fable2Recomp config targets GOTY TU1 and 164 of its
923 entries land past the end of our `.text`, so "a Fable II patch file" is not
the same thing as "a patch file for this disc".

The proof this one *does* match is the tick-rate patch. It presets a `.data`
double, and at that address our image holds exactly **15.0**, which the patch
turns into exactly **30.0** - matching its description ("doubles tickrate to
30hz") to the bit. The store it NOPs, `stfd f0, -0x6af0(r8)`, targets precisely
that address. All four hooks then confirmed themselves at runtime against the
live values:

```
Patch: frame divider 2 -> 1
Patch: MSAA samples 2 -> 1
Patch: render width 1120 -> 1280
Patch: tick rate 15 Hz -> 30 Hz (guest 0x83319510)
```

### Two patches deliberately not shipped

**Unlock Collectors Edition Content** does not apply to this build. Its value
`0x39200001` (`li r9, 1`) lands at `0x824B366C`, which here is
`lwz r9, 0x10(r3)` immediately before `mtctr r9; bctrl` - so the patch would
make the game call address 1. The real CE package installs properly instead
(`--dlc_root`).

**Unlock Website Items** is coherent here (force the branch, and turn the
`li r3, 0` it lands on into `li r3, 1`) but is a content unlock rather than a
fix, so it is left out for now.

### The black-texture bug

Fable II's best-known emulation bug: the hero's and the dog's textures turn
black once the hero grows up. The proper fix is GPU readback, and this runtime
exposes it as a graduated `readback_resolve` (`none` / `fast` / `some` /
`full`) - `some` is what the [unofficial Xenia fork for this
game](https://github.com/just-harry/unofficial-xenia-femtofork-for-fable-ii)
hand-builds. It is a Graphics row in the settings menu. That fork also notes
that **stale cached shaders make the bug linger**, and this runtime does cache
them (in `Documents/fable2/cache/shaders`), so clear that when testing.

Guy's older "Disable Texture Morphing" patch is the cheap fallback; the fork
dropped it once it had real readback.

### Compatibility flag

`gpu_allow_invalid_fetch_constants = true` is in `Fable2Tuning::Fixed()`. The
Fable II guides are consistent that this title emits fetch constants the strict
path rejects, and that leaving it off drops textures - the visible symptom is
missing ground detail, "no grass". It is the only entry in that list, and it
has a citation, which is the bar for being there at all.

## Prerequisites

- The ReXGlue SDK. `tools/build.cmd` looks for `%REXSDK%`, falling back to
  `<project>/../RexBlue/win-amd64` (where this project keeps its own copy).
- LLVM/clang 20+, Ninja, CMake 3.25+, MSVC Build Tools (for the Windows SDK
  headers and libs that `clang-cl`-targeted clang++ needs — `vcvars64.bat`).
- Python 3.12 with `capstone` for the analysis tools.
- No vcpkg. SDL3, fmt, spdlog, utf8cpp and DXC all ship inside the SDK.

## Layout

```
fable2recomp/
  assets/default.xex          the XEX alone, what codegen reads
  game/                       the whole extracted disc, what the build runs against
  config/functions.toml       function-boundary overrides (hand-found, then a generated block)
  config/fnptr_exclude.txt   addresses scan_fnptrs.py must never register
  generated/default/          codegen output (500+ files, ~290 MB)
  src/                        the host application
  tools/                      analysis and build scripts
  fable2_manifest.toml        the codegen manifest
  out/build/win-amd64-*/      build output
```

## Building and running

```
tools\build.cmd Release            # or RelWithDebInfo when chasing a crash
tools\run.cmd
```

`build.cmd` runs `rexglue codegen` as its own step **before** CMake. This is
not tidiness: codegen rewrites `generated/default/fable2_pch.h`, and inside a
single ninja run the precompiled header can be built from the old copy before
codegen replaces it, after which every translation unit fails with *"file has
been modified since the precompiled header was built"*.

`Release` strips debug info from the ~500 recompiled TUs. Any other
configuration keeps line tables, and codegen emits **one source line per guest
instruction**, so a fault resolves to the exact PowerPC instruction that caused
it. That is the entire triage method — build `RelWithDebInfo` before debugging.

A bare `--flag` does **not** set a boolean cvar in this runtime; it is accepted
and silently ignored. Write `--fullscreen=true`.

## Tools

| tool | what it does |
|---|---|
| `extract_disc.py` | GDF (XGD1/2/3) extractor: `--list`, `--only`, or everything |
| `xex_image.py` | retail XEX2 decrypt + decompress to a flat guest image; caches `out/image.bin` |
| `xex_imports.py` | which kernel/xam ordinals the game imports, and who calls them |
| `hitch_census.py` | buckets a region load by 5 s: pipelines created, fence waits by reason, pack uploads, fps and hitches - what a stutter is made of |
| `impostor_test_memexport.cmd`, `impostor_test_resolvefull.cmd` | launch the game with one readback turned up for that process (`FABLE2_TUNE`), for the magenta tree impostors |
| `resolve_calls.py` | **run codegen, register every unresolved call, repeat to a fixpoint** |
| `add_function.py` | register one missed function, sizing it by walking to its terminator |
| `find_setjmp.py` | locate `_setjmp` / `longjmp` by shape, since the XEX is stripped |
| `scan_fnptrs.py` | **find missed functions in bulk by walking vtables**, instead of one crash per rebuild; `--check`, `--write`, `--prune` |
| `boot.py` | run the build for N seconds, stop it *by PID*, summarise the log |
| `boot_loop.py` | run → crash → register → rebuild, automated |
| `whereis.py` | what is at a guest address |
| `xrefs.py` | who reads/writes a guest range |
| `gstrings.py` | strings with their guest addresses |
| `scan_missed.py` | diagnostic only — see the warning below |
| `build.cmd`, `run.cmd` | build and launch |

`scan_missed.py` is kept only for diagnosis. **Scanning `.text` for things that
look like function prologues does not work**: `.text` contains pointer tables,
and a pointer beginning `0x82…` decodes as a plausible `lwz`. On Ninja Gaiden
II it produced ~1,340 candidates that were mostly false.

`scan_fnptrs.py` inverts that and is the tool to use — see below.

## Findings

### Codegen needed 12 function overrides to validate at all, found by iteration

The analyzer takes function bounds from `.pdata`, then fills the gaps. MSVC
emits no `.pdata` for small helpers with no prologue — `this`-adjusting virtual
thunks, one-line getters, forwarders — so a helper sitting in the padding after
a real function is absorbed into that function's body, and a `b` into it fails
validation with *"target not in any function"*.

The first codegen reported 8. Registering those exposed 3 more, and those
exposed 2 more: a registered forwarder's own `b` target becomes a call *from* a
real function, so this has to be iterated to a fixpoint rather than done once.
`tools/resolve_calls.py` does that automatically and stopped at **12**, with
codegen reporting zero errors. Three more came from actually running the game,
and 153 more from following function pointers (next section), for **168 in total**.

All 12 are the expected shape, e.g.

```
0x82C000F8  lwz r3, 0xc(r3)      # a forwarder to a member function
0x82C000FC  b   0x82c106a8
```

### Finding missed functions in bulk

The functions the analyzer misses are exactly the ones nothing calls directly:
they are reached through a **pointer**, which is why static analysis never sees
a call to them and why they surface at runtime one
`[FATAL] Call to invalid or unregistered function` at a time. At roughly three
minutes per rebuild, finding a hundred of them that way costs hours.

`tools/scan_fnptrs.py` finds them in bulk, from **two channels**.

**Channel 1 — pointers stored as data.** Read every 4-byte-aligned word in
`.rdata` and `.data`, keep the ones pointing into `.text`, drop the ones the
analyzer already knows, and require the pointer to sit in a **run of at least
two** consecutive code pointers (so a lone integer that looks like an address
is ignored). This is where vtables and dispatch tables live.

**Channel 2 — pointers built in code.** A callback address does not have to be
stored anywhere. MSVC materialises it inline:

```
lis  r11, 0x8221
addi r10, r11, 0x42D0        -> 0x822142D0
```

`0x822142D0` is a real three-instruction getter, and the value `0x822142D0`
appears **nowhere in the image** — not as a pointer, not at any alignment.
Channel 1 cannot see it. It reached us as a runtime fatal only once the player
got past character select and the world started loading.

Both channels then require the instruction **before** the target to be one
control cannot fall through — `blr`, `bctr`, an unconditional `b`, or padding.
That is what makes a false positive cheap: registering an address that is
really the middle of a straight-line function would cut that function short,
but if the preceding instruction cannot fall through, splitting there costs
nothing even when the guess is wrong.

**What a crash in play taught channel 2 (2026-09-12).** A builder at
0x82DE2D48 fills a table with ten callbacks at 0x82DE2B38..0x82DE2CF8, and
the analyzer had absorbed builder and callbacks alike into one function;
the old rule "site and target must be in different functions" (meant to
keep out labels) threw the whole family away, and the game died on the
fourth of them. The rule is now the property it stood for: a label is the
destination of a direct branch somewhere in `.text`, and a materialised
address no branch jumps to is a function pointer whoever owns it. The
`lis` window is 24 instructions (that builder holds five `lis` results
across 16). Three shape tests keep the wider net honest, each written for a
case seen in a sample: a block that reads a non-volatile register, or the
caller's frame at a non-negative `r1` offset, before writing it is the
middle of a function (a continuation the parent stores), not a function; a
run of `li rD, k; b L` pairs is a switch's cases; and a site whose
materialised address feeds a `bctr` before any call is a computed jump into
its own cases. The same tests dropped one entry the first pass had
registered, 0x82451E90, which reads the frame pointer in its first
instruction - a continuation that would have read a zero `r31` the day the
game resumed there. The import-thunk area under 0x832B97A8 stays in
`config/fnptr_exclude.txt` (the codegen drops those and the link then wants
them).

#### Channel 2 was wrong the first time, and only a human sample caught it

Raw, channel 2 produced **919** candidates. Every automated check passed. A
random sample of ten, disassembled, contained **nine switch jump tables and one
real function**:

```
0x82479F9C   lwz r18, -0x6028(r7)
             lwz r18, -0x5fb4(r7)
             lwz r18, -0x5968(r7)
             lwz r18, -0x5968(r7)      <- this is a table of code addresses,
             lwz r18, -0x5968(r7)         not code
```

A PowerPC switch computes its table's address with exactly the `lis`/`addi`
pair channel 2 looks for, the table sits immediately after a `bctr` (a perfect
"terminator"), and a table entry like `0x8221A4F0` disassembles as a
thoroughly plausible `lwz`. **This is the same trap that made ng2recomp's
`scan_missed.py` useless**, arrived at from the opposite direction — and the
docstring claiming this approach avoided it was, for a while, simply wrong.

Two filters fix it, and the count falls **919 → 175 → 56**:

1. **Read the target as data, not as code.** If the first three words are all
   `.text` addresses, it is a jump table, not a function.
2. **The site and the target must be in different analyzer functions.** A
   switch base is computed inside the function that owns it, a few instructions
   away. A genuine callback is taken by code somewhere else entirely — the
   confirmed one had its site 3.6 MB from its target.

After both, 32 of the 56 survivors are the textbook virtual-dispatch thunk
(`lwz r12,0(r3); lwz r11,off(r12); mtctr r11; bctr`) and the rest are small
helpers. `--sample N` exists because eyeballing a disassembled sample is the
check that actually worked; run it before believing a new channel.

#### `b $+4` is not a terminator, and that one word cost a build

Registering the whole channel-2 set regressed the game from *reaches character
select* to *unhandled read of guest `0x54`, milliseconds after launch*.
`tools/bisect_fnptrs.py` narrowed 51 registrations to one over eight rebuilds:
**`0x82FFD258`**.

```
0x82FFD250  mr r8, r8
0x82FFD254  b 0x82FFD258        <- an unconditional b ... to the next instruction
0x82FFD258  lwz r3, 0x54(r31)   <- registered as a new function starts HERE
0x82FFD25C  bl 0x82FFCD38
```

MSVC emits `b $+4` as a no-op. It is an unconditional `b`, so the obvious
"control cannot fall through" test says terminator — while it plainly does fall
through. Splitting there cut a function in half, and the orphaned half reads
`0x54` off `r31`, the frame pointer its parent's prologue set up
(`addi r31, r1, -0x80`). Under `non_volatile_as_local` every recompiled
function owns its own r14–r31, so the new half got a zeroed `r31` and read
guest `0x54` — **which is the fault address in the error message, verbatim**.

Two things are worth keeping from this:

- The safety rule ("only split where control cannot fall through") was sound;
  the *implementation* of it had one hole, and the hole was the only branch in
  the instruction set that goes nowhere.
- `non_volatile_as_local` is what turns a bad split from "subtly wrong" into a
  clean, immediate, diagnosable null read. That is a good trade — without it
  the two halves would have shared `r31` and the split would have appeared to
  work.

With `b $+4` excluded, the whole channel-2 set goes back in and the build is
clean.

#### The other four ways it broke

Each produced a plausible-looking result:

1. **Sizes must be clamped to the next function start.** `function_extent`
   walks forward to a terminator and will run straight through a later entry
   point. Codegen then refuses the whole manifest —
   *"Overlapping boundaries: 0x830FD7B8+0x7C overlaps 0x830FD7F4+0x40"* — and
   because that failure happens in codegen, the *previous* executable is still
   sitting in the build directory, so the next run looks like it succeeded.
   Always check that the build actually rebuilt.
2. **A split can break a branch.** A `b` that used to be an internal jump — a
   loop back-edge, or a jump to a shared epilogue — can end up crossing the new
   boundary, and the recompiler cannot emit a jump across a C++ function
   boundary. Codegen reports *"Unresolved b target 0xT from 0xS"*. A control
   run with only the 15 hand-found overrides produced **zero** of these, so
   every one was ours. `--prune` is the fixpoint loop: run codegen, add
   whatever it complained about to `config/fnptr_exclude.txt`, **regenerate the
   whole block**, repeat. Regeneration rather than deleting lines matters —
   remove one entry and its predecessor is still sized to a boundary that no
   longer exists, leaving a hole that surfaces as a *new* unresolved call.
3. **Some targets are import thunks, and codegen keeps the import.** The last
   few KB of `.text` is the import thunk table. Registering an entry there
   makes codegen log `[functions] 0x832B2A0C outranked by import`, register a
   name, and then never emit a body for it — so the failure lands at *link*
   time, after a full compile, as `undefined symbol: sub_832B2A0C_fnptr`.
   Codegen said so several minutes earlier; `--prune` now reads that line.
4. **The tool must not read its own output as evidence.** Candidates are
   compared against the analyzer's function list in
   `generated/default/codegen.partition.json` — but that file is written by the
   last codegen run, which included whatever this tool wrote last time. Reading
   it raw made the tool treat its own previous output as the analyzer's
   opinion, and the candidate list silently collapsed from 121 to 13 on the
   second run. It now subtracts its own generated block, which is sound because
   registering a function only ever *splits* an analyzer function, never merges
   two.

And the analyzer's list is the right comparison in the first place: its
Discover and GapFill phases find thousands of functions beyond the unwind table
— 60,219 registered against 46,072 `.pdata` entries — so comparing against
`.pdata` reported 4,323 "missing" functions where there are ~155.

**`--check`** re-derives the functions registered by hand and reports how many
this scan would have found. It finds 4 of 15 — honest rather than
disappointing: the other 11 are reached by a direct `b` from code, which is
`resolve_calls.py`'s job at codegen time.

### `setjmp` / `longjmp` — found before running, on purpose

On Ninja Gaiden II this pair was the single biggest fix, and it was found the
hard way: unset, the resource parsers' error path *returned* instead of
unwinding, fell into a copy loop whose bounds check was meant to be fatal, and
overran a stack buffer — crashing minutes later on a garbage pointer nowhere
near the cause. **A crash on a garbage pointer far from any obvious cause is a
plausible signature of missing `setjmp`/`longjmp`**, because an un-modelled
`longjmp` turns "abort this operation" into "carry on with invalid state".

So `tools/find_setjmp.py` finds them by shape rather than waiting for the
symptom. Both are confirmed exact mirrors of each other:

```
0x83000200  _setjmp(buf)
    lis r4, 0x8332 / lwz r0, 0x1a8c(r4) / mtctr / bnectr   <- the setjmp hook
    mflr r0 / mfcr r4
    stfd f14..f31 -> buf+0x00 ; std r13..r31 -> buf+0x98
    stvx128 v20.. -> buf+0x140
    stw LR -> +0x134 ; stw CR -> +0x130 ; std r1 -> +0x90 ; stw 0 -> +0x138
    li r3, 0 ; blr                                          (ends 0x830004C8)

0x82CA9260  longjmp(buf, value)
    lwz r0, 0x138(r3)         <- the same flag setjmp cleared
    mr r7, r3                 <- buf moves to r7
    lwz r3, 0x134(r7)         <- saved LR
    lwz r4, 0x90(r7)          <- saved SP
    lfd f14..f31 / ld r13..r31 / lvx128 back out of r7
```

`_setjmp` has **no `.pdata` entry of its own** — it sits inside the range
starting `0x82FFFF68`, alongside the `__savevmx`/`__restvmx` helpers — which is
exactly why it has to be named in the manifest by hand. Same as on NG2.

### Codegen flags

The manifest sets `non_volatile_as_local`, `skip_lr`, `ctr/xer/cr/reserved_as_local`
— what re:Blue ships, and what NG2 needed. The defaults keep every guest
register in one shared `PPCContext`, which makes correctness depend on each
callee's prologue and epilogue saving and restoring non-volatiles through guest
stack memory; on NG2 that was a real crash (`r31` went null across a call).
With the flag, r14–r31 become function locals and the `__savegprlr`/`__restgprlr`
helper calls are elided.

`skip_lr` was verified safe here the same way: **zero `bl $+4` PC-capture
idioms** in 4.5 M instructions.

### The 2.4 MB "function" warning is not a bug

Codegen warns:

```
Function 0x82242ED0 is 2433590 bytes, exceeds max_file_size_bytes (1048576)
```

That number is the size of the *emitted C++*, not of the guest function.
`0x82242ED0` is a genuine `.pdata` function start whose next entry is
`0x8225DC40` — 0x1AD70 = 109,424 guest bytes, about 27,000 instructions, which
at ~89 bytes of C++ each is exactly 2.4 MB. It compiles; it is just a big
translation unit.

## Known gaps

- Nothing has been run yet. Reaching a build is not reaching a frame.
- The 20 Bink videos are untested (see above for why they are expected to work).
- No DLC. The GOTY disc carries the two expansions on-disc, so unlike NG2 there
  is probably no STFS licence work to do — unverified.
- `$SystemUpdate/` on the disc is the *dashboard* update, not a game title
  update, and is not used.
- No settings UI. `ng2recomp` grew a pre-boot setup screen and an F10 overlay
  that are worth porting once the game runs; the SDK's own cvar browser is on
  F4 in the meantime.
