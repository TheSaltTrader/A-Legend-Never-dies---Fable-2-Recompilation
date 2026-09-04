"""Stage our own SDK build's DLLs over the ones the app build copies in.

    python tools/stage_sdk.py [--config Release] [--check]

WHY THIS EXISTS

The app links against an INSTALLED ReXGlue SDK (REXSDK, by default
..\\RexBlue\\win-amd64), and `rexglue_setup_target` copies that SDK's
rexruntime.dll and rexgpu-xenos.dll next to fable2.exe on EVERY build. So any
DLL built from our own SDK source tree is silently overwritten by the stock one
the next time the app is built.

That has bitten this project twice - once with the Vulkan plugin, once here -
and both times the symptom was a feature that "stopped working" with no error,
because a stock DLL is a perfectly valid DLL. It just does not have the thing.

So: build the app first, then run this. It is the last step, never the first.

WHAT OUR BUILD HAS THAT THE STOCK SDK DOES NOT

  * Vulkan   - the stock Windows plugin is D3D12-only (REXGLUE_USE_VULKAN
               defaults OFF on Windows), so "Graphics engine: Vulkan" cannot
               load at all without this.
  * FSR/CAS  - the stock runtime advertises present_effect="bilinear" and
               nothing else, so the Upscaling setting has only one value.

Both come from rexglue-src/build_vulkan.cmd.

ABI NOTE

REX_HAS_FIDELITYFX_SDK adds members to GuestOutputPaintConfig and values to
Presenter::Effect, so a header compiled without it does NOT describe these
binaries. That is safe here only because fable2.exe never constructs those
types - it calls Window::RequestPresenterUIPaintFromUIThread() and nothing
else - and because rexgpu-xenos.dll takes its rexui objects from
rexruntime.dll rather than containing its own copy. If app code ever starts
using Presenter's config directly, the SDK must be installed and REXSDK
repointed instead of staging loose DLLs.
"""

import argparse
import hashlib
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_BUILD = os.path.join(ROOT, "..", "rexglue-src", "out", "win-amd64", "Release")
DLLS = ["rexruntime.dll", "rexgpu-xenos.dll"]

# A marker string that only our build's DLL contains, so "did the stage take?"
# is answered by reading the file rather than by trusting a copy that ran.
MARKERS = {
    "rexruntime.dll": [b"bilinear, cas, fsr", b"present_fsr_sharpness_reduction"],
    "rexgpu-xenos.dll": [b"vulkan"],
}


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()[:12]


def markers_present(path):
    """Which of the expected markers this binary actually contains."""
    name = os.path.basename(path)
    with open(path, "rb") as f:
        blob = f.read()
    return [m for m in MARKERS.get(name, []) if m in blob]


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", default="Release",
                    help="app build config (default Release)")
    ap.add_argument("--check", action="store_true",
                    help="report what is staged, copy nothing")
    args = ap.parse_args()

    dest_dir = os.path.join(ROOT, "out", "build", "win-amd64-" + args.config)
    if not os.path.isdir(dest_dir):
        sys.exit("app build directory does not exist: %s\n"
                 "Build the app first - this is the step AFTER it." % dest_dir)
    if not os.path.isdir(SOURCE_BUILD):
        sys.exit("SDK source build not found: %s\n"
                 "Run rexglue-src\\build_vulkan.cmd first." % SOURCE_BUILD)

    failures = 0
    for name in DLLS:
        src = os.path.join(SOURCE_BUILD, name)
        dst = os.path.join(dest_dir, name)
        if not os.path.exists(src):
            print("  MISSING in SDK build: %s" % name)
            failures += 1
            continue

        if not args.check:
            shutil.copy2(src, dst)

        if not os.path.exists(dst):
            print("  NOT STAGED: %s" % name)
            failures += 1
            continue

        same = digest(src) == digest(dst)
        found = markers_present(dst)
        want = MARKERS.get(name, [])
        state = "ours" if same else "STOCK (overwritten by the app build)"
        print("  %-20s %s  sha=%s  markers=%d/%d"
              % (name, state, digest(dst), len(found), len(want)))
        for m in want:
            if m not in found:
                print("       missing marker: %s" % m.decode())
        if not same or len(found) != len(want):
            failures += 1

    if failures:
        print("\n%d problem(s). If a DLL reads STOCK, the app was rebuilt after "
              "staging - run this again." % failures)
        return 1
    print("\nStaged OK." if not args.check else "\nAll staged DLLs are ours.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
