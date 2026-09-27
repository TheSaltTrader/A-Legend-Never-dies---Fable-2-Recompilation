#!/bin/sh
# One A/B leg of the texture-untile dump comparison (TEXTURE_CONVERSION_LIFT_PLAN
# run-validation recipe). Usage: ab_untile_leg.sh <tag> <ngpu_use_sdk_untile 0|1> [world_seconds]
#
# Rules enforced here (each paid for in this project):
#  - refuses if ANY fable2.exe runs (one game at a time; the person's session wins);
#  - refuses if ~/.game-test-lock is held by another session with a future until=;
#  - writes its own lock with a REAL until= (the user is asleep; no open-ended hold);
#  - FABLE2_TUNE items separated by ';' and the run VERIFIES its overrides applied;
#  - every leg has its own log; the dump dir is emptied BEFORE the run;
#  - kills ONLY the PID it started, never by name or window title;
#  - non-blocking for the night: every wait has a deadline, a stuck leg is ABANDONED.
set -u
TAG="$1"; SDK="$2"; WORLD_SECS="${3:-75}"; EXTRA="${4:-}"
WT="/c/users/renoi/claudecode/Fable 2 Recompile Xbox/wt-fable2-nativegpu"
# BUILD_DIR (env): run another checkout's build - e.g. a rebuild of the exact commit
# an earlier table used, so a comparison differs by one thing. POSIX path; the
# Windows form is derived below. Default: this worktree's Release build.
BD="${BUILD_DIR:-$WT/out/build/win-amd64-Release}"
GAME="C:\\users\\renoi\\claudecode\\Fable 2 Recompile Xbox\\fable2recomp\\game"
OUT="$WT/out"
BDW="$(cygpath -w "$BD")"
LOG="$OUT/$TAG.log"
LOGW="C:\\users\\renoi\\claudecode\\Fable 2 Recompile Xbox\\wt-fable2-nativegpu\\out\\$TAG.log"
LOCK="$HOME/.game-test-lock"
ME="claudecode-4c"
TUNE="ngpu_census=true;ngpu_census_report_secs=10;ngpu_shadow=true;ngpu_native_draws=true;ngpu_dump_textures=64;ngpu_use_sdk_untile=$( [ "$SDK" = "1" ] && echo true || echo false )${EXTRA:+;$EXTRA}"
EXPECT=6

say() { echo "[$TAG $(date +%H:%M:%S)] $*"; }
waitfor() { i=0; while [ $i -lt "$3" ]; do grep -q "$1" "$2" 2>/dev/null && return 0; sleep 1; i=$((i+1)); done; return 1; }
release_lock() { printf 'session=%s game=none until=now note=machine-free (A/B untile leg %s done)\n' "$ME" "$TAG" > "$LOCK"; }
kill_mine() {
  # Only the PID this leg started. Ask first, then stop it; wait for the exit.
  [ -z "${PID:-}" ] && return 0
  powershell -NoProfile -Command "\$p = Get-Process -Id $PID -ErrorAction SilentlyContinue; if (\$p) { \$null = \$p.CloseMainWindow(); for (\$i = 0; \$i -lt 20; \$i++) { if (\$p.HasExited) { break }; Start-Sleep -Milliseconds 500 }; if (-not \$p.HasExited) { Stop-Process -Id $PID -Force } }" > /dev/null 2>&1
  sleep 2
}

# --- guards ---------------------------------------------------------------
if tasklist 2>/dev/null | grep -qi "^fable2.exe"; then say "ABORT: a fable2.exe is already running - not launching beside it"; exit 2; fi
if [ -f "$LOCK" ]; then
  L=$(cat "$LOCK")
  LS=$(echo "$L" | sed -n 's/.*session=\([^ ]*\).*/\1/p'); LG=$(echo "$L" | sed -n 's/.*game=\([^ ]*\).*/\1/p'); LU=$(echo "$L" | sed -n 's/.*until=\([^ ]*\).*/\1/p')
  case "$L" in *machine-free*) LG=none;; esac   # a holder that declared the machine free has released it
  if [ "$LS" != "$ME" ] && [ "$LG" != "none" ] && [ "$LU" != "now" ]; then
    NOWM=$(date +%H:%M)
    if [ "$LU" \> "$NOWM" ]; then say "ABORT: lock held by $LS for $LG until $LU (now $NOWM)"; exit 2; fi
  fi
fi
UNTIL=$(date -d "+$((WORLD_SECS + 300)) seconds" +%H:%M 2>/dev/null || date +%H:%M)
printf 'session=%s game=fable2 until=%s note=A/B untile dump leg %s (sdk_untile=%s) plugin=%s runtime=%s, kills only its own PID\n' "$ME" "$UNTIL" "$TAG" "$SDK" "$(sha256sum "$BD/rexgpu-xenos.dll" | cut -c1-12)" "$(sha256sum "$BD/rexruntime.dll" | cut -c1-12)" > "$LOCK"

# --- clean inputs -----------------------------------------------------------
rm -f "$BD/pad_script.txt" "$LOG"
rm -rf "$BD/ngpu_textures"
mkdir -p "$OUT"

