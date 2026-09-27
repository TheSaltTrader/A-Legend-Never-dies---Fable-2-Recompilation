#include "fable2_profiler.h"
#include "fable2_perf.h"  // StartPerfMonitor: an app anchor for the gap census

#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <rex/logging.h>

#include "generated/default/fable2_init.h"  // PPCFuncMappings: guest address -> host function

namespace fable2 {
namespace {

constexpr int kMaxFrames = 24;
constexpr int kSampleIntervalUs = 1000;
constexpr double kReportSeconds = 10.0;

struct Target {
  DWORD tid = 0;
  HANDLE handle = nullptr;
  std::string name;
  // Aggregates, touched only by the sampler thread and only while nothing is
  // suspended (see Sample()).
  std::unordered_map<uint64_t, uint32_t> leaf;       // rip -> samples
  std::unordered_map<uint64_t, uint32_t> guest_fn;   // host entry of first sub_ frame -> samples
  std::unordered_map<uint64_t, uint32_t> waiter;     // first frame above ntdll/kernel32 -> samples
  std::unordered_map<uint64_t, uint32_t> frame_hit;  // every frame's rip -> samples (inclusive)
  uint32_t samples = 0;         // on-CPU samples: the thread ran since the last look
  uint32_t blocked = 0;         // wall-clock samples where it had not run at all
  uint64_t last_cycles = 0;
  // The EXACT on-CPU measure: cycle deltas summed over the window, and the
  // scheduled time from GetThreadTimes at each report. The ran/blocked verdict
  // above is binary per 1 ms sample and aliases with the game's 1 ms sleeps
  // and yields: a thread sleeping in 1 ms steps reads "on CPU 100%"
  // (2026-09-23, GameThread: 100% on CPU with 64% of samples in its
  // NtYieldExecution wrapper). Accumulate, do not threshold (claudecode-76).
  uint64_t cycles_window = 0;
  uint64_t last_cpu_100ns = 0;
  uint32_t by_module[8] = {};  // see ModuleClass
  uint32_t no_guest_frame = 0;
};

enum ModuleClass { kGenerated, kApp, kRuntime, kGpuPlugin, kNtdll, kKernel32, kOther, kUnknown };
const char* kModuleNames[8] = {"guest code", "app", "rexruntime", "gpu plugin",
                               "ntdll (syscall)", "kernel32", "other", "unknown"};

// The generated functions, sorted by host address, for "which sub_ is this
// rip in". Built once from the codegen's table.
struct GuestFn {
  uint64_t host;
  uint32_t guest;
};
std::vector<GuestFn> g_guest_fns;
uint64_t g_guest_lo = 0, g_guest_hi = 0;  // host address range covered by generated code
uint64_t g_app_anchor = 0;                 // the app function that bounds it (see BuildGuestTable)
// Largest host extent a generated function is allowed: beyond it a rip is app
// code in a gap. 256 KB of host code is ~16K guest instructions; the gap census
// at startup lists the gaps this turns into app code, and the largest gap that
// is NOT an app region bounds the cap from below.
constexpr uint64_t kMaxGuestFnBytes = 0x40000;
uint64_t g_gap_count = 0, g_gap_bytes = 0, g_largest_gap = 0, g_gap_unanchored = 0;
std::string g_gap_detail;

HMODULE g_exe = nullptr, g_runtime = nullptr, g_gpu = nullptr, g_ntdll = nullptr, g_k32 = nullptr;

bool SymbolIsGenerated(uint64_t rip);  // defined below GuestFnFor; the sampler's per-rip rule
// The gap census's tri-state: 0 = no symbol at all (unresolved), 1 = a
// generated function (a public "sub_" symbol), 2 = an app function. Uncached;
// it runs twice at startup.
int SymbolKind(uint64_t rip) {
  alignas(SYMBOL_INFO) char sbuf[sizeof(SYMBOL_INFO) + 256] = {};
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(sbuf);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 255;
  DWORD64 disp = 0;
  if (!SymFromAddr(GetCurrentProcess(), rip, &disp, sym)) return 0;
  return std::strncmp(sym->Name, "sub_", 4) == 0 ? 1 : 2;
}
void BuildGuestTable() {
  if (!g_guest_fns.empty()) return;  // built once; the sampler and the crash log both ask
  for (const PPCFuncMapping* m = PPCFuncMappings; m->host != nullptr; ++m) {
    g_guest_fns.push_back({reinterpret_cast<uint64_t>(m->host), uint32_t(m->guest)});
  }
  std::sort(g_guest_fns.begin(), g_guest_fns.end(),
            [](const GuestFn& a, const GuestFn& b) { return a.host < b.host; });
  if (!g_guest_fns.empty()) {
    g_guest_lo = g_guest_fns.front().host;
    g_guest_hi = g_guest_fns.back().host + 0x100000;  // the last function's extent is unknown
    // The app's own code follows the generated code in this exe. With the
    // open-ended extent above, every app sample was classed as GUEST and
    // symbolised to the nearest preceding sub_: P1 (2026-09-23) reported
    // 65.7% of the render thread as "sub_832BD218" with its stacks in the
    // NVIDIA driver - that was the native GPU path. Bound the guest range by
    // the lowest app function this file can name; anything above it is app.
    // The bound is logged so a run states what it applied.
    const uint64_t app_anchor = reinterpret_cast<uint64_t>(&BuildGuestTable);
    if (app_anchor > g_guest_fns.back().host && app_anchor < g_guest_hi) g_guest_hi = app_anchor;
    g_app_anchor = app_anchor;
    // Gap census: consecutive generated functions further apart than the cap.
    // Those spans are app code (or one giant function - the count says which).
    // Each large gap is named by the function before it and checked for an
    // app anchor (a function from another app translation unit) inside it:
    // a large gap WITHOUT an anchor is a candidate giant generated function
    // that the cap would truncate - the defect with the sign flipped
    // (claudecode-76, 2026-09-23) - and is counted as such, not assumed away.
    const uint64_t anchors[] = {reinterpret_cast<uint64_t>(&BuildGuestTable),
                                reinterpret_cast<uint64_t>(&fable2::StartPerfMonitor),
                                reinterpret_cast<uint64_t>(&fable2::StartProfiler)};
    for (size_t i = 1; i < g_guest_fns.size(); ++i) {
      const uint64_t gap = g_guest_fns[i].host - g_guest_fns[i - 1].host;
      if (gap > g_largest_gap) g_largest_gap = gap;
      if (gap >= kMaxGuestFnBytes) {
        ++g_gap_count;
        g_gap_bytes += gap - kMaxGuestFnBytes;
        int anchored = 0;
        for (uint64_t a : anchors)
          if (a >= g_guest_fns[i - 1].host && a < g_guest_fns[i].host) ++anchored;
        // The symbol just past the cap settles a gap without an anchor: a sub_
        // there means one giant generated function (kept whole by
        // GuestFnFor's symbol rule); anything else is app code.
        // Tri-state on purpose: "not a sub_" is an APP verdict, not the absence
        // of one (the first build of this census counted an app verdict as
        // unresolved - P7, 2026-09-23). Only "no symbol at all" is unresolved.
        const int kind = SymbolKind(g_guest_fns[i - 1].host + kMaxGuestFnBytes + 16);
        const bool sym_gen = kind == 1;
        if (!anchored && kind == 0) ++g_gap_unanchored;
        if (g_gap_detail.size() < 600) {
          char b[200];
          std::snprintf(b, sizeof(b), "%safter sub_%08X: %llu KB (%s; symbol past the cap %s)",
                        g_gap_detail.empty() ? "" : ", ", g_guest_fns[i - 1].guest,
                        static_cast<unsigned long long>(gap / 1024),
                        anchored ? "holds an app anchor" : "no app anchor",
                        kind == 1 ? "is a sub_: ONE GIANT GENERATED FUNCTION, kept whole"
                        : kind == 2 ? "is an app symbol: app code"
                                    : "NO SYMBOL: unresolved");
          g_gap_detail += b;
        }
      }
    }
  }
}

// Does the nearest symbol at rip name a generated function ("sub_...")?
// Cached per rip: the sampler asks this only for rips beyond the extent cap.
// Returns false when there is no symbol information (the cap then stands).
bool SymbolIsGenerated(uint64_t rip) {
  static std::unordered_map<uint64_t, bool> cache;
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  auto it = cache.find(rip);
  if (it != cache.end()) return it->second;
  alignas(SYMBOL_INFO) char sbuf[sizeof(SYMBOL_INFO) + 256] = {};
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(sbuf);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 255;
  DWORD64 disp = 0;
  const bool ok = SymFromAddr(GetCurrentProcess(), rip, &disp, sym) != FALSE;
  const bool gen = ok && std::strncmp(sym->Name, "sub_", 4) == 0;
  // A failed lookup (symbols not initialised yet - the crash log builds the
  // table too) is not cached, so it cannot pin a wrong verdict on a rip.
  if (ok && cache.size() < 200000) cache.emplace(rip, gen);
  return gen;
}

// The generated function containing rip, or nullptr.
const GuestFn* GuestFnFor(uint64_t rip) {
  if (rip < g_guest_lo || rip >= g_guest_hi)
    return nullptr;
  auto it = std::upper_bound(g_guest_fns.begin(), g_guest_fns.end(), rip,
                             [](uint64_t v, const GuestFn& f) { return v < f.host; });
  if (it == g_guest_fns.begin())
    return nullptr;
  --it;
  // A rip is inside function i only if it lies within i's extent. The extent
  // is unknown, so cap it: the app's own translation units are linked in
  // GAPS between generated ones (P3, 2026-09-23: the profiler's code sits
  // inside the guest host range), and with no cap every app sample was
  // charged to the generated function that happened to precede its gap
  // ("sub_832BD218" at 65.7% of the render thread, offsets megabytes past
  // the function, stacks in the NVIDIA driver). The startup line counts the
  // gaps the cap turns into app code, so a run states what the rule moved.
  // Past the cap the SYMBOL decides (the PDB names app functions since the
  // build of 2026-09-23 09:33; generated ones are public "sub_" symbols): a
  // rip whose nearest symbol is still a sub_ is inside a giant generated
  // function (sub_82242F10 is 72,536 generated lines, ~1.15 MB of host code -
  // the cap alone truncated it, the sign-flipped defect); anything else is
  // app code linked in the gap. No symbol information at all keeps the cap.
  if (rip - it->host >= kMaxGuestFnBytes) return SymbolIsGenerated(rip) ? &*it : nullptr;
  return &*it;
}

ModuleClass ClassOf(uint64_t rip) {
  HMODULE mod = nullptr;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCSTR>(rip), &mod);
  if (!mod) return kUnknown;
  if (mod == g_exe) return GuestFnFor(rip) ? kGenerated : kApp;
  if (mod == g_runtime) return kRuntime;
  if (mod == g_gpu) return kGpuPlugin;
  if (mod == g_ntdll) return kNtdll;
  if (mod == g_k32) return kKernel32;
  return kOther;
}

std::string Describe(uint64_t rip) {
  if (const GuestFn* f = GuestFnFor(rip)) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "sub_%08X+%llx", f->guest, (unsigned long long)(rip - f->host));
    return buf;
  }
  HMODULE mod = nullptr;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCSTR>(rip), &mod);
  char modpath[MAX_PATH] = "?";
  if (mod) GetModuleFileNameA(mod, modpath, MAX_PATH);
  const char* base = modpath;
  for (const char* c = modpath; *c; ++c)
    if (*c == '\\' || *c == '/') base = c + 1;
  std::string name = base;
  const size_t dot = name.rfind('.');
  if (dot != std::string::npos) name.resize(dot);
  alignas(SYMBOL_INFO) char sbuf[sizeof(SYMBOL_INFO) + 256] = {};
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(sbuf);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 255;
  DWORD64 disp = 0;
  char out[400];
  if (SymFromAddr(GetCurrentProcess(), rip, &disp, sym)) {
    // The app's sources carry line tables since the build of 2026-09-23 09:33
    // (CMakeLists: -gline-tables-only on FABLE2_SOURCES), so a hot leaf names
    // its source line: "+4266" in a 19 KB function is not a place to look,
    // "present.cpp:6321" is. Inlined callees resolve to the line of the call.
    IMAGEHLP_LINE64 line = {};
    line.SizeOfStruct = sizeof(line);
    DWORD ldisp = 0;
    if (SymGetLineFromAddr64(GetCurrentProcess(), rip, &ldisp, &line) && line.FileName) {
      const char* fbase = line.FileName;
      for (const char* c = line.FileName; *c; ++c)
        if (*c == '\\' || *c == '/') fbase = c + 1;
      std::snprintf(out, sizeof(out), "%s!%s+%llx @%s:%lu", name.c_str(), sym->Name,
                    (unsigned long long)disp, fbase, (unsigned long)line.LineNumber);
      return out;
    }
    std::snprintf(out, sizeof(out), "%s!%s+%llx", name.c_str(), sym->Name, (unsigned long long)disp);
  } else {
    std::snprintf(out, sizeof(out), "%s+%llx", name.c_str(),
                  (unsigned long long)(mod ? rip - reinterpret_cast<uint64_t>(mod) : rip));
  }
  return out;
}

