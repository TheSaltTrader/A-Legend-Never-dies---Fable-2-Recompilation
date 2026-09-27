"""Run XenosRecomp OFFLINE on every shader container the live census dumped.

Input:  <build>/ngpu_shader_census/<ucode>_<v|p>.xvu   (container + physical block, as the JIT feeds it)
Output: <scratch>/xr_hlsl/<ucode>_<v|p>.hlsl (+ .hlsl.layout)  named by the SDK's ucode hash,
        so binding_diff.py pairs them with <ucode>_<v|p>.bindings.txt with no address mapping.

THIS IS ALSO THE PROVENANCE CHECK FOR THE RUNAWAY CLASS: XenosRecomp once emitted 32-34 GB
of HLSL from an unbounded control-flow walk, and a walk that terminates still emits wrong
blocks at a plausible size. Every translation runs under a wall-clock timeout and an
output-size cap; any shader that times out, is killed, or exceeds the cap is recorded as
RUNAWAY-CLASS with its size. Those are the first suspects for the striped material.
"""
import os, sys, subprocess, time, glob, shutil

XR = r"C:\Users\renoi\ClaudeCode\NativeGPU\build\xenosrecomp\XenosRecomp\XenosRecomp.exe"
HDR = r"C:\Users\renoi\ClaudeCode\NativeGPU\fable2_shader_common.h"
BD = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\wt-fable2-nativegpu\out\build\win-amd64-Release"
SRC = os.path.join(BD, "ngpu_shader_census")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "xr_hlsl")
TIMEOUT_S = 60
CAP_BYTES = 64 << 20   # a real HLSL is tens of KB; 64 MB is already absurd

def main():
    os.makedirs(OUT, exist_ok=True)
    xvus = sorted(glob.glob(os.path.join(SRC, "*.xvu")))
    ok = fail = runaway = 0
    report = []
    t_all = time.time()
    for xvu in xvus:
        base = os.path.basename(xvu)[:-4]          # <ucode>_<v|p>
        hlsl = os.path.join(OUT, base + ".hlsl")
        if os.path.exists(hlsl) and os.path.getsize(hlsl) > 0:
            ok += 1
            continue
        for stale in (hlsl, hlsl + ".layout"):
            if os.path.exists(stale):
                os.remove(stale)
        t0 = time.time()
        status = "ok"
        try:
            p = subprocess.Popen([XR, xvu, hlsl, HDR], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            # poll: kill on timeout OR when the output grows past the cap
            while True:
                rc = p.poll()
                size = os.path.getsize(hlsl) if os.path.exists(hlsl) else 0
                if rc is not None:
                    break
                if time.time() - t0 > TIMEOUT_S:
                    p.kill(); status = "RUNAWAY-CLASS timeout"; break
                if size > CAP_BYTES:
                    p.kill(); status = f"RUNAWAY-CLASS output {size >> 20} MB"; break
                time.sleep(0.05)
            err = p.stderr.read().decode(errors="replace")[:200] if p.stderr else ""
            if status == "ok" and (p.returncode != 0 or not os.path.exists(hlsl) or os.path.getsize(hlsl) == 0):
                status = f"failed rc={p.returncode} {err.strip()}"
        except Exception as exc:
            status = f"failed {exc}"
        dt = time.time() - t0
        size = os.path.getsize(hlsl) if os.path.exists(hlsl) else 0
        if status == "ok":
            ok += 1
        elif status.startswith("RUNAWAY"):
            runaway += 1
            report.append(f"{base}: {status} after {dt:.1f}s")
            if os.path.exists(hlsl): os.remove(hlsl)
        else:
            fail += 1
            report.append(f"{base}: {status} ({dt:.1f}s, {size} bytes)")
    print(f"XenosRecomp offline: {len(xvus)} containers, ok {ok}, failed {fail}, RUNAWAY-CLASS {runaway}, {time.time() - t_all:.0f}s")
    for r in report:
        print("  " + r)

if __name__ == "__main__":
    main()
