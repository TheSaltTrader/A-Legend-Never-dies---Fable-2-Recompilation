#!/bin/sh
# bridge_leg_ref1x.sh NAME "CVARS": a bridge_leg_series.sh leg with the PLUGIN REFERENCE UNENHANCED - texture pack and AI
# textures off, 1x internal resolution, no anisotropic override, texture_scale 1 - so the scene-against-scene reference
# is the game's own textures at the native path's resolution (REF1X, 2026-09-25: the enhanced reference drew DIFFERENT
# fern textures and finer mips; the "fern x0.72" was the pack, not the port). fable2_settings.cfg is backed up, edited,
# restored and verified by hash; the dump folder is moved to D:\fable2_flash\ngpu_dumps_20260924\NAME.
W="/c/users/renoi/claudecode/Fable 2 Recompile Xbox/wt-fable2-nativegpu"
B="$W/out/build/win-amd64-Release"
name=$1; cvars=$2
if tasklist | grep -qi fable2; then echo "fable2 running - stop"; exit 1; fi
bak="/d/fable2_flash/fable2_settings.cfg.bak_$name"
cp "$B/fable2_settings.cfg" "$bak" || exit 1
h0=$(sha256sum "$B/fable2_settings.cfg" | cut -d' ' -f1)
sed -i -e 's/^resolution_scale=.*/resolution_scale=1/' -e 's/^anisotropic=.*/anisotropic=0/' -e 's/^texture_pack=.*/texture_pack=0/' \
       -e 's/^texture_ai=.*/texture_ai=0/' -e 's/^texture_scale=.*/texture_scale=1/' "$B/fable2_settings.cfg"
[ -d "$B/ngpu_rts" ] && mv "$B/ngpu_rts" "/d/fable2_flash/ngpu_dumps_20260924/ngpu_rts_before_$name"
sh /d/fable2_flash/gameplay/bridge_leg_series.sh "$name" "$cvars" > "/d/fable2_flash/gameplay/$name.leg.txt" 2>&1
cp "$bak" "$B/fable2_settings.cfg"
[ "$h0" = "$(sha256sum "$B/fable2_settings.cfg" | cut -d' ' -f1)" ] && echo "settings restored, hash matches" || echo "SETTINGS HASH MISMATCH - restore from $bak"
[ -d "$B/ngpu_rts" ] && mv "$B/ngpu_rts" "/d/fable2_flash/ngpu_dumps_20260924/$name"
echo "$name done: $(tail -1 /d/fable2_flash/gameplay/$name.leg.txt)"
grep -h -i -m1 "internal scale" "$W"/out/$name*.log
