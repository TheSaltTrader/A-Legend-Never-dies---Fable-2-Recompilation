"""Delete the runtime's caches, without touching saves or installed content.

    python tools/clear_cache.py            # show what would go, change nothing
    python tools/clear_cache.py --yes      # actually delete

WHY THIS EXISTS, AND WHY IT IS NOT `rm -rf`
-------------------------------------------
The runtime keeps everything for this title under one root:

    Documents/fable2/
      cache/shaders/                          <- safe to delete, rebuilt on demand
      B13EBABEBABEBABE/4D5307F1/00000001/     <- SAVE GAMES. Hero000..Hero003.
      0000000000000000/4D5307F1/00000002/     <- installed DLC packages
      4D5307F1/profile/                       <- the player profile

Only the first is disposable. Everything else is hours of somebody's game, and
a stray recursive delete one directory too high takes the lot - so this script
enumerates exactly what it will remove, refuses to touch anything outside
`cache/`, and does nothing at all without --yes.

Clearing the shader cache matters for this game specifically: the maintainers
of the unofficial Xenia fork for Fable II note that stale cached shaders make
its black-texture bug linger, and a cache built by an earlier, buggier build of
this project is exactly that hazard.
"""

import argparse
import os
import shutil
import sys

ROOT = os.path.join(os.path.expanduser("~"), "Documents", "fable2")

# Only these are ever removed. Anything not under one of them is out of scope
# by construction, not by a check that could be got wrong later.
DISPOSABLE = [
    os.path.join("cache", "shaders"),
]

# Named so the dry run can say what is being protected, rather than silently
# leaving it out.
PROTECTED = [
    ("B13EBABEBABEBABE", "save games"),
    ("0000000000000000", "installed DLC"),
    ("4D5307F1", "player profile"),
]


def tree_size(path):
    total = files = 0
    for base, _, names in os.walk(path):
        for n in names:
            try:
                total += os.path.getsize(os.path.join(base, n))
                files += 1
            except OSError:
                pass
    return files, total


def human(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024
    return f"{n:.1f} GB"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--yes", action="store_true",
                    help="actually delete; without it this only reports")
    ap.add_argument("--root", default=ROOT)
    args = ap.parse_args()

    if not os.path.isdir(args.root):
        print(f"{args.root} does not exist - nothing cached yet.")
        return 0

    print(f"runtime data root: {args.root}\n")

    print("keeping:")
    for name, what in PROTECTED:
        p = os.path.join(args.root, name)
        if os.path.isdir(p):
            files, size = tree_size(p)
            print(f"   {name:<20} {what:<16} {files} file(s), {human(size)}")
    print()

    targets = []
    for rel in DISPOSABLE:
        p = os.path.join(args.root, rel)
        if os.path.isdir(p):
            files, size = tree_size(p)
            targets.append((p, rel, files, size))

    if not targets:
        print("nothing to clear - no shader cache present.")
        return 0

    print("clearing:" if args.yes else "would clear (pass --yes to do it):")
    for p, rel, files, size in targets:
        print(f"   {rel:<20} {files} file(s), {human(size)}")

    if not args.yes:
        return 0

    for p, rel, _, _ in targets:
        # Belt and braces: never delete anything that is not under the root's
        # own cache directory, whatever DISPOSABLE might say in future.
        cache_root = os.path.join(os.path.abspath(args.root), "cache")
        if not os.path.abspath(p).startswith(cache_root + os.sep):
            print(f"   refusing {p}: outside {cache_root}")
            continue
        shutil.rmtree(p, ignore_errors=True)
        print(f"   removed {rel}")

    print("\nThe shaders rebuild on the next launch, so the first run after "
          "this is slower.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
