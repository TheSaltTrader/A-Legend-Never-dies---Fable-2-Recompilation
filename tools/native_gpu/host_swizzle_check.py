"""host_swizzle_check.py: diff the native kHostSw table against the plugin's producer table (rexglue
d3d12/texture_cache.cpp host_formats), keyed by FORMAT NAME via xenos.h's TextureFormat enum - a positional
compare misaligned by one missing entry on each side and reported 35 false differences."""
import re, sys
R = r"C:/Users/renoi/claudecode/Fable 2 Recompile Xbox/"
x = open(R + "rexglue-src/include/rex/graphics/xenos.h", encoding="utf-8", errors="replace").read()
e = x[x.find("enum class TextureFormat"):]; e = e[:e.find("};")]
num = {n: int(v) for n, v in re.findall(r"\b(k_[A-Za-z0-9_]+)\s*=\s*(\d+)", e)}
src = open(R + "rexglue-src/src/graphics/d3d12/texture_cache.cpp", encoding="utf-8", errors="replace").read()
parts = re.split(r"\n\s*//\s*(k_[A-Za-z0-9_]+)", src)
code = {"RGBA": 0, "RRRR": 1, "RGGG": 2, "RGBB": 3}
ref = {}
for i in range(1, len(parts) - 1, 2):
    m = re.search(r"XE_GPU_TEXTURE_SWIZZLE_([A-Z]+)", parts[i + 1][:600])
    if parts[i] in num and m and num[parts[i]] not in ref: ref[num[parts[i]]] = code[m.group(1)]
mine_src = open(R + "wt-fable2-nativegpu/src/native_gpu_present.cpp", encoding="utf-8").read()
m = re.search(r"kHostSw\[64\] = \{(.*?)\};", mine_src, re.S)
vals = [int(v) for v in re.findall(r"-?\d+", re.sub(r"//[^\n]*", "", m.group(1)))]
diff = [(f, ref[f], vals[f]) for f in sorted(ref) if ref[f] != vals[f]]
print(f"formats compared {len(ref)} of 64; differences {diff}; producer lists none for {[f for f in range(64) if f not in ref]}")
sys.exit(1 if diff else 0)