// One sample of one thread. NOTHING in here may allocate, log or take a lock
// while the target is suspended: if the target holds the heap or the logger,
// the sampler would wait for a thread it has stopped, forever. Frames go into
// a fixed array; aggregation happens after ResumeThread.
int Sample(Target& t, uint64_t* frames) {
  if (SuspendThread(t.handle) == DWORD(-1))
    return -1;
  CONTEXT ctx;
  std::memset(&ctx, 0, sizeof(ctx));
  ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
  int n = 0;
  if (GetThreadContext(t.handle, &ctx)) {
    // Unwind with the OS's own tables - no DbgHelp, no heap.
    for (; n < kMaxFrames && ctx.Rip != 0; ++n) {
      frames[n] = ctx.Rip;
      DWORD64 image_base = 0;
      RUNTIME_FUNCTION* fn = RtlLookupFunctionEntry(ctx.Rip, &image_base, nullptr);
      if (!fn) {
        // Leaf function without unwind info: return address is at rsp.
        if (!ctx.Rsp) break;
        uint64_t ret = 0;
        __try {
          ret = *reinterpret_cast<const uint64_t*>(ctx.Rsp);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
          ret = 0;
        }
        if (!ret) break;
        ctx.Rip = ret;
        ctx.Rsp += 8;
        continue;
      }
      void* handler_data = nullptr;
      DWORD64 establisher = 0;
      __try {
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, ctx.Rip, fn, &ctx, &handler_data, &establisher,
                         nullptr);
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        break;
      }
    }
  }
  ResumeThread(t.handle);
  return n;
}