# --- launch (Start-Process inherits this shell's env; WMI would drop it) -----
export FABLE2_TUNE="$TUNE"
export FABLE2_HUD=1
unset FABLE2_PAD_SCRIPT
PID=$(powershell -NoProfile -Command "\$r = Start-Process -FilePath '$BDW\fable2.exe' -ArgumentList @('--game_data_root', '\"$GAME\"', '--log_file', '\"$LOGW\"', '--log_level', 'info', '--log_flush_interval', '1') -WorkingDirectory '$BDW' -PassThru; \$r.Id" | tr -d '\r')
case "$PID" in ''|*[!0-9]*) say "ABORT: launch failed (pid '$PID')"; release_lock; exit 3;; esac
say "launched pid $PID  tune=$TUNE  save card=${SAVE_INDEX:-1}"
# BINARY PROVENANCE, recorded with the leg (2026-09-23): size, mtime and SHA-256 of
# the exe and both DLLs the leg ran, so "which binary was that table?" is a
# lookup and not a recollection. The recompiler is not deterministic, so a
# rebuilt commit is "same source, probably same binary" - the hash says which.
{ for f in fable2.exe rexgpu-xenos.dll rexruntime.dll; do
    python -c "import hashlib,sys,os,time; d=open(sys.argv[1],'rb').read(); print(sys.argv[2], len(d), time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(os.path.getmtime(sys.argv[1]))), hashlib.sha256(d).hexdigest())" "$BD/$f" "$f"
  done; echo "build_dir $BD"; echo "git $(git -C "$WT" rev-parse --short HEAD 2>/dev/null) (this worktree's HEAD; the build dir may belong to another checkout - see build_dir)"; } > "$OUT/$TAG.artifact.txt" 2>&1
say "artifacts recorded in out/$TAG.artifact.txt: $(head -1 "$OUT/$TAG.artifact.txt" | cut -c1-90)"

# --- title -> menu -> world ---------------------------------------------------
if ! waitfor "scene none -> menu" "$LOG" 120; then say "ABANDON: no title/menu in 120 s"; kill_mine; release_lock; exit 4; fi
applied=$(grep -c "Tuning override (FABLE2_TUNE)" "$LOG" 2>/dev/null)
if [ "$applied" -lt "$EXPECT" ]; then say "ABANDON: only $applied of $EXPECT tune overrides applied - this run would measure the default build"; kill_mine; release_lock; exit 5; fi
say "menu reached, $applied overrides applied"
sleep 4
# SAVE_INDEX (env, default 1): which save CARD to load, 1-based. The main menu is
# New Game / Continue / ..., so ONE down selects Continue; the saves are then a
# CARD FAN navigated with RIGHT (read off the screen with the vision tools,
# 2026-09-23, after a blind downs script loaded the wrong save and then nothing).
# 1 = Hero 1 / Bower Lake, 2 = Hero 2 / Bowerstone Market. The region assertion
# below is what catches a mis-selection; never the key presses alone.
{ printf 'a:0.2\nwait:3\ndown:0.2\nwait:0.8\na:0.2\nwait:3\n'; i=1; while [ $i -lt "${SAVE_INDEX:-1}" ]; do printf 'right:0.2\nwait:0.6\n'; i=$((i+1)); done; printf 'a:0.2\n'; } > "$BD/pad_script.txt"
if ! waitfor "scene loading -> world" "$LOG" 150; then
  printf 'a:0.2\n' > "$BD/pad_script.txt"
  if ! waitfor "scene loading -> world" "$LOG" 90; then say "ABANDON: world never loaded"; kill_mine; release_lock; exit 6; fi
fi
# SCENE ASSERTION: N key presses into a menu is open-loop, so the leg reads the
# game's own "region '<name>' is loading" line and refuses a stand in the wrong
# scene - a REPORTED outcome, never a tidy table about the wrong place.
# EXPECT_REGION (env, default bowerstone_cemetery) names the expected one.
EXPECT="${EXPECT_REGION:-bowerstone_cemetery}"
if ! grep -q "region '$EXPECT' is loading" "$LOG"; then
  seen=$(grep -o "region '[a-z_0-9]*' is loading" "$LOG" | sort -u | tr '\n' ' ')
  say "ABANDON: WRONG SCENE - expected region '$EXPECT', the log loaded: ${seen:-none}"
  kill_mine; release_lock; exit 7
fi
say "world loaded in region '$EXPECT' (asserted from the log); standing for $WORLD_SECS s"
# PAD_ROUTE (optional, 2026-09-26): a pad-script route (one command per line, the game's own pad_script.txt channel)
# played INSTEAD of the stand's wait - movement legs. Unset: the stand waits, as before.
if [ -n "${PAD_ROUTE:-}" ]; then printf '%s\n' "$PAD_ROUTE" > "$BD/pad_script.txt"; say "pad route queued ($(printf '%s\n' "$PAD_ROUTE" | wc -l) commands)"
else printf 'wait:%s\n' "$WORLD_SECS" > "$BD/pad_script.txt"; fi
sleep "$WORLD_SECS"

# --- stop MY process, then collect ------------------------------------------
kill_mine
release_lock
n=$(ls "$BD/ngpu_textures" 2>/dev/null | wc -l)
rm -rf "$OUT/ngpu_$TAG"
[ -d "$BD/ngpu_textures" ] && mv "$BD/ngpu_textures" "$OUT/ngpu_$TAG"
cp "$BD/ngpu_census.txt" "$OUT/ngpu_census_$TAG.txt" 2>/dev/null
say "done: $n dump files -> out/ngpu_$TAG; log $LOG"
grep -o "UNTILE.*SELF-TEST[^:]*: [A-Z]*" "$LOG" | sort | uniq -c | sed "s/^/    /"
grep "guest fps" "$LOG" | tail -2 | grep -o "[0-9.]* guest fps.*" | sed "s/^/    /"
