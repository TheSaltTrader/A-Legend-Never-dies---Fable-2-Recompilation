"""Package a versioned, runnable release of fable2recomp into ..\\Releases.

A release is a folder anyone can unzip and run *provided they own the game*:
the recompiled executable, the SDK runtime and GPU plugin beside it, the VC
runtime, the controller database, the texture tools with the AI engine, and
instructions to point the setup screen at their own disc.

    python tools/make_release.py                 # cut the version in VERSION
    python tools/make_release.py --build         # rebuild first, then cut it
    python tools/make_release.py --version 0.3.0 --force
    python tools/make_release.py --update D:\\Fable2   # this build over an install

What it refuses to do, ported from the Ninja Gaiden II port's packager where
each refusal was paid for by a bad release:

  * cut a version with no CHANGELOG entry - the notes are the release,
  * update a folder that is not already an install (--update),
  * cut a release missing a REQUIRED tool,
  * cut a build older than the sources it claims to be built from,
  * ship one stock SDK DLL beside one source-built one,
  * overwrite an existing release folder without --force,
  * stage anything that looks like game data (see FORBIDDEN_SUFFIXES).

The zip is what the GitHub release carries and what the in-game updater
downloads (src/fable2_update.cpp looks for fable2recomp-*-win-amd64.zip). It
holds the game's code in translated form and no game data.
"""

import argparse
import datetime
import glob
import hashlib
import os
import platform
import re
import shutil
import subprocess
import sys
import textwrap
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SDK_DIR = os.path.abspath(os.path.join(ROOT, "..", "RexBlue", "win-amd64"))
DEFAULT_RELEASES = os.path.abspath(os.path.join(ROOT, "..", "Releases"))
BUILD_DIR = os.path.join(ROOT, "out", "build", "win-amd64-Release")

# Copied verbatim from the build directory. Everything the executable needs at
# runtime other than the game itself.
PAYLOAD = [
    "fable2.exe",
    "rexruntime.dll",
    "rexgpu-xenos.dll",
    "gamecontrollerdb.txt",
]

# The Visual C++ runtime the executable and both SDK DLLs import. Windows does
# not ship it; without these a machine that never installed the redistributable
# fails at start-up with a "DLL not found" box. Microsoft permits shipping them
# beside an application (app-local deployment).
VCRT_DLLS = ["msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll",
             "msvcp140_atomic_wait.dll"]
VCRT_GLOB = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                         "Microsoft Visual Studio", "2022", "*", "VC", "Redist", "MSVC",
                         "*", "x64", "Microsoft.VC143.CRT")

# Sources whose mtime must predate the executable, or the build is stale.
SOURCE_GLOBS = ["src", "config", "fable2_manifest.toml", "CMakeLists.txt", "VERSION"]

# Copied into the release's tools/. (name, required). The app finds them by
# looking for tools/<name> beside the executable; a missing one makes the
# texture buttons fail with "missing from this install".
TOOLS = [
    ("upscale_textures.py", True),   # dump -> pack, and the plain upscaler
    ("ai_upscale.py", True),         # imported by the above for --ai
    ("get_upscaler.py", True),       # fetches a per-folder engine on request
]
# Folders copied whole into the release's tools/. The AI upscaler engine
# (Real-ESRGAN ncnn-vulkan + its models, BSD-3, third party) lives at
# tools/upscaler beside the scripts, kept out of git; shipping it makes the AI
# option - the default - work without a download.
TOOL_DIRS_SHIPPED = [("upscaler", True)]

# Where a tool may live. The source tree first, then the build directory.
TOOL_DIRS = [os.path.join(ROOT, "tools"), os.path.join(BUILD_DIR, "tools")]

# What an install must already contain before --update will write into it.
INSTALL_MARKERS = ["fable2.exe", "game", "fable2_settings.cfg"]