std::atomic<bool> g_run{false};
std::thread g_thread;

std::vector<std::string> WantedNames() {
  std::vector<std::string> out;
  const char* env = std::getenv("FABLE2_PROFILE");
  if (!env || !*env) return out;
  std::string s = env;
  if (s == "1") s = "GameThread,3D Engine";
  // "all": every guest thread - the runtime names them "<name> (F8xxxxxx)".
  // The boot loader turned out to be a thread neither default name covers
  // (2026-09-12: 400 ms per sound bank on a thread nobody was sampling).
  if (s == "all") s = "*";
  size_t start = 0;
  while (start <= s.size()) {
    size_t comma = s.find(',', start);
    if (comma == std::string::npos) comma = s.size();
    std::string name = s.substr(start, comma - start);
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (!name.empty()) out.push_back(name);
    start = comma + 1;
  }
  return out;
}

typedef HRESULT(WINAPI* GetThreadDescriptionFn)(HANDLE, PWSTR*);

// Find this process's threads whose description matches a wanted name.
void RefreshTargets(const std::vector<std::string>& wanted, std::vector<Target>& targets) {
  static GetThreadDescriptionFn get_desc = reinterpret_cast<GetThreadDescriptionFn>(
      GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));
  if (!get_desc) return;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return;
  THREADENTRY32 te;
  te.dwSize = sizeof(te);
  const DWORD pid = GetCurrentProcessId();
  if (Thread32First(snap, &te)) {
    do {
      if (te.th32OwnerProcessID != pid) continue;
      bool known = false;
      for (const Target& t : targets)
        if (t.tid == te.th32ThreadID) known = true;
      if (known) continue;
      HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                            FALSE, te.th32ThreadID);
      if (!h) continue;
      PWSTR desc = nullptr;
      std::string name;
      if (SUCCEEDED(get_desc(h, &desc)) && desc) {
        char narrow[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, desc, -1, narrow, sizeof(narrow), nullptr, nullptr);
        LocalFree(desc);
        name = narrow;
      }
      // The runtime names a guest thread "<name> (<handle>)" on the host -
      // "GameThread (F8000004)" - so match the name as a prefix.
      bool want = false;
      for (const std::string& w : wanted)
        if (name == w || name.rfind(w + " (", 0) == 0 ||
            (w == "*" && name.find(" (F8") != std::string::npos))
          want = true;
      if (!want) {
        static int listed = 0;
        if (!name.empty() && listed < 40) {
          ++listed;
          REXLOG_INFO("[profile] not sampling thread '{}' (tid {})", name, te.th32ThreadID);
        }
        CloseHandle(h);
        continue;
      }
      Target t;
      t.tid = te.th32ThreadID;
      t.handle = h;
      t.name = name;
      targets.push_back(std::move(t));
      REXLOG_INFO("[profile] sampling thread '{}' (tid {})", name, te.th32ThreadID);
    } while (Thread32Next(snap, &te));
  }
  CloseHandle(snap);
}

