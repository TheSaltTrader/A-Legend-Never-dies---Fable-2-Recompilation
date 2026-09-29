#!/bin/sh
# area.sh ARM LOGTAG REGION_INDEX DEST_INDEX: fast-travel via D-pad up -> Regions -> region -> destination.
# The travel is detected by the loading map's camera ("[cam] loading map" line): a travel inside the current region
# loads no region and writes no "[stage] region" line (2026-09-27 23:15 - it read as NO TRAVEL). Then
# "scene loading -> world" is awaited (a miss is REPORTED, not skipped); then look around and walk. One line out.
# Up to three attempts, each closing any open menu first.
ARM="$1"; TAG="$2"; R="$3"; D="$4"
P="/d/fable2_flash/ab110/$ARM/pad_script.txt"; A="/d/fable2_flash/ab110/logs/$TAG.log"
rep() { k=0; while [ $k -lt "$2" ]; do printf '%s\nwait:0.8\n' "$1"; k=$((k+1)); done; }
m0=$(grep -ac "\[cam\] loading map" "$A"); s0=$(grep -ac "\[stage\] region '" "$A"); w0=$(grep -ac "scene loading -> world" "$A")
try=1
while :; do
  printf 'release
b\nwait:0.8\nb\nwait:0.8\nb\nwait:0.8\nb\nwait:1.5\n' > "$P"; sleep 6
  { printf 'up\nwait:2.5\n'; rep down 4; printf 'a\nwait:2.5\n'; rep down "$R"; printf 'a\nwait:3\n'; rep down "$D"; printf 'a\nwait:1\n'; } > "$P"
  i=0; while [ $i -lt 30 ]; do m=$(grep -ac "\[cam\] loading map" "$A"); [ "$m" -gt "$m0" ] && break; sleep 2; i=$((i+1)); done
  [ "$m" -gt "$m0" ] && break
  [ $try -ge 3 ] && { echo "R$R D$D | NO TRAVEL after 3 tries (no loading map) | $(date +%H:%M:%S)"; exit 1; }
  try=$((try+1))
done
i=0; while [ $i -lt 45 ]; do w=$(grep -ac "scene loading -> world" "$A"); [ "$w" -gt "$w0" ] && break; sleep 2; i=$((i+1)); done
world=$([ "$w" -gt "$w0" ] && echo world-detected || echo WORLD-NOT-DETECTED)
s=$(grep -ac "\[stage\] region '" "$A")
reg=$([ "$s" -gt "$s0" ] && grep -aoh "\[stage\] region '[a-z_0-9]*'" "$A" | tail -1 | grep -oE "'[a-z_0-9]*'" || echo same-region)
t=$(date +%H:%M:%S)
sleep 6
printf 'r:1,0:4\nl:0,1:8\nr:-1,0:3\nl:0,1:6\nr:0.6,0:3\nwait:1\n' > "$P"
sleep 27
echo "R$R D$D | $reg | $world | $t | try $try"
