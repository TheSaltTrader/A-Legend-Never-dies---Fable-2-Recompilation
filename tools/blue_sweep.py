"""Sweep settings against the flat-blue bug, and report which ones avoid it.

    python tools/blue_sweep.py --seconds 330

THE BUG
-------
Once the game reaches the tutorial ("Follow the glowing trail to your next
objective"), the 3D scene renders as flat saturated blue. The UI layer draws
correctly ON TOP of it, which is the whole clue: this is not a freeze and not a
hang - the frame keeps changing, the guest keeps running, and the HUD pass
works. The scene pass is producing a constant colour.

MEASURING IT
------------
"Blue" is a fraction of pixels where the blue channel dominates both others by a
margin, sampled over the run. Two numbers are reported:

  peak blue   the worst single frame
  blue frames how many samples were mostly blue

A run that never reaches the tutorial scores 0 and is reported as INCONCLUSIVE
rather than as a pass - that distinction matters, because a fixed-time input
schedule does NOT reliably reach the failing state. Debug-level logging alone
slows the boot enough to desynchronise it, which already produced one run that
looked clean and had simply stayed in the attract loop.

Reaching the tutorial is detected by the tutorial banner's own dark strip at the
top of the frame, so "did we get there" is answered from the picture rather than
assumed from the clock.
"""

import argparse
import ctypes
import os
import shutil
import subprocess
import sys
import time

import numpy as np
import win32gui
import win32process
from PIL import Image
from windows_capture import WindowsCapture

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHADER_CACHE = os.path.join(os.path.expanduser("~"), "Documents", "fable2",
                            "cache", "shaders")

def play_presses(until):
    """Load a SAVE and walk, rather than starting a new game.

    New Game means the character-select screen, and that screen is the one
    place a blind schedule reliably gets stuck: the two cards need a stick
    deflection before A does anything, and a dense stream of A presses does not
    supply one. Three separate sweeps were lost that way, each quietly
    reporting "clean" while sitting on an unselected pair of cards.

    Continue skips all of it and lands directly in the world - which is also
    exactly where the flat-blue bug lives. It needs a save to exist; the
    project has had several since the first playthrough.

    The path is short and each step is a state the menu really has:
        A            title screen -> main menu (New Game highlighted)
        down, A      main menu -> Continue -> load
    then walk. A few spare A presses ride along to clear any prompt the load
    puts up.
    """
    out = [
        "20:space",                 # title -> main menu
        "27:s:0.35",                # New Game -> Continue
        "31:space",                 # load it
        "45:space", "60:space",     # clear whatever the load puts up
    ]
    t = 80
    while t < until:
        out += [f"{t}:w:3.0", f"{t+7}:a:1.0", f"{t+13}:w:3.0", f"{t+20}:space"]
        t += 26
    return out


def clear_shader_cache():
    shutil.rmtree(SHADER_CACHE, ignore_errors=True)


def run_case(name, extra, seconds):
    clear_shader_cache()
    outdir = os.path.join(ROOT, "out", "shots")
    tag = f"blue_{name}"
    for f in os.listdir(outdir):
        if f.startswith(tag + "_"):
            os.remove(os.path.join(outdir, f))

    cmd = [sys.executable, os.path.join(ROOT, "tools", "play_probe.py"),
           "--seconds", str(seconds), "--interval", "10", "--tag", tag,
           "--hold", "0.25"]
    for e in extra:
        cmd.append(f"--extra={e}")
    for p in play_presses(seconds - 40):
        cmd += ["--press", p]

    print(f"\n=== {name}: {' '.join(extra) if extra else '(defaults)'} ===",
          flush=True)
    subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)

    peak = 0.0
    blue_frames = 0
    reached = False
    frames = sorted(f for f in os.listdir(outdir) if f.startswith(tag + "_"))
    for f in frames:
        a = np.asarray(Image.open(os.path.join(outdir, f)).convert("RGB")
                       .resize((120, 68)), dtype=float)
        # The scene occupies everything below the tutorial banner.
        scene = a[12:, :, :]
        blue = float(((scene[..., 2] > scene[..., 0] + 50) &
                      (scene[..., 2] > scene[..., 1] + 50)).mean())
        peak = max(peak, blue)
        if blue > 0.5:
            blue_frames += 1
        # The tutorial banner: a dark strip across the very top.
        band = a[2:9, :, :]
        if band.mean() < 60 and band.std() < 55:
            reached = True

    verdict = ("BLUE" if blue_frames else
               "clean" if reached else "INCONCLUSIVE - never reached the tutorial")
    print(f"   frames {len(frames)}  peak blue {peak*100:.0f}%  "
          f"blue frames {blue_frames}  -> {verdict}")
    return verdict


def main():
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seconds", type=float, default=330)
    ap.add_argument("--only", help="run just this case")
    args = ap.parse_args()

    # Flat blue with a working UI layer says the SCENE pass is producing a
    # constant colour while the HUD pass, drawn later, is fine. The most
    # likely shape of that is a resolve or composite landing on the wrong
    # surface - so the resolve-path options are in here alongside the
    # patches and readback.
    # From tools/config_diff.py: the GPU/kernel settings where Canary - which
    # plays through this point - differs from our defaults. The stand-out is
    # execute_unclipped_draw_vs_on_cpu: Xenia runs the vertex shader for
    # UNCLIPPED draws on the CPU, and a loading screen is exactly a big
    # unclipped full-screen quad.
    CANARY = ["--execute_unclipped_draw_vs_on_cpu=true",
              "--clear_memory_page_state=false",
              "--readback_memexport=false",
              "--primitive_processor_cache_min_indices=4096",
              "--anisotropic_override=-1"]
    cases = [
        ("baseline", []),
        ("canaryall", CANARY),
        ("unclipped", ["--execute_unclipped_draw_vs_on_cpu=true"]),
        ("nocleanpg", ["--clear_memory_page_state=false"]),
        ("nomemexp", ["--readback_memexport=false"]),
        # The community guidance for this title names d3d12_readback_resolve,
        # which is a DIFFERENT cvar from readback_resolve - this runtime has
        # both, and only the latter was being set. Worth its own case.
        ("d3drdbk",  ["--d3d12_readback_resolve=true"]),
        ("bothrdbk", ["--d3d12_readback_resolve=true", "--readback_resolve=full"]),
        ("nopatch",  ["--fable2_720p=false", "--fable2_disable_msaa=false",
                      "--fable2_60fps=false", "--fable2_high_tick_rate=false"]),
        ("noreadbk", ["--readback_resolve=none"]),
        ("fullrdbk", ["--readback_resolve=full"]),
        ("strictfc", ["--gpu_allow_invalid_fetch_constants=false"]),
        ("rtv",      ["--render_target_path_d3d12=rtv"]),
        # Resolve path.
        ("nodirect", ["--direct_host_resolve=false"]),
        ("no3d2d",   ["--gpu_3d_to_2d_texture=false"]),
        ("nopremask", ["--pre_mask_resolve_l2_block=false"]),
        ("no2xmsaa", ["--native_2x_msaa=false"]),
        ("nounorm16", ["--gamma_render_target_as_unorm16=false"]),
        ("nosnorm",  ["--snorm16_render_target_full_range=false"]),
    ]
    results = {}
    for name, extra in cases:
        if args.only and name != args.only:
            continue
        results[name] = run_case(name, extra, args.seconds)

    print("\n=== summary ===")
    for name, verdict in results.items():
        print(f"   {name:<10} {verdict}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