// The start of the function containing rip (dbghelp), cached: report time
// groups thousands of distinct addresses every ten seconds.
uint64_t FunctionStart(uint64_t rip) {
  static std::unordered_map<uint64_t, uint64_t> cache;
  auto it = cache.find(rip);
  if (it != cache.end()) return it->second;
  alignas(SYMBOL_INFO) char sbuf[sizeof(SYMBOL_INFO) + 256] = {};
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(sbuf);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 255;
  DWORD64 disp = 0;
  uint64_t start = rip;
  if (SymFromAddr(GetCurrentProcess(), rip, &disp, sym)) start = rip - disp;
  cache.emplace(rip, start);
  return start;
}

// Host (non-guest) samples of a map, grouped by containing function.
void TopHostFunctions(const Target& t, const std::unordered_map<uint64_t, uint32_t>& m,
                      const char* what, size_t count) {
  std::unordered_map<uint64_t, uint32_t> by_fn;
  for (const auto& p : m) {
    const ModuleClass c = ClassOf(p.first);
    if (c == kGenerated || c == kUnknown) continue;
    by_fn[FunctionStart(p.first)] += p.second;
  }
  if (by_fn.empty()) return;
  std::vector<std::pair<uint64_t, uint32_t>> v(by_fn.begin(), by_fn.end());
  std::partial_sort(v.begin(), v.begin() + std::min(count, v.size()), v.end(),
                    [](const auto& a, const auto& b) { return a.second > b.second; });
  std::string line;
  for (size_t i = 0; i < std::min(count, v.size()); ++i) {
    char b[512];
    std::snprintf(b, sizeof(b), "%s%s %.1f%%", i ? ", " : "", Describe(v[i].first).c_str(),
                  100.0 * v[i].second / t.samples);
    line += b;
  }
  REXLOG_INFO("[profile]   {}: {}", what, line);
}

