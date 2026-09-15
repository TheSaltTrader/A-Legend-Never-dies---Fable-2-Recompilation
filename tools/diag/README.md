# Ultrawide menu / transition diagnostics

Session tooling for the ultrawide menu work (see ../docs/ULTRAWIDE_MENU.md).
They capture the game window with GDI, read the pause flag by ReadProcessMemory
(guest 0x834B2467, host = 0x100000000 + 0x834B2467), and drive the menu by
writing `pad_script.txt` in the exe dir. Paths inside are absolute to this
machine's build dir - adjust EXE_DIR / OUT as needed.

- `menu_probe.py <outdir> <seconds>` - open+close the menu (start at 1.3s, close
  at 4.2s), capture every frame, read the flag on the same clock, classify each
  frame ULTRAW / BARS169 (16:9 bars) / BLACK / MENUq, print a timeline.
- `montage.py <framedir> <lo_ms> <hi_ms> <out.png> [cols]` - contact sheet of the
  frames whose filename ms is in [lo,hi].
- `open_burst.py <outdir>` / `close_burst.py <outdir>` - full-res burst of one
  open / close transition, saving every frame with flag + edge/centre means.

Capture samples ~40 fps; the game runs ~170, so single-frame transitions are
missed. Use the AI-Vision `eye` MCP tools when available (see the doc).

- `analyze_dump.py <draw_dump_N.txt>`: which shader pairs come and go across the frames of a per-draw dump (`dump:N` pad command), with their state; found the pause-menu transition layer (v0.2.11).
- `launch_hero.ps1 [-Pad ...] [-Tune ...]`: launch fable2.exe from the build dir with a boot pad script and FABLE2_TUNE overrides; refuses if one runs.
- `flash_probe.py <outdir> <seconds>`: 40 fps GDI capture of the primary screen scored per frame for one-frame white/magenta/brightness spikes; saves a contact sheet per flagged frame.
- `flash_ab.sh <tag> "<tune>"`: hero 1 at Bower Lake, camera sweeps, the flash probe running; prints the verdict, fps and fence line.
- `lake_await.sh <tag> "<tune>" [secs]` / `lake_profile.sh`: the same sweep without the probe (fps + fence census), and with FABLE2_PROFILE (hottest host functions).
- `variant_flash2.sh "tag|tune" ...` / `variant_flash3.sh`: per configuration, launch (Start-Process, so FABLE2_TUNE reaches the game), boot from the log's own state lines, pan-pan-walk to the reproducing forest view, score 25 s static (v2) or a 70-s pan (v3). `flash_region.py x y w h secs` scores one screen region at ~190 fps.
