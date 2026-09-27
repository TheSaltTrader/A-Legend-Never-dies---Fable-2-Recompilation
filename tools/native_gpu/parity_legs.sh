#!/bin/sh
# One parity table for one scene: plugin (native OFF) and native (defaults)
# INTERLEAVED, three each, every diagnostic OFF in both arms, the overrun-frame
# dump ON in both arms (one clock read a frame, symmetric, stated).
#
#   parity_legs.sh <tag> <save_index> <expect_region> [world_seconds]
#   e.g. parity_legs.sh PBw 1 bowerstone_cemetery 75
#        parity_legs.sh PT  2 bowerstone_market   75
#
# The rule it serves (PERF_BASELINE_2026-09-22.md, "THE PARITY DECISION RULE"):
# same save, same stand, same settings, back to back, n >= 3 per condition; the
# leg runner asserts the region from the game's own log line and refuses a
# wrong scene; parity_read.py applies the verdicts. A cvar value is not a
# workload: the census and the texture dumps are OFF here in both arms because
# the native arm did more census work under the same cvar (2026-09-23).
set -u
TAG="$1"; SAVE="$2"; REGION="$3"; SECS="${4:-75}"
HERE="$(cd "$(dirname "$0")" && pwd)"
COMMON="ngpu_census=false;ngpu_dump_textures=0;ngpu_dump_rts=0;ngpu_log_consts=0;ngpu_overrun_ms=18"
unset FABLE2_PROFILE
export SAVE_INDEX="$SAVE" EXPECT_REGION="$REGION"
i=1
while [ $i -le 3 ]; do
  sh "$HERE/ab_untile_leg.sh" "${TAG}_off$i" 1 "$SECS" "ngpu_shadow=false;$COMMON" 2>&1 | tail -3
  rc=$?; sleep 4
  sh "$HERE/ab_untile_leg.sh" "${TAG}_on$i" 1 "$SECS" "$COMMON" 2>&1 | tail -3
  sleep 4
  i=$((i+1))
done
echo "[parity $TAG] done; read with: python parity_read.py out ${TAG}_off ${TAG}_on 6"
