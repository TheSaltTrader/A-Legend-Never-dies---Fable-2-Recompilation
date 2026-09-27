#!/bin/sh
# Per-slot whitening sweep on the lake PS E33859F0, measured IN-SCENE: "lake adds" = the lake region after the
# frame's final lake group minus just before it, at dump frames 1080 / 2160 / 3240. Reflection fix on (reflection only).
# usage: slotwalk.sh "0 1 2 ..."   (slot -1 = no whitening, the baseline)
set -u
B="/c/users/renoi/claudecode/Fable 2 Recompile Xbox/wt-fable2-nativegpu/out/build/win-amd64-Release"
T=/c/Users/renoi/.claude/jobs/6397a53c/tmp
DST=/d/fable2_flash/ngpu_dumps_20260924
ps=$(python -c "x=0xE33859F0; print(x-2**32)")
for s in $1; do
  if [ "$s" = "-1" ]; then tag=SLOTW_base; extra=""; else
    tag=SLOTW_$s; mask=$(python -c "print((0xFFFFFFFF & ~(1<<$s)) - 2**32)"); extra=";ngpu_tex_slots=$mask;ngpu_tex_slots_only_ps=$ps"; fi
  mkdir -p "$B/ngpu_rts"; rm -rf "$B/ngpu_rts_walk_tmp"; mv "$B/ngpu_rts" "$B/ngpu_rts_walk_tmp"; mkdir -p "$B/ngpu_rts"
  mkdir -p "$DST/$tag" && mv "$B/ngpu_rts_walk_tmp"/* "$DST/$tag/" 2>/dev/null; rmdir "$B/ngpu_rts_walk_tmp" 2>/dev/null
  sh /d/fable2_flash/gameplay/bridge_leg_series.sh $tag "ngpu_hooked_draws=false;ngpu_edram_clear_alias=true;ngpu_edram_clear_alias_only=67240216;ngpu_dump_rts=1080;ngpu_dump_scene_draws=1;ngpu_dump_scene_from=1490;ngpu_dump_scene_to=1570;ngpu_truth_keep=true;ngpu_dump_res_min_w=4096$extra" > $T/$tag.leg 2>&1
  (cd "$B/ngpu_rts" && python - "$tag" <<'EOF'
import numpy as np, glob, re, sys
out=[]
for fr in (1080,2160,3240):
    rows=[]
    for f in sorted(glob.glob(f"f{fr:06d}_scene_seq*_rt_14010500_00030000_1280x720*.f16")):
        m=re.search(r"seq(\d+)_after_VS(\w+)_PS(\w+)_rt",f)
        h=np.nan_to_num(np.fromfile(f,dtype=np.float16).astype(np.float32).reshape(720,1280,4)[222:292,700:1200,:3]).mean()
        rows.append((int(m.group(1)),m.group(3)[:8],h))
    idx=[i for i,r in enumerate(rows) if r[1]=="E33859F0"]
    if not idx or idx[0]==0: out.append(f"{fr}: no lake group in window"); continue
    b=rows[idx[0]-1][2]; a=rows[idx[-1]][2]; out.append((fr,b,a-b))
txt=[]; adds={}
for o in out:
    if isinstance(o,str): txt.append(o)
    else: txt.append(f"{o[0]}: bed {o[1]:.3f} adds {o[2]:+.3f}"); adds[o[0]]=o[2]
r = adds[3240]/adds[1080] if 1080 in adds and 3240 in adds and abs(adds[1080])>1e-3 else float('nan')
print(sys.argv[1], " | ".join(txt), f"| ratio add3240/add1080 {r:.2f} (baseline 0.21; bound >= 0.6)")
EOF
  ) | tee -a $T/slotwalk.results
done