void Report(Target& t, double secs) {
  if (t.samples + t.blocked == 0) return;
  // A thread asleep in a wait is somewhere, but it is not USING the core.
  // Sampling on the wall clock would count that as time in ntdll; the cycle
  // counter says whether it ran at all since the last sample, and only the
  // samples where it did make up the profile below.
  REXLOG_INFO("[profile] {} (tid {}): on CPU {:.0f}% of the time ({} of {} samples), blocked {:.0f}%",
              t.name, t.tid, 100.0 * t.samples / (t.samples + t.blocked), t.samples,
              t.samples + t.blocked, 100.0 * t.blocked / (t.samples + t.blocked));
  // The exact share: scheduled kernel+user time over the window, from the
  // kernel's own accounting, beside the summed cycle deltas. Printed from the
  // second report on (the first has no baseline).
  FILETIME ct, et, kt, ut;
  if (GetThreadTimes(t.handle, &ct, &et, &kt, &ut)) {
    const uint64_t cpu = ((uint64_t(kt.dwHighDateTime) << 32) | kt.dwLowDateTime) +
                         ((uint64_t(ut.dwHighDateTime) << 32) | ut.dwLowDateTime);
    if (t.last_cpu_100ns && cpu >= t.last_cpu_100ns && secs > 0.0) {
      const double cpu_ms = double(cpu - t.last_cpu_100ns) / 10000.0;
      REXLOG_INFO("[profile] {} (tid {}): SCHEDULED {:.1f} ms of {:.1f} ms wall = {:.1f}% "
                  "(GetThreadTimes kernel+user; {:.0f} Mcycles summed) - the exact on-CPU share; "
                  "the ran/blocked line is per 1 ms sample and reads a thread sleeping or "
                  "yielding in 1 ms steps as on CPU",
                  t.name, t.tid, cpu_ms, secs * 1000.0, 100.0 * cpu_ms / (secs * 1000.0),
                  double(t.cycles_window) / 1e6);
    }
    t.last_cpu_100ns = cpu;
  }
  t.cycles_window = 0;
  if (t.samples == 0) {
    t.blocked = 0;
    return;
  }
  std::string modules;
  for (int i = 0; i < 8; ++i) {
    if (!t.by_module[i]) continue;
    char b[64];
    std::snprintf(b, sizeof(b), "%s %.0f%%  ", kModuleNames[i], 100.0 * t.by_module[i] / t.samples);
    modules += b;
  }
  REXLOG_INFO("[profile] {} (tid {}): {} samples in {:.1f} s - {}", t.name, t.tid, t.samples, secs,
              modules);
  auto top = [&](const std::unordered_map<uint64_t, uint32_t>& m, const char* what, size_t count) {
    std::vector<std::pair<uint64_t, uint32_t>> v(m.begin(), m.end());
    std::partial_sort(v.begin(), v.begin() + std::min(count, v.size()), v.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
    std::string line;
    for (size_t i = 0; i < std::min(count, v.size()); ++i) {
      char b[512];
      std::snprintf(b, sizeof(b), "%s%s %.1f%%", i ? ", " : "", Describe(v[i].first).c_str(),
                    100.0 * v[i].second / t.samples);
      line += b;
    }
    REXLOG_INFO("[profile]   {}: {}", what, line);
  };
  top(t.guest_fn, "guest fn", 14);
  top(t.leaf, "leaf", 12);
  TopHostFunctions(t, t.leaf, "host fn (self)", 16);
  TopHostFunctions(t, t.frame_hit, "host fn (incl)", 16);
  // For samples sitting in a system call: who called it. "ntdll 90%" says
  // the thread waits; this says on what - a fence, the guest's WAIT_REG_MEM
  // poll, the ring buffer event - which is the difference between "the GPU is
  // slow" and "we are sleeping in one-millisecond steps".
  if (!t.waiter.empty()) top(t.waiter, "waiting in", 10);
  if (t.no_guest_frame)
    REXLOG_INFO("[profile]   {} samples ({:.0f}%) had no guest frame on the stack", t.no_guest_frame,
                100.0 * t.no_guest_frame / t.samples);
  t.leaf.clear();
  t.guest_fn.clear();
  t.waiter.clear();
  t.frame_hit.clear();
  t.samples = 0;
  t.blocked = 0;
  t.no_guest_frame = 0;
  std::memset(t.by_module, 0, sizeof(t.by_module));
}

void SamplerMain(std::vector<std::string> wanted) {
  g_exe = GetModuleHandleW(nullptr);
  g_runtime = GetModuleHandleW(L"rexruntime.dll");
  g_gpu = GetModuleHandleW(L"rexgpu-xenos.dll");
  g_ntdll = GetModuleHandleW(L"ntdll.dll");
  g_k32 = GetModuleHandleW(L"kernel32.dll");
  SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
  SymInitialize(GetCurrentProcess(), nullptr, TRUE);
  BuildGuestTable();
  REXLOG_INFO("[profile] {} generated functions indexed; sampling every {} us, report every {:.0f} s; "
              "guest host range [0x{:X}, 0x{:X}) - bounded by the app's own code at 0x{:X} ({}), "
              "so app and driver frames classify as themselves and a spin in the native path is "
              "not a guest function",
              g_guest_fns.size(), kSampleIntervalUs, kReportSeconds, g_guest_lo, g_guest_hi,
              g_app_anchor,
              g_app_anchor > g_guest_fns.back().host && g_guest_hi == g_app_anchor
                  ? "bound applied"
                  : "the app's code lies INSIDE the generated range - it is linked in gaps");
  REXLOG_INFO("[profile] generated-function extent capped at {} KB: {} gaps between consecutive "
              "generated functions exceed it ({:.1f} MB beyond the cap, classed as app code); the "
              "largest gap is {} KB; {} of the gaps are UNRESOLVED (no app anchor AND no symbol "
              "verdict - such a gap may be one giant generated function whose tail the cap charges "
              "to app code, the defect with the sign flipped; zero means both directions are "
              "accounted for). Gaps: {}",
              kMaxGuestFnBytes / 1024, g_gap_count, double(g_gap_bytes) / (1024.0 * 1024.0),
              g_largest_gap / 1024, g_gap_unanchored, g_gap_detail);

  std::vector<Target> targets;
  using clock = std::chrono::steady_clock;
  auto last_refresh = clock::now() - std::chrono::seconds(60);
  auto last_report = clock::now();
  uint64_t frames[kMaxFrames];
  while (g_run.load(std::memory_order_acquire)) {
    const auto now = clock::now();
    if (std::chrono::duration<double>(now - last_refresh).count() > 5.0) {
      RefreshTargets(wanted, targets);
      last_refresh = now;
      if (!g_gpu) g_gpu = GetModuleHandleW(L"rexgpu-xenos.dll");
    }
    for (Target& t : targets) {
      // Did it run since the last sample? If not, this is a blocked thread and
      // where it sits (a wait in ntdll) says nothing about CPU use.
      uint64_t cycles = 0;
      QueryThreadCycleTime(t.handle, &cycles);
      const bool ran = cycles != t.last_cycles;
      if (t.last_cycles && cycles > t.last_cycles) t.cycles_window += cycles - t.last_cycles;
      t.last_cycles = cycles;
      if (!ran) {
        ++t.blocked;
        continue;
      }
      const int n = Sample(t, frames);
      if (n <= 0) continue;
      ++t.samples;
      ++t.leaf[frames[0]];
      for (int i = 0; i < n; ++i) ++t.frame_hit[frames[i]];
      const ModuleClass leaf_class = ClassOf(frames[0]);
      ++t.by_module[leaf_class];
      if (leaf_class == kNtdll || leaf_class == kKernel32 || leaf_class == kOther) {
        // Walk up past the system's own frames to whoever asked for the wait,
        // and count its caller too: "CheckSubmissionFence" alone does not say
        // whether a frame is pacing itself or a readback is draining the GPU.
        int found = 0;
        for (int i = 1; i < n && found < 2; ++i) {
          const ModuleClass c = ClassOf(frames[i]);
          if (c != kNtdll && c != kKernel32 && c != kOther && c != kUnknown) {
            ++t.waiter[frames[i]];
            ++found;
          }
        }
      }
      const GuestFn* g = nullptr;
      for (int i = 0; i < n && !g; ++i) g = GuestFnFor(frames[i]);
      if (g)
        ++t.guest_fn[g->host];
      else
        ++t.no_guest_frame;
    }
    const double since = std::chrono::duration<double>(clock::now() - last_report).count();
    if (since >= kReportSeconds) {
      for (Target& t : targets) Report(t, since);
      last_report = clock::now();
    }
    std::this_thread::sleep_for(std::chrono::microseconds(kSampleIntervalUs));
  }
  for (Target& t : targets) CloseHandle(t.handle);
}

}  // namespace

void StartProfiler() {
  std::vector<std::string> wanted = WantedNames();
  if (wanted.empty()) return;
  if (g_run.exchange(true)) return;
  g_thread = std::thread(SamplerMain, std::move(wanted));
}

void StopProfiler() {
  if (!g_run.exchange(false)) return;
  if (g_thread.joinable()) g_thread.join();
}

std::string DescribeHostAddress(uint64_t rip) {
  static std::mutex init_mutex;
  static bool ready = false;
  {
    std::lock_guard<std::mutex> lock(init_mutex);
    if (!ready) {
      ready = true;
      if (!g_exe) g_exe = GetModuleHandleW(nullptr);
      SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
      SymInitialize(GetCurrentProcess(), nullptr, TRUE);
      if (g_guest_fns.empty()) BuildGuestTable();
    }
  }
  return Describe(rip);
}

}  // namespace fable2