# Never touched by --update (nor by the in-game updater, which only moves the
# zip's own files into place). Everything here is the player's.
UPDATE_KEEPS = ["game", "dlc", "user", "logs", "cache", "shadow", "crashdumps",
                "diagnostics", "fable2_settings.cfg", "fable2.toml"]

# A release must never contain game data. Checked over the staged tree rather
# than trusted: assets/, game/ and dlc/ are gitignored, but a stray copy in the
# build directory would sail straight into the zip. The AI engine's model
# weights are the one .bin carried on purpose, under tools/upscaler only.
FORBIDDEN_SUFFIXES = (".xex", ".xexp", ".iso", ".bin", ".dat", ".bnk", ".big", ".lut",
                      ".gdb", ".bik", ".wmv", ".xma", ".tex")
FORBIDDEN_EXCEPTIONS = ("gamecontrollerdb.txt",)

KNOWN_ISSUES = [
    "At ultrawide, the pause and Up menus are shown at full width, so their "
    "circular map reads a little wide (an oval). The world stays correctly "
    "proportioned and the title and main menus are 16:9.",
    "Opening or closing the pause / Up menu at ultrawide, the menu's own "
    "fade-in / fade-out is drawn in a centred 16:9 band for a moment (a brief "
    "squeeze in, a faint layer out) while the world shows through the sides. The "
    "steady menu and the world are unaffected; a plugin-side fix is planned.",
]

README_TITLE = "fable2recomp v{version} - Fable II, statically recompiled for PC"

README_TEMPLATE = """\
{title}
{underline}

This is not an emulator. The game's PowerPC code was translated to C++ and
compiled into a native x86-64 executable; the ReXGlue SDK {sdk} supplies the
Xbox 360 kernel, filesystem, audio and GPU emulation around it.

REQUIRES YOUR OWN COPY OF THE GAME. No game code or data is included here.


Setting it up
-------------

1. Run fable2.exe. The first run opens a setup screen.

2. Point it at your game. Either:

     * "Choose folder..." - a folder that already holds your extracted disc,
       so that default.xex sits in it; or
     * "Choose disc image..." - your own .iso, which it extracts for you
       (about 6.5 GB, a minute or two).

   It reads the title out of the disc, so it will tell you if you have
   pointed it at the wrong game.

3. Optional: your DLC packages (the extensionless STFS files from your
   console's content folder) and the title update, which the setup screen
   installs beside the disc files.

4. Press Play.

The game/ and dlc/ folders beside fable2.exe are the defaults, so putting
your files there works without choosing anything.


Updating
--------

The game asks this project's GitHub releases page for the newest version
when it starts (three seconds at most; offline it just starts) and offers a
newer one: Update now, Not now, or Skip this version. Nothing downloads or
installs without a click, and the restart is a click of its own. An update
replaces only the files this zip carries; your game folder, DLC, saves,
settings and texture pack are never touched. The check can be turned off on
the settings screen (F10, Updates).

Updating by hand works too: unzip a newer release over this folder. The
same rule holds - game\\, dlc\\, user\\ and fable2_settings.cfg are not in
the zip, so they are not touched.


Settings
--------

  F10      the settings menu, over the running game
  F8       the on-screen readouts
  F9       texture pack on and off, without opening a menu
  Escape   quit, saving the settings
  F4       every runtime setting, unfiltered

The setup screen from step 1 comes back whenever you hold Shift while
launching. Everything is saved in fable2_settings.cfg beside the executable,
which is documented and can be edited by hand.


Known issues
------------

{known_issues}


Provenance
----------

Built {built} on {host}.
See SHA256SUMS for file hashes and provenance.txt for exactly what went in.

fable2.exe contains Fable II's own code in translated form and no game data;
it does nothing without your own copy of the game.
"""


def die(msg):
    print("ERROR: %s" % msg, file=sys.stderr)
    sys.exit(1)


