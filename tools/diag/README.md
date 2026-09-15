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
