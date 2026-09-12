#include "fable2_profiler.h"

#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
  uint32_t samples = 0;         // on-CPU samples: the thread ran since the last look
  uint32_t blocked = 0;         // wall-clock samples where it had not run at all
  uint64_t last_cycles = 0;
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

HMODULE g_exe = nullptr, g_runtime = nullptr, g_gpu = nullptr, g_ntdll = nullptr, g_k32 = nullptr;

void BuildGuestTable() {
  for (const PPCFuncMapping* m = PPCFuncMappings; m->host != nullptr; ++m) {
    g_guest_fns.push_back({reinterpret_cast<uint64_t>(m->host), uint32_t(m->guest)});
  }
  std::sort(g_guest_fns.begin(), g_guest_fns.end(),
            [](const GuestFn& a, const GuestFn& b) { return a.host < b.host; });
  if (!g_guest_fns.empty()) {
    g_guest_lo = g_guest_fns.front().host;
    g_guest_hi = g_guest_fns.back().host + 0x100000;  // the last function's extent is unknown
  }
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
        if (name == w || name.rfind(w + " (", 0) == 0) want = true;
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

void Report(Target& t, double secs) {
  if (t.samples + t.blocked == 0) return;
  // A thread asleep in a wait is somewhere, but it is not USING the core.
  // Sampling on the wall clock would count that as time in ntdll; the cycle
  // counter says whether it ran at all since the last sample, and only the
  // samples where it did make up the profile below.
  REXLOG_INFO("[profile] {} (tid {}): on CPU {:.0f}% of the time ({} of {} samples), blocked {:.0f}%",
              t.name, t.tid, 100.0 * t.samples / (t.samples + t.blocked), t.samples,
              t.samples + t.blocked, 100.0 * t.blocked / (t.samples + t.blocked));
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
  if (t.no_guest_frame)
    REXLOG_INFO("[profile]   {} samples ({:.0f}%) had no guest frame on the stack", t.no_guest_frame,
                100.0 * t.no_guest_frame / t.samples);
  t.leaf.clear();
  t.guest_fn.clear();
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
  REXLOG_INFO("[profile] {} generated functions indexed; sampling every {} us, report every {:.0f} s",
              g_guest_fns.size(), kSampleIntervalUs, kReportSeconds);

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
      t.last_cycles = cycles;
      if (!ran) {
        ++t.blocked;
        continue;
      }
      const int n = Sample(t, frames);
      if (n <= 0) continue;
      ++t.samples;
      ++t.leaf[frames[0]];
      ++t.by_module[ClassOf(frames[0])];
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

}  // namespace fable2
