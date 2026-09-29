#!/bin/sh
# cemtog.sh TAG SCALE: travel to Bowerstone Cemetery; dump on/off/on mid-game (relative test folder); then walk and
# pan the camera for the 60 s burst.
T="$1"; L=/d/fable2_flash/ab110/logs; P=/d/fable2_flash/ab110/R113/pad_script.txt
rm -f $T.out $L/$T.log $L/$T.stand_start; rm -rf /d/fable2_flash/ab110/R113/dumptest
(sh claim.sh 9 "cemetery toggle $T" >/dev/null && FABLE2_WORLD_HEIGHT=720 FABLE2_SCALE="$2" PAD_ROUTE="wait:1" sh arm_leg.sh R113 $T 2 fairfax 330 > $T.out 2>&1; sh claim.sh release $T) > /dev/null 2>&1 &
for k in $(seq 1 4000); do [ -f $L/$T.stand_start ] && break; grep -q ABANDON $T.out 2>/dev/null && break; sleep 1; done
[ -f $L/$T.stand_start ] || { echo "$T VOID"; wait; exit 1; }
sleep 3; echo "$T travel: $(sh area.sh R113 $T 3 0)"
MOVE=""; k=0; while [ $k -lt 8 ]; do MOVE="${MOVE}l:0,1:2.5
r:1,0:1.2
l:0,-1:2.5
r:-1,0:1.2
"; k=$((k+1)); done
printf 'release
set:texture_dump_path=dumptest
set:texture_dump=true
wait:2
set:texture_dump=false
wait:2
set:texture_pack_path=../../../Fable 2 Portable/textures/pack
wait:3
set:texture_pack_path=
wait:2
set:texture_pack_path=../../../Fable 2 Portable/textures/pack
wait:2
%s' "$MOVE" > $P; sleep 5
tasklist | grep -qi fable2.exe || { echo "$T game gone before burst"; wait; exit 1; }
echo "$T burst from $(date +%T)"; powershell -NoProfile -ExecutionPolicy Bypass -File burst.ps1 "D:/fable2_flash/ab110/logs/sweep/${T}_burst" 60 > /dev/null
wait; tail -1 $T.out; echo "$T pack changes x$(grep -ac 'pack path changed' $L/$T.log), replaced $(grep -a 'texture_pack_replaced\|replacements' $L/$T.log | tail -1 | cut -c60-160) | dump on x$(grep -ac 'dumping to' $L/$T.log), files $(ls /d/fable2_flash/ab110/R113/dumptest 2>/dev/null | wc -l)"