def human(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return "%.1f %s" % (n, unit) if unit != "B" else "%d B" % n
        n /= 1024.0


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_version(explicit):
    if explicit:
        version = explicit.strip().lstrip("v")
    else:
        path = os.path.join(ROOT, "VERSION")
        if not os.path.exists(path):
            die("no VERSION file and no --version given")
        version = open(path).read().strip().lstrip("v")
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        die("version %r is not MAJOR.MINOR.PATCH" % version)
    return version


def changelog_section(version):
    """The body of the '## X.Y.Z' section (a leading v is accepted).

    A release with no notes is a release nobody can tell apart from the one
    before it, so a missing or empty section is fatal rather than a warning.
    """
    path = os.path.join(ROOT, "CHANGELOG.md")
    if not os.path.exists(path):
        die("CHANGELOG.md is missing - write the notes before cutting a release")
    text = open(path, encoding="utf-8").read()
    heading = re.compile(r"^## v?%s\b.*$" % re.escape(version), re.M)
    m = heading.search(text)
    if not m:
        die("CHANGELOG.md has no '## %s' section - write the notes first" % version)
    rest = text[m.end():]
    nxt = re.search(r"^## ", rest, re.M)
    body = (rest[:nxt.start()] if nxt else rest).strip()
    if not body:
        die("CHANGELOG.md section for %s is empty" % version)
    return body


def iter_sources(paths):
    """Every source file under `paths`, as ROOT-relative posix paths."""
    for rel in paths:
        target = os.path.join(ROOT, rel)
        if os.path.isfile(target):
            yield rel.replace("\\", "/")
        for dirpath, dirnames, filenames in os.walk(target):
            dirnames[:] = [d for d in dirnames if d != "__pycache__"]
            for name in filenames:
                full = os.path.join(dirpath, name)
                yield os.path.relpath(full, ROOT).replace("\\", "/")


def newest_source(paths):
    """(mtime, path) of the most recently modified source file."""
    newest = (0.0, None)
    for rel in paths:
        target = os.path.join(ROOT, rel)
        if os.path.isfile(target):
            newest = max(newest, (os.path.getmtime(target), target))
        for dirpath, dirnames, filenames in os.walk(target):
            dirnames[:] = [d for d in dirnames if d != "__pycache__"]
            for name in filenames:
                full = os.path.join(dirpath, name)
                newest = max(newest, (os.path.getmtime(full), full))
    return newest


def find_tool(name):
    for directory in TOOL_DIRS:
        candidate = os.path.join(directory, name)
        if os.path.isfile(candidate):
            return candidate
    return None


def copy_tool_dirs(dest_tools):
    """Copy each shipped tool folder (whole tree) into dest_tools."""
    for folder, required in TOOL_DIRS_SHIPPED:
        source = None
        for base in TOOL_DIRS:
            cand = os.path.join(base, folder)
            if os.path.isdir(cand):
                source = cand
                break
        if source is None:
            if required:
                die("tools/%s is required in a release and was not found in %s"
                    % (folder, " or ".join(TOOL_DIRS)))
            print("  tools/%-18s (not found, skipped)" % folder)
            continue
        target = os.path.join(dest_tools, folder)
        if os.path.isdir(target):
            shutil.rmtree(target)
        shutil.copytree(source, target,
                        ignore=shutil.ignore_patterns("__pycache__", "*.jpg", "*.mp4"))
        total = sum(os.path.getsize(os.path.join(r, f))
                    for r, _d, fs in os.walk(target) for f in fs)
        print("  tools/%-18s %s   [folder]" % (folder + "/", human(total)))


def sdk_dll_origin(name):
    """Which pair the release carries, for the two SDK DLLs; empty otherwise.

    ../RexBlue/win-amd64/bin holds the pair this port DEPLOYS - its own
    source-built runtime and GPU plugin (see patches/), not the vendor's -
    and every build re-copies it into the build directory. A DLL that differs
    from it was left in the build directory by hand, and must not ship
    unnoticed: a plugin against a runtime it was not built with exits at
    startup with no error and an empty log.
    """
    if name not in ("rexruntime.dll", "rexgpu-xenos.dll"):
        return ""
    deployed = os.path.join(SDK_DIR, "bin", name)
    built = os.path.join(BUILD_DIR, name)
    if not os.path.isfile(deployed):
        return "unknown (no deployed copy to compare)"
    tag = sha256(built)[:12]
    same = sha256(deployed) == sha256(built)
    return ("deployed pair, sha256 %s" % tag) if same else ("DIFFERS from the deployed pair, sha256 %s" % tag)


def check_sdk_pair():
    """Refuse to ship a DLL that is not the deployed one."""
    origins = {n: sdk_dll_origin(n) for n in ("rexruntime.dll", "rexgpu-xenos.dll")}
    off = [n for n, o in origins.items() if not o.startswith("deployed pair")]
    if off:
        die("%s in the build directory is not the deployed pair in %s\\bin. Deploy both "
            "from the same plugin build before cutting." % (", ".join(off), SDK_DIR))
    print("  SDK pair: both the deployed pair (RexBlue\\win-amd64\\bin)")


def sdk_version():
    try:
        out = subprocess.check_output(
            [os.path.join(SDK_DIR, "bin", "rexglue.exe"), "--version"],
            text=True, stderr=subprocess.STDOUT, timeout=30)
        return out.strip().splitlines()[0]
    except Exception:
        return "unknown"


def run_build():
    print("=== Building (tools/build.cmd) ===")
    rc = subprocess.call([os.path.join(ROOT, "tools", "build.cmd")], cwd=ROOT)
    if rc != 0:
        die("build failed (rc=%d) - not cutting a release from a broken build" % rc)


def update_install(folder, version):
    """Replace our files in an existing install, keeping the player's."""
    folder = os.path.abspath(folder)
    if not os.path.isdir(folder):
        die("no such folder: %s" % folder)
    if not any(os.path.exists(os.path.join(folder, m)) for m in INSTALL_MARKERS):
        die("%s does not look like a fable2 install (no %s).\n"
            "       Refusing to write an executable into it."
            % (folder, ", ".join(INSTALL_MARKERS)))
    print("=== Updating %s to v%s ===" % (folder, version))
    print("    keeping: %s" % ", ".join(UPDATE_KEEPS))
    for name in PAYLOAD:
        source = os.path.join(BUILD_DIR, name)
        if not os.path.isfile(source):
            die("%s is missing from the build - build first" % name)
        shutil.copy2(source, os.path.join(folder, name))
        print("  %-24s %s" % (name, human(os.path.getsize(source))))
    os.makedirs(os.path.join(folder, "tools"), exist_ok=True)
    for tool, required in TOOLS:
        source = find_tool(tool)
        if source is None:
            if required:
                die("%s is required and was not found in %s" % (tool, " or ".join(TOOL_DIRS)))
            continue
        shutil.copy2(source, os.path.join(folder, "tools", tool))
        print("  tools/%-18s %s" % (tool, human(os.path.getsize(source))))
    copy_tool_dirs(os.path.join(folder, "tools"))
    with open(os.path.join(folder, "VERSION.txt"), "w") as f:
        f.write("fable2recomp v%s\n" % version)
    print("\n=== %s is now v%s ===" % (folder, version))
    print("Game data, DLC, saves and settings untouched.")


def write_provenance(path, version):
    """Exactly what went into this build, so a release can be reproduced."""
    lines = [
        "fable2recomp v%s" % version,
        "built      %s" % datetime.datetime.now().isoformat(timespec="seconds"),
        "host       %s (%s)" % (platform.node(), platform.platform()),
        "SDK        ReXGlue %s at %s" % (sdk_version(), SDK_DIR),
        "build dir  %s" % BUILD_DIR,
        "",
        "Inputs (sha256):",
    ]
    # Enumerated from SOURCE_GLOBS, never hand-listed: a typed list silently
    # stops covering files added later.
    for rel in sorted(iter_sources(SOURCE_GLOBS)):
        lines.append("  %s  %s" % (sha256(os.path.join(ROOT, rel)), rel))
    with open(path, "w", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")


def write_sums(dest):
    """SHA256SUMS over every staged file, in the format sha256sum -c expects."""
    staged = []
    for dirpath, dirnames, filenames in os.walk(dest):
        dirnames.sort()
        for name in sorted(filenames):
            if name == "SHA256SUMS":
                continue
            staged.append(os.path.join(dirpath, name))
    with open(os.path.join(dest, "SHA256SUMS"), "w", newline="\n") as f:
        for full in staged:
            rel = os.path.relpath(full, dest).replace("\\", "/")
            f.write("%s  %s\n" % (sha256(full), rel))
    return staged


def make_zip(dest, zip_path, version):
    """Zip the staged folder with the files at its root (no version folder
    inside), so unzipping over an install lands them in place - which is
    also what the in-game updater expects."""
    print("=== Zipping -> %s ===" % os.path.basename(zip_path))
    if os.path.exists(zip_path):
        os.remove(zip_path)
    count = 0
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for dirpath, dirnames, filenames in os.walk(dest):
            dirnames.sort()
            for name in sorted(filenames):
                full = os.path.join(dirpath, name)
                z.write(full, os.path.relpath(full, dest).replace("\\", "/"))
                count += 1
    with zipfile.ZipFile(zip_path) as z:
        bad = z.testzip()
        if bad:
            die("zip verification failed at %s" % bad)
        names = z.namelist()
    if "fable2.exe" not in names:
        die("the zip has no fable2.exe at its root")
    print("  %d entries, %s, verified" % (count, human(os.path.getsize(zip_path))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default=None, help="override the version in VERSION")
    ap.add_argument("--update", metavar="FOLDER", default=None,
                    help="apply this build over an existing install, keeping its "
                         "game/, dlc/, user/ and settings")
    ap.add_argument("--releases-dir", default=DEFAULT_RELEASES)
    ap.add_argument("--build", action="store_true", help="run tools/build.cmd first")
    ap.add_argument("--force", action="store_true",
                    help="replace an existing release folder of this version")
    ap.add_argument("--no-zip", action="store_true")
    ap.add_argument("--allow-stale", action="store_true",
                    help="package a build older than the sources anyway")
    args = ap.parse_args()

    version = read_version(args.version)

    if args.update:
        if args.build:
            run_build()
        update_install(args.update, version)
        return 0

    # Everything that can fail on human input fails here, before any work.
    notes = changelog_section(version)
    dest = os.path.join(args.releases_dir, "v" + version)
    if os.path.exists(dest) and not args.force:
        die("%s already exists - bump the version, or pass --force" % dest)

    if args.build:
        run_build()

    missing = [f for f in PAYLOAD if not os.path.exists(os.path.join(BUILD_DIR, f))]
    if missing:
        die("build output missing %s in %s - run tools/build.cmd" % (", ".join(missing), BUILD_DIR))

    exe = os.path.join(BUILD_DIR, "fable2.exe")
    src_mtime, src_path = newest_source(SOURCE_GLOBS)
    if src_mtime > os.path.getmtime(exe):
        msg = ("fable2.exe is older than %s - rebuild, or pass --allow-stale"
               % os.path.relpath(src_path, ROOT))
        if not args.allow_stale:
            die(msg)
        print("WARNING: %s" % msg)

    print("=== Staging v%s -> %s ===" % (version, dest))
    if os.path.exists(dest):
        shutil.rmtree(dest)
    os.makedirs(dest)

    for name in PAYLOAD:
        shutil.copy2(os.path.join(BUILD_DIR, name), os.path.join(dest, name))
        note = sdk_dll_origin(name)
        print("  %-24s %s%s" % (name, human(os.path.getsize(os.path.join(dest, name))),
                                ("   [%s]" % note) if note else ""))
    check_sdk_pair()

    vcrt_dirs = sorted(glob.glob(VCRT_GLOB))
    if not vcrt_dirs:
        die("Visual C++ redistributable folder not found under %s" % VCRT_GLOB)
    vcrt_dir = vcrt_dirs[-1]
    for name in VCRT_DLLS:
        source = os.path.join(vcrt_dir, name)
        if not os.path.isfile(source):
            die("%s is missing from %s" % (name, vcrt_dir))
        shutil.copy2(source, os.path.join(dest, name))
        print("  %-24s %s   [VC runtime]" % (name, human(os.path.getsize(source))))

    os.makedirs(os.path.join(dest, "tools"), exist_ok=True)
    for tool, required in TOOLS:
        source = find_tool(tool)
        if source is None:
            if required:
                die("%s is required in a release and was not found in %s"
                    % (tool, " or ".join(TOOL_DIRS)))
            print("  tools/%-18s (not found, skipped)" % tool)
            continue
        shutil.copy2(source, os.path.join(dest, "tools", tool))
        print("  tools/%-18s %s" % (tool, human(os.path.getsize(os.path.join(dest, "tools", tool)))))
    copy_tool_dirs(os.path.join(dest, "tools"))

    # An empty folder does not survive a zip, so each carries a note. The
    # in-game updater skips these two folders when it installs a release.
    for folder, text in (
        ("game", "Put your extracted Fable II disc here, so that default.xex sits\r\n"
                 "in this folder - or let the setup screen extract your disc image\r\n"
                 "into it.\r\n"),
        ("dlc", "Optional. Put your DLC packages here; the setup screen installs\r\n"
                "them.\r\n"),
    ):
        os.makedirs(os.path.join(dest, folder), exist_ok=True)
        with open(os.path.join(dest, folder, "PUT_FILES_HERE.txt"), "w", newline="\r\n") as f:
            f.write(text)

    known = "\r\n\r\n".join(
        textwrap.fill(issue, width=76, initial_indent="  * ", subsequent_indent="    ")
        for issue in KNOWN_ISSUES) if KNOWN_ISSUES else "  none"
    title = README_TITLE.format(version=version)
    with open(os.path.join(dest, "README.txt"), "w", newline="\r\n") as f:
        f.write(README_TEMPLATE.format(
            title=title, underline="=" * len(title), sdk=sdk_version(), known_issues=known,
            built=datetime.datetime.now().strftime("%Y-%m-%d %H:%M"), host=platform.node()))
    with open(os.path.join(dest, "RELEASE_NOTES.md"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write("# fable2recomp v%s\n\n%s\n" % (version, notes))
    write_provenance(os.path.join(dest, "provenance.txt"), version)

    engine_dir = os.path.normcase(os.path.join(dest, "tools", "upscaler"))
    for dirpath, _, filenames in os.walk(dest):
        if os.path.normcase(dirpath).startswith(engine_dir):
            continue
        for name in filenames:
            if name in FORBIDDEN_EXCEPTIONS:
                continue
            if name.lower().endswith(FORBIDDEN_SUFFIXES):
                die("staged %s looks like game data - refusing to package it"
                    % os.path.join(dirpath, name))

    staged = write_sums(dest)
    total = sum(os.path.getsize(p) for p in staged)
    print("  %d files, %s" % (len(staged), human(total)))

    if not args.no_zip:
        make_zip(dest, os.path.join(args.releases_dir, "fable2recomp-v%s-win-amd64.zip" % version),
                 version)

    print("\n=== v%s is in %s ===" % (version, dest))
    print("Attach the zip to the GitHub release; it holds translated game code and no game data.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
