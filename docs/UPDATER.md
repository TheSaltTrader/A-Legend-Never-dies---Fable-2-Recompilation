# The in-game updater (0.2.0 - 0.2.2)

The port keeps itself current from the project's GitHub releases page. This
is what it does, what it must never do, how it is tested, and what its first
live runs found. Code: `src/fable2_update.h`, `src/fable2_update.cpp`; the
app wiring is in `src/fable2_app.h` (search `update_`), the settings page in
`src/fable2_menu.cpp` (`DrawUpdatesSection`).

## What happens at launch

1. The app creates the `UpdateOverlay` with the other always-on overlays and,
   if `update_check` is on (the default), calls `StartCheck(false)`. The
   check runs on its own thread and never holds the game: it asks
   `https://api.github.com/repos/<repo>/releases/latest` with WinHTTP,
   3 seconds at most, and reads two things out of the answer - `tag_name`
   and the asset named `fable2recomp-*-win-amd64.zip` (its URL and size).
   The JSON reading is a few string searches bounded to the `assets` array;
   the answer is small and its field order is stable.
2. `CompareVersions` puts the tag against `FABLE2_VERSION` (the VERSION file,
   baked in by CMake). Older or equal: one log line, nothing shown. Newer but
   equal to `update_skip`: one log line, nothing shown. Newer: the panel.
3. The panel offers **Update now**, **Not now**, **Skip this version**, and a
   **Release notes** link. Nothing downloads without the first button.
4. Update now: `DownloadToFile` streams the zip into `update/<name>.part`
   beside the executable, renames it into place, and the size is compared
   with what the release lists; a mismatch is refused, not installed.
5. `InstallReleaseZip`: Windows' own `tar.exe` (every Windows 10 and 11 has
   it) extracts the zip into `update/stage/`; then each file, except the
   `game/` and `dlc/` placeholders, is moved into place. A file already
   there is renamed to `<name>.old` first (Windows allows renaming a file in
   use, which is how the running executable and DLLs are replaced), and the
   list of renamed files is written to `update/renamed.txt` BEFORE the first
   move. Any failure puts everything back.
6. **Restart now** is a separate click. `RelaunchSelf` starts a new
   `fable2.exe` with this process's own command line (minus the program
   token) and `FABLE2_WAIT_PID` set to this process; the new one waits for
   the old to exit (10 s cap) before it opens its window, so one window is on
   screen at a time. The old process leaves by Escape's path, which saves the
   settings first.
7. The new process's `CleanupAfterUpdate` deletes the `.old` files listed in
   `renamed.txt` and the zip, retrying for five seconds in the background
   because the old process may still be closing. What it cannot remove waits
   for the next start.

## What it never touches

A release zip carries the executable, the two SDK DLLs, the VC runtime, the
controller database, the texture tools and the AI engine, the README, the
release notes, SHA256SUMS and provenance. It never carries `game/`, `dlc/`,
`user/`, `fable2_settings.cfg`, `fable2.toml`, the texture folder, the shader
cache or the logs, so an update cannot touch them by construction. The
packager (`tools/make_release.py`) refuses to stage anything that looks like
game data; the census's ASSEMBLY sweep (`tools/lodestone_census.py --package
<folder>`) checks the staged folder the same way.

## Settings

- `update_check` - "Check for updates at start" on the F10 screen, Updates
  section; default on. "Check now" beside it asks on demand and reports "Up
  to date (vX)" or "Version Y is available" in place.
- `update_skip` - written by "Skip this version"; cleared by "Offer it again"
  on the same page. A later version is offered as usual.

## Developer switches (environment variables)

- `FABLE2_UPDATE_PRETEND_VERSION=0.1.0` - the build reports that version to
  the comparison, so the "available" path can be exercised against the real
  releases. Said once in the log.
- `FABLE2_UPDATE_AUTO=1` - the panel presses Update now and Restart now
  itself. Together with the one above this is the whole path with nobody at
  the mouse. `RelaunchSelf` clears both before starting the new process, so
  a pretend-old build cannot update itself forever.
- `FABLE2_QUIT_AFTER=<s>` (the app's existing quit seam) ends each process
  by Escape's path, for scripted runs. It does not fire while the setup
  screen is up; a scripted run that lands there must be closed by PID.

## Tests, and what they found

`tools/update_e2e_test.ps1` runs the whole path against the real releases:
it hashes the executable, launches with the two switches, watches the log
for `[update]` lines, waits for both processes to end, then reports the
executable's hash against the release's, any `.old` left, the `update/`
folder, and the restarted process's log. `tools/remember_folder_test.ps1`
launches once with `--game_data_root` and once with no arguments.

Run on 2026-09-13, against the published releases:

| run | from | to | result |
|---|---|---|---|
| 1 | build pretending v0.1.0 | v0.2.0 | check 0.3 s, 77 MB in 3 s, 30 files installed, 4 moved aside, restart, 4 of 4 cleaned, "up to date" |
| 2 | build (0.2.1) pretending v0.1.0 | v0.2.0 | as above with 30 moved aside; the restart kept its arguments |
| 3 | a fresh copy of the v0.2.0 release folder | v0.2.1 | real update, no pretence; 30 files, restart, cleaned, "up to date" |
| 4 | build (0.2.2), folder on the command line, then no arguments | - | run 1 remembered the folder; run 2 booted straight into the game |

Two things the first run found:

- **The restart dropped the command line** (fixed in 0.2.1). A launch from
  a shortcut or a script carries `--game_data_root` and `--log_file`; the
  restarted process had neither and opened the setup screen as if the game
  had never been configured. `RelaunchSelf` now forwards the arguments.
- **The settings never held the game folder** (fixed in 0.2.2). Every
  launch on this machine passed the folder on the command line, so
  `game_path` stayed empty and `configured` stayed 0, and any start without
  arguments - a restart from the v0.2.0 executable, a plain double-click -
  asked for the folder. The player read that as "all settings were lost";
  nothing was. `OnConfigurePaths` now writes a usable command-line folder
  into the settings the first time it arrives, so a plain start finds the
  game where it was. Shift at launch and a missing folder still bring the
  setup screen back.

One thing the tests did not cover: a release unzipped into a brand-new folder
has no settings file and starts fresh. A copy of the settings in the user's
profile, offered on such a first run, would close that; it has not been
built.

## Cutting a release

```
python tools/make_release.py --build      # or without --build for the current build
git add ... && git commit && git tag -a vX.Y.Z -m "..."
git push origin tu1:main && git push origin vX.Y.Z
gh release create vX.Y.Z --title "A Legend Never Dies vX.Y.Z" --notes-file notes.md ../Releases/fable2recomp-vX.Y.Z-win-amd64.zip
```

The packager refuses a version without a CHANGELOG section, an executable
older than the sources, a DLL that is not the deployed SDK pair
(`../RexBlue/win-amd64/bin`, which holds this port's own source-built pair),
and anything that looks like game data. The updater looks for the tag and
the `-win-amd64.zip` asset, nothing else, so the zip name and the tag format
are part of the contract.
