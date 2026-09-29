#!/bin/sh
# optb_series3.sh: the option-B / option-C validation series on the gate-fixed binary (2026-09-29 13:50), back to back;
# each leg re-checks the machine before launching (the leg script's tasklist check + claim.sh). SPL_BL_2 first (option
# C alone, gate-independent, the informative arm); OPTB_1b replaces OPTB_1 (void by the gate hole); OPTB_RC_1 = three
# region loads (the gate premise); OPTB_REV_1 = capture across the reveal (the toggle moment). Stops at a VOID.
run() { S="$1"; T="$2"; TUNE="$3"; B="${4:-45}"
  tasklist | grep -qi "fable2.exe\|ng2.exe" && { echo "$T: a game is running - stopping"; exit 1; }
  echo "$(date +%H:%M:%S) $T launching via $S ($TUNE)"
  sh "$S" "$T" "$TUNE" 300 "$B" 3 2>&1 | tail -2
  [ -f /d/fable2_flash/ab110/logs/$T.stand_start ] || { echo "$T VOID launch - stopping"; exit 1; }
  sleep 5
}
run uwpans.sh   SPL_BL_2   "ngpu_split_after_load=false;readback_resolve_split_before_load=true"
run uwpans.sh   OPTB_1b    "-"
run uwpans2.sh  OPTB_RC_1  "-"
run uwpans.sh   OPTB_2     "-"
run uwpans.sh   SPL_BL_3   "ngpu_split_after_load=false;readback_resolve_split_before_load=true"
run uwpanrev.sh OPTB_REV_1 "-" 90
echo "$(date +%H:%M:%S) series done"
