#include "native_gpu_census.h"
#include "native_gpu_shader_census.h"

#include <windows.h>

#include <rex/cvar.h>
#include <rex/logging.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// The census records the Xenos surface a game exercises and how much of it the
// native layer covers. See native_gpu_census.h for the four failure modes this
// design defends against. Default OFF: every recording call is a cvar load and,
// when on, one hashed lookup under a mutex - only paid when measuring.
REXCVAR_DEFINE_BOOL(ngpu_census, false, "GPU",
    "Native-GPU coverage census: record every Xenos subject encountered (PM4 "
    "opcode, register, shader op, format) on two axes - ported and verified - "
    "and report unported / off-census / refused as a FLOOR");
REXCVAR_DEFINE_INT32(ngpu_census_report_secs, 15, "GPU",
    "Native-GPU census: seconds between reports (0 = only when Report() is called)");

namespace fable2 {
namespace ngpu {
namespace census {
namespace {

struct Subject {
  const char* name = nullptr;
  bool in_denominator = false;  // present in the SDK-generated .inc
  bool ported = false;          // axis 1: a native handler exists
  const char* oracle = nullptr; // axis 2: non-null once verified, names WHAT checked it
  uint64_t seen = 0;            // arrivals at the raw boundary (a FLOOR)
  const char* replaces = nullptr;  // the rexglue emulation code this handler replaces (file:function)
  bool emulated_dead = false;      // that code no longer executes on the native path
};

inline uint64_t Key(Kind k, uint32_t id) {
  return (uint64_t(static_cast<uint8_t>(k)) << 32) | id;
}

std::mutex g_mu;
std::unordered_map<uint64_t, Subject> g_subjects;          // denominator + off-census
std::array<uint64_t, static_cast<size_t>(Kind::kCount)> g_refused{};  // per-kind, a FLOOR
// Arrivals dropped because they came from a speculative walk that did not stick
// (per kind). Reported, so the instrument's own rejections are countable.
std::array<uint64_t, static_cast<size_t>(Kind::kCount)> g_discarded{};
bool g_loaded = false;
double g_next_report = 0.0;

// Tentative buffer: per thread (only the parsing thread probes, but a buffer
// shared across threads would mix a probe with a real arrival elsewhere).
struct Tentative {
  bool active = false;
  bool poisoned = false;   // an inner scope discarded: the outermost commit must discard too
  uint32_t depth = 0;
  struct Hit { Kind kind; uint32_t id; const char* name; bool refused; };
  std::vector<Hit> hits;
};
thread_local Tentative t_tent;

// Count one arrival (caller holds g_mu; denominator loaded).
void CountLocked(Kind kind, uint32_t id, const char* name) {
  auto& s = g_subjects[Key(kind, id)];
  if (!s.name) {  // first sight of an id not in the denominator = off-census (point d)
    s.name = name ? name : "<off-census>";
    s.in_denominator = false;
  }
  ++s.seen;
}

double NowSecs() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Populate the denominator once from the SDK-generated subject list. Caller holds g_mu.
void LoadLocked() {
  if (g_loaded) return;
  g_loaded = true;
// Param names are underscored so they cannot collide with the Subject members
// they assign (s.name / s.in_denominator) - `name` as a parameter rewrote
// `s.name` into `s."..."`.
#define NGPU_CENSUS_SUBJECT(k_, id_, name_) \
  do { auto& s_ = g_subjects[Key(Kind::k_, uint32_t(id_))]; \
       if (!s_.name) { s_.name = (name_); s_.in_denominator = true; } } while (0);
#include "native_gpu_census_subjects.inc"
#undef NGPU_CENSUS_SUBJECT
  // The XDK-private device-shadow fields: NOT from an SDK enum (no enum names
  // them), registered programmatically as the 85 non-register dwords of the
  // 144-dword shadow block (0x2010-0x2012 and 0x2211-0x2262, by slot label).
  // Provenance: their VALUES on 2026-09-22 (native_gpu_present.cpp,
  // CensusShadowRegisters) - sizes, pointers, polygon-offset floats, counters.
  // Named where the value identified the field; the rest carry their slot only.
  auto xdk = [](uint32_t id, const char* name) {
    auto& s_ = g_subjects[Key(Kind::XdkDeviceState, id)];
    if (!s_.name) { s_.name = name; s_.in_denominator = true; }
  };
  for (uint32_t id = 0x2010; id <= 0x2012; ++id) xdk(id, id == 0x2012 ? "XDK: render-target size h<<16|w" : "XDK field (shadow head)");
  for (uint32_t id = 0x2211; id <= 0x2262; ++id) {
    const char* name = "XDK field";
    if (id == 0x223B) name = "XDK: render-target size h<<16|w (2nd)";
    else if (id == 0x2247) name = "XDK: polygon offset front scale (float)";
    else if (id == 0x2248) name = "XDK: polygon offset front bias (float)";
    else if (id == 0x2249) name = "XDK: polygon offset back scale (float)";
    else if (id == 0x224A) name = "XDK: polygon offset back bias (float)";
    else if (id == 0x223A) name = "XDK: guest pointer";
    else if (id == 0x2257 || id == 0x2258) name = "XDK: physical address";
    else if (id == 0x225A) name = "XDK: counter";
    xdk(id, name);
  }
}

Subject* Find(Kind k, uint32_t id) {
  auto it = g_subjects.find(Key(k, id));
  return it == g_subjects.end() ? nullptr : &it->second;
}

}  // namespace

const char* KindName(Kind k) {
  switch (k) {
    case Kind::Pm4Opcode:        return "PM4 opcode";
    case Kind::Register:         return "register";
    case Kind::ShaderAluVector:  return "shader ALU-vector op";
    case Kind::ShaderAluScalar:  return "shader ALU-scalar op";
    case Kind::ShaderFetch:      return "shader fetch op";
    case Kind::ShaderControlFlow:return "shader control-flow op";
    case Kind::TextureFormat:    return "texture format";
    case Kind::VertexFormat:     return "vertex format";
    case Kind::ColorRtFormat:    return "color RT format";
    case Kind::DepthRtFormat:    return "depth RT format";
    case Kind::Endian:           return "endian";
    case Kind::PrimitiveType:    return "primitive type";
    case Kind::Sequence:         return "sequence/ordering";
    case Kind::XdkDeviceState:   return "XDK device state (no reg)";
    default:                     return "?";
  }
}

void EnsureLoaded() {
  std::lock_guard<std::mutex> lk(g_mu);
  LoadLocked();
}

void See(Kind kind, uint32_t id, const char* name) {
  if (!REXCVAR_GET(ngpu_census)) return;
  if (t_tent.active) { t_tent.hits.push_back({kind, id, name, false}); return; }
  std::lock_guard<std::mutex> lk(g_mu);
  LoadLocked();
  CountLocked(kind, id, name);
}

void Refused(Kind kind, uint32_t /*raw*/) {
  if (!REXCVAR_GET(ngpu_census)) return;
  if (t_tent.active) { t_tent.hits.push_back({kind, 0, nullptr, true}); return; }
  std::lock_guard<std::mutex> lk(g_mu);
  ++g_refused[static_cast<size_t>(kind)];  // a FLOOR: what the decoder dropped
}

void BeginTentative() {
  if (t_tent.depth++ == 0) { t_tent.active = true; t_tent.poisoned = false; t_tent.hits.clear(); }
}

// Scopes nest (a probe wraps a walk that wraps itself): only the OUTERMOST
// scope decides, and an inner discard poisons the whole buffer - otherwise an
// overshooting walk inside a probe that 'succeeded' would be committed.
static void DiscardAllLocked() {
  for (const auto& h : t_tent.hits) ++g_discarded[static_cast<size_t>(h.kind)];
  t_tent.hits.clear();
}

void CommitTentative() {
  if (t_tent.depth == 0) return;
  if (--t_tent.depth) return;
  t_tent.active = false;
  if (t_tent.hits.empty()) return;
  std::lock_guard<std::mutex> lk(g_mu);
  if (t_tent.poisoned) { DiscardAllLocked(); return; }
  LoadLocked();
  for (const auto& h : t_tent.hits) {
    if (h.refused) ++g_refused[static_cast<size_t>(h.kind)];
    else CountLocked(h.kind, h.id, h.name);
  }
  t_tent.hits.clear();
}

void DiscardTentative() {
  if (t_tent.depth == 0) return;
  if (--t_tent.depth) { t_tent.poisoned = true; return; }
  t_tent.active = false;
  if (t_tent.hits.empty()) return;
  std::lock_guard<std::mutex> lk(g_mu);
  DiscardAllLocked();
}

void MarkPorted(Kind kind, uint32_t id) {
  std::lock_guard<std::mutex> lk(g_mu);
  LoadLocked();
  auto& s = g_subjects[Key(kind, id)];
  s.ported = true;
  if (!s.name) { s.name = "<ported,off-census>"; s.in_denominator = false; }
}

void MarkVerifiedAllPorted(Kind kind, const char* oracle) {
  std::lock_guard<std::mutex> lk(g_mu);
  LoadLocked();
  for (auto& kv : g_subjects) {
    if (static_cast<Kind>(kv.first >> 32) != kind) continue;
    Subject& s = kv.second;
    if (!s.ported) continue;
    // A stronger (full) verification already recorded is never downgraded to PARTIAL.
    if (s.oracle && std::strncmp(s.oracle, "PARTIAL:", 8) != 0 && std::strncmp(oracle, "PARTIAL:", 8) == 0) continue;
    s.oracle = oracle;
  }
}

void Verdict(const char* line) {
  static std::mutex mu;
  std::lock_guard<std::mutex> lk(mu);
  if (FILE* f = std::fopen("ngpu_verdicts.txt", "ab")) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    char stamp[32] = {0};
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    tmv = *std::localtime(&t);
#endif
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmv);
    std::fprintf(f, "[%s] %s\n", stamp, line);
    std::fclose(f);
  }
}

// Per-kind census lines go to the verdict file only when they change, so the
// file holds the run's progression without the per-report repetition.
static void VerdictIfChanged(size_t kind_index, const char* line) {
  static std::string last[static_cast<size_t>(Kind::kCount) + 1];
  if (kind_index >= static_cast<size_t>(Kind::kCount) + 1) return;
  if (last[kind_index] == line) return;
  last[kind_index] = line;
  Verdict(line);
}

void MarkReplaces(Kind kind, uint32_t id, const char* replaces, bool emulated_dead) {
  std::lock_guard<std::mutex> lk(g_mu);
  LoadLocked();
  auto& s = g_subjects[Key(kind, id)];
  s.replaces = replaces;
  s.emulated_dead = emulated_dead;
  if (!s.name) { s.name = "<replaces,off-census>"; s.in_denominator = false; }
}

void MarkVerified(Kind kind, uint32_t id, const char* oracle) {
  std::lock_guard<std::mutex> lk(g_mu);
  LoadLocked();
  auto& s = g_subjects[Key(kind, id)];
  s.ported = true;         // verified implies a handler exists
  s.oracle = oracle;
  if (!s.name) { s.name = "<verified,off-census>"; s.in_denominator = false; }
}

static std::atomic<int> g_transplant{0};   // 0 none, 1 duplicated (plugin still renders), 2 replaced (offload)
void NoteBackendTransplant(bool plugin_backend_skipped) { g_transplant.store(plugin_backend_skipped ? 2 : 1); }

void Report() {
  std::lock_guard<std::mutex> lk(g_mu);
  LoadLocked();
  // DEAD IS MEASURED (peer review 2026-09-26): the plugin counts executions of its backend's draw / copy / swap /
  // register-hook bodies and how often its offload gate skipped them (RexNgpuPluginBackendBodies). REPLACED needs
  // every body count at 0 with the gates taken; otherwise (or without the export) the subjects stay DUPLICATED.
  int t = g_transplant.load();
  if (t == 2) {
    using BodiesFn = void (*)(uint64_t*);
    static BodiesFn bodies_fn = [] {
      HMODULE m = GetModuleHandleA("rexgpu-xenos.dll");
      return m ? reinterpret_cast<BodiesFn>(GetProcAddress(m, "RexNgpuPluginBackendBodies")) : nullptr;
    }();
    uint64_t b[8] = {};
    if (bodies_fn) bodies_fn(b);
    const bool dead = bodies_fn && !b[0] && !b[2] && !b[4] && !b[6] && b[1] && b[5];
    char line[320];
    std::snprintf(line, sizeof(line),
                  "plugin backend bodies under offload (MEASURED): draw %llu run / %llu gated, copy %llu / %llu, swap %llu / "
                  "%llu, register hooks %llu / %llu -> %s",
                  (unsigned long long)b[0], (unsigned long long)b[1], (unsigned long long)b[2], (unsigned long long)b[3],
                  (unsigned long long)b[4], (unsigned long long)b[5], (unsigned long long)b[6], (unsigned long long)b[7],
                  !bodies_fn ? "NOT MEASURED (plugin has no counter export) - DUPLICATED" : dead ? "0 executions: REPLACED" : "the plugin's copy RAN - DUPLICATED");
    REXLOG_INFO("[ngpu-census] {}", line);
    if (!dead) t = 1;
  }
  if (t) {
    static const char* kTransplant =
        "(registers: the D3D12 STATE APPLY only - the plugin still parses PM4 into its register file and marks the "
        "dirty bitmap the backend reads) rexgpu-xenos d3d12 backend (command_processor/pipeline_cache/render_target_cache/texture_cache/"
        "primitive_processor/shared_memory) - vendored in-app as the native backend";
    for (auto& kv : g_subjects) {
      const Kind k = static_cast<Kind>(kv.first >> 32);
      if (!kv.second.in_denominator) continue;
      if (k != Kind::Register && k != Kind::TextureFormat && k != Kind::VertexFormat && k != Kind::ColorRtFormat &&
          k != Kind::DepthRtFormat && k != Kind::Endian && k != Kind::PrimitiveType) continue;
      kv.second.ported = true;
      kv.second.replaces = kTransplant;
      kv.second.emulated_dead = t == 2;
    }
  }

  struct Row { uint64_t denom=0, denom_seen=0, ported=0, ported_seen=0, verified=0, partial=0, off=0, dead=0, dup=0, unnamed=0; };
  std::array<Row, static_cast<size_t>(Kind::kCount)> rows{};
  std::vector<std::string> off_census;      // seen, not in denominator (point d)
  std::vector<std::string> seen_unported;   // in denominator, seen, no handler

  for (auto& kv : g_subjects) {
    const Kind k = static_cast<Kind>(kv.first >> 32);
    const uint32_t id = static_cast<uint32_t>(kv.first & 0xffffffffu);
    const Subject& s = kv.second;
    Row& r = rows[static_cast<size_t>(k)];
    if (s.in_denominator) {
      ++r.denom;
      if (s.seen)   ++r.denom_seen;
      if (s.ported) {
        ++r.ported; if (s.seen) ++r.ported_seen;
        // The replacement ledger: a handler whose emulated counterpart still
        // runs is DUPLICATED (the rejected architecture), not a replacement.
        if (!s.replaces) ++r.unnamed;
        else if (s.emulated_dead) ++r.dead;
        else ++r.dup;
      }
      if (s.oracle) { if (std::strncmp(s.oracle, "PARTIAL:", 8) == 0) ++r.partial; else ++r.verified; }
      if (s.seen && !s.ported) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s 0x%X (%s) x%llu",
                      KindName(k), id, s.name ? s.name : "?",
                      static_cast<unsigned long long>(s.seen));
        seen_unported.emplace_back(buf);
      }
    } else if (s.seen || s.ported) {
      ++r.off;
      char buf[96];
      std::snprintf(buf, sizeof(buf), "%s 0x%X (%s) x%llu",
                    KindName(k), id, s.name ? s.name : "?",
                    static_cast<unsigned long long>(s.seen));
      off_census.emplace_back(buf);
    }
  }

  auto emit = [](const char* line) { REXLOG_INFO("[ngpu-census] {}", line); };
  emit("==== Xenos->native coverage census (all counts are FLOORS) ====");
  // Point d: the census cannot enumerate its own blind spot, so it states which
  // enums it took and why that set is believed complete.
  emit("provenance: subjects generated from the SDK enums - Type3Opcode, TextureFormat/"
       "VertexFormat/Color+DepthRTFormat/Endian/PrimitiveType (xenos.h), Register "
       "(register_table.inc), shader ISA AluVector/AluScalar/Fetch/ControlFlow (ucode.h).");
  // Point d: a census cannot enumerate its own blind spot, so the boundary is STATED,
  // not implied - for each thing NOT a separate subject, where it IS covered and why.
  emit("BOUNDARY (not separate subjects, and why): blend/alpha modes = RB_BLENDCONTROL/"
       "RB_COLORCONTROL sub-fields (the register is censused; the mode is a PARAMETER of it "
       "- point a). sampler state = SQ-tex/fetch-constant sub-fields (folded into registers). "
       "vertex-fetch = the 0x4800 fetch-constant block (folded into registers). index format "
       "= a draw payload field (IndexFormat kInt16/kInt32) - NOT taken as a kind. query/fence "
       "= the VIZ_QUERY/WAIT_REG_MEM/EVENT_WRITE PM4 opcodes. memexport = a shader feature + "
       "fetch constant (shader ISA + registers). Anything else a game exercises = OFF-CENSUS.");
  // Point b: ordering is on no enum.
  // Point d, measured not hypothetical: the denominator is what the taken enums NAME,
  // not "all subjects" - a game CAN exercise a subject no enum enumerates, and one has.
  emit("DENOMINATOR CAVEAT (MEASURED): the denominator = subjects the taken enums NAME, "
       "not ALL subjects. Counterexample seen: real register writes to the COHERENT block "
       "0x2010-0x202D, NOT in register_table.inc and distinct from the scattered <0x2000 "
       "desync noise - undocumented regs / a different addressing mode / our own decode error, "
       "UNRESOLVED. So off-census is not all garbage; a coherent off-census block is a finding.");
  emit("SEQUENCE/ORDERING coverage is UNMEASURED until Kind::Sequence has handlers "
       "(EDRAM aliasing, resolve-after-clear, marker-carries-own-registers).");
  // Point a: two axes, never one number.
  emit("axes: PORTED = a native handler exists; VERIFIED = its output was checked "
       "against the plugin oracle; PARTIAL = one named STAGE of the handler was checked "
       "(the oracle column says which - never read PARTIAL as verified). Coverage != correctness.");

  // The replacement ledger, stated before the numbers so PORTED is never read
  // as "the emulation is gone": ported = a native handler exists; of those,
  // REPLACED = its rexglue counterpart is named AND no longer executes on the
  // native path; DUPLICATED = the counterpart is named and STILL RUNS (the
  // rejected architecture - native on top of live emulation); unnamed = the
  // handler names no counterpart yet. On the hybrid build the plugin renders
  // the main window, so REPLACED is 0 for every subject until the emulated
  // path is actually removed - that number, not PORTED, is the deliverable.
  emit("replacement ledger: PORTED = handler exists; REPLACED = rexglue counterpart named + DEAD on "
       "the native path; DUPLICATED = counterpart named but STILL EXECUTES (not a port); "
       "unnamed = no counterpart recorded. discarded(probe) = arrivals from speculative "
       "resync walks that did not stick (the instrument's own rejections, counted not hidden).");
  char line[320];
  for (size_t ki = 0; ki < static_cast<size_t>(Kind::kCount); ++ki) {
    const Row& r = rows[ki];
    if (r.denom == 0 && r.off == 0 && g_refused[ki] == 0 && g_discarded[ki] == 0) continue;
    std::snprintf(line, sizeof(line),
        "%-22s denom=%llu seen=%llu | PORTED=%llu (seen %llu) [REPLACED=%llu DUPLICATED=%llu unnamed=%llu] | "
        "VERIFIED=%llu PARTIAL=%llu | off-census=%llu | refused(floor)=%llu | discarded(probe)=%llu",
        KindName(static_cast<Kind>(ki)),
        (unsigned long long)r.denom, (unsigned long long)r.denom_seen,
        (unsigned long long)r.ported, (unsigned long long)r.ported_seen,
        (unsigned long long)r.dead, (unsigned long long)r.dup, (unsigned long long)r.unnamed,
        (unsigned long long)r.verified, (unsigned long long)r.partial, (unsigned long long)r.off,
        (unsigned long long)g_refused[ki], (unsigned long long)g_discarded[ki]);
    emit(line);
    VerdictIfChanged(ki, line);
  }

  // BOTH register denominators, always, side by side (never a percentage against
  // the smaller one alone): the SDK's full list, and the subset the XDK driver
  // SHADOWS in the device (0x2000-0x2012, 0x2100-0x2114, 0x2180-0x2184,
  // 0x2200-0x2262 = 144) - the state a D3D9-level game can set, censused from
  // the shadow at draw time. What falls between them is written by the driver's
  // own ring setup or by nothing; the ring-fed feed that could count those is
  // OFF by default (ngpu_census_ring) because it measured the parser.
  {
    uint64_t sh_denom = 0, sh_seen = 0, sh_ported = 0, sh_verified = 0, sh_partial = 0;
    for (auto& kv : g_subjects) {
      if (static_cast<Kind>(kv.first >> 32) != Kind::Register) continue;
      const uint32_t id = static_cast<uint32_t>(kv.first & 0xffffffffu);
      // The register slots of the XDK device shadow (59): its head mirrors these
      // registers; the remaining 85 dwords of the 144-dword block are XDK-private
      // fields (sizes, pointers, polygon-offset floats, counters) - established by
      // their values on 2026-09-22, see CensusShadowRegisters in native_gpu_present.cpp.
      const bool shadowed = (id >= 0x2000 && id < 0x2010) || (id >= 0x2100 && id < 0x2115) ||
                            (id >= 0x2180 && id < 0x2185) || (id >= 0x2200 && id < 0x2211);
      if (!shadowed || !kv.second.in_denominator) continue;
      const Subject& s = kv.second;
      ++sh_denom;
      if (s.seen) ++sh_seen;
      if (s.ported) ++sh_ported;
      if (s.oracle) { if (std::strncmp(s.oracle, "PARTIAL:", 8) == 0) ++sh_partial; else ++sh_verified; }
    }
    std::snprintf(line, sizeof(line),
        "register (DEVICE-SHADOW subset of the above; the D3D9-level state) denom=%llu of the SDK's %llu | seen=%llu (value changed at a draw, a FLOOR) | "
        "PORTED=%llu | VERIFIED=%llu PARTIAL=%llu | the %llu SDK registers outside the shadow are driver-setup or unused and are NOT censused by default",
        (unsigned long long)sh_denom, (unsigned long long)rows[static_cast<size_t>(Kind::Register)].denom,
        (unsigned long long)sh_seen, (unsigned long long)sh_ported, (unsigned long long)sh_verified, (unsigned long long)sh_partial,
        (unsigned long long)(rows[static_cast<size_t>(Kind::Register)].denom - sh_denom));
    emit(line);
    VerdictIfChanged(static_cast<size_t>(Kind::kCount), line);
  }

  if (!off_census.empty()) {
    char hdr[192];
    std::snprintf(hdr, sizeof(hdr),
        "---- OFF-CENSUS: %zu subjects seen but NOT in the denominator - either an "
        "incomplete enum OR upstream garbage (e.g. parser desync); investigate ----",
        off_census.size());
    emit(hdr);
    size_t shown = 0;
    for (auto& s : off_census) {
      emit(s.c_str());
      if (++shown >= 40) { emit("... (listing capped at 40; the count above is the FLOOR)"); break; }
    }
  }
  if (!seen_unported.empty()) {
    emit("---- SEEN but UNPORTED (the work queue, by exercised subject) ----");
    size_t shown = 0;
    for (auto& s : seen_unported) { emit(s.c_str()); if (++shown >= 200) { emit("... (truncated)"); break; } }
  }
  // The shader-ISA instrument reports beside the surface numbers it feeds.
  // (g_mu is held here; shader_census::Report takes only its own lock.)
  fable2::ngpu::shader_census::Report();

  // Persist the denominator + coverage next to the exe: the checkable artefact.
  if (FILE* f = std::fopen("ngpu_census.txt", "wb")) {
    std::fprintf(f, "# Xenos->native coverage census (FLOORS). ported!=verified; sequence coverage unmeasured.\n");
    std::fprintf(f, "# replaces = the rexglue emulation code the handler stands in for; emulated_dead = 1 only when that code no longer executes on the native path (ported && !emulated_dead = DUPLICATED, not a port).\n");
    std::fprintf(f, "# columns: kind\tid\tname\tin_denom\tported\tverified_by\tseen\treplaces\temulated_dead\n");
    for (auto& kv : g_subjects) {
      const Kind k = static_cast<Kind>(kv.first >> 32);
      const uint32_t id = static_cast<uint32_t>(kv.first & 0xffffffffu);
      const Subject& s = kv.second;
      std::fprintf(f, "%s\t0x%X\t%s\t%d\t%d\t%s\t%llu\t%s\t%d\n",
                   KindName(k), id, s.name ? s.name : "?",
                   s.in_denominator ? 1 : 0, s.ported ? 1 : 0,
                   s.oracle ? s.oracle : "-", (unsigned long long)s.seen,
                   s.replaces ? s.replaces : "-", s.emulated_dead ? 1 : 0);
    }
    for (size_t ki = 0; ki < static_cast<size_t>(Kind::kCount); ++ki) {
      if (g_refused[ki]) std::fprintf(f, "# refused(floor)\t%s\t%llu\n",
                                      KindName(static_cast<Kind>(ki)), (unsigned long long)g_refused[ki]);
      if (g_discarded[ki]) std::fprintf(f, "# discarded(probe)\t%s\t%llu\n",
                                        KindName(static_cast<Kind>(ki)), (unsigned long long)g_discarded[ki]);
    }
    std::fclose(f);
  }
}

void MaybeReport() {
  if (!REXCVAR_GET(ngpu_census)) return;
  const int secs = REXCVAR_GET(ngpu_census_report_secs);
  if (secs <= 0) return;
  const double now = NowSecs();
  {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_next_report == 0.0) { g_next_report = now + secs; return; }
    if (now < g_next_report) return;
    g_next_report = now + secs;
  }
  Report();
}

// ---- Static handler declaration -------------------------------------------
// The PORTED axis (does a native handler EXIST?) is knowable without a game, so
// a game-free run still reports real coverage. Seeded from the handlers the code
// demonstrably has today. This is the "a handler exists" axis ONLY - never
// VERIFIED (that needs the plugin oracle). If the code's switch changes, this
// list must too; a handler seen at runtime that is not here shows up as ported
// via the dispatch-site MarkPorted regardless.
static void SeedKnownPorted() {
  // ON THE HYBRID BUILD NOTHING IS DEAD: the plugin (rexgpu-xenos.dll) still
  // parses the PM4 stream, applies every register, decodes every texture and
  // renders the main window; the native path runs beside it. So every
  // MarkReplaces below says emulated_dead=false, and the ledger reports them as
  // DUPLICATED. That is the true state of the build, and the number that has to
  // move is REPLACED - it moves only when the emulated code stops executing.
  const bool kDead = false;
  // Texture formats with a case in GetTexture's switch (native_gpu_present.cpp).
  static const uint32_t kTex[] = {2,6,10,18,19,20,24,25,26,27,28,29,30,31,32,36,37,38,49,58,59,62};
  for (uint32_t f : kTex) {
    MarkPorted(Kind::TextureFormat, f);
    MarkReplaces(Kind::TextureFormat, f, "rexgpu-xenos pipeline/texture/cache.cpp (texture load: format->host, untile, swap)", kDead);
  }
  for (uint32_t e = 0; e <= 3; ++e) {  // all four, applied by the untile
    MarkPorted(Kind::Endian, e);
    MarkReplaces(Kind::Endian, e, "rexcore pipeline/texture/conversion.cpp CopySwapBlock (SDK, reused as-is by ngpu_use_sdk_untile)", kDead);
  }
  // Vertex formats mapped by MapVertexFormat (native_gpu_present.cpp).
  static const uint32_t kVtx[] = {6,7,16,17,25,26,31,32,36,37,38,57};
  for (uint32_t f : kVtx) {
    MarkPorted(Kind::VertexFormat, f);
    MarkReplaces(Kind::VertexFormat, f, "rexgpu-xenos pipeline/shader/dxbc_translator_fetch.cpp (vertex fetch decode in the translated shader)", kDead);
  }
  // PM4 opcodes the native RingParse re-parse extracts state from (a FLOOR: the
  // plugin's parser owns the complete stream including indirect buffers).
  static const uint32_t kPm4[] = {0x64,0x2D,0x2F,0x27,0x2B,0x22,0x34,0x35,0x36,0x21};
  for (uint32_t o : kPm4) {
    MarkPorted(Kind::Pm4Opcode, o);
    MarkReplaces(Kind::Pm4Opcode, o, "rexgpu-xenos command_processor.cpp ExecutePacketType3 (RingParse re-parses the same stream)", kDead);
  }
  // Primitive types the native draw path maps to a Plume topology (the draw
  // pipeline in native_gpu_present.cpp: 4 kTriangleList, 6 kTriangleStrip,
  // 1 kPointList; 8 kRectangleList via ngpu_rect_list and 13 kQuadList via
  // ngpu_quad_list are expanded to triangles). Seen from the draw hooks' own
  // primitive argument, not the ring parse.
  static const uint32_t kPrim[] = {1, 4, 6, 8, 13};
  for (uint32_t p : kPrim) {
    MarkPorted(Kind::PrimitiveType, p);
    MarkReplaces(Kind::PrimitiveType, p, "rexgpu-xenos primitive_processor.cpp (primitive type -> host topology, rect/quad expansion)", kDead);
  }
  // Registers the native path READS AND APPLIES today (audited by grep of every
  // register-index read form in native_gpu_present.cpp - Reg(dev,..), Reg(0,..),
  // g_ring.regs[..], snap.st/rt/cm[..] - 2026-09-22, not from memory). Scope of
  // "ported" here: the native path consumes the register's value; NOT that every
  // field value is decoded as the plugin decodes it (that is the VERIFIED axis).
  //   ReadDrawState:      0x2200 RB_DEPTHCONTROL, 0x2201 RB_BLENDCONTROL0, 0x2205 PA_SU_SC_MODE_CNTL, 0x2104 RB_COLOR_MASK
  //   render targets:     0x2000 RB_SURFACE_INFO, 0x2001 RB_COLOR_INFO, 0x2002 RB_DEPTH_INFO, 0x2208 RB_MODECONTROL
  //   window/scissor:     0x2080 PA_SC_WINDOW_OFFSET, 0x2081/0x2082 PA_SC_WINDOW_SCISSOR_TL/BR
  //   index/vertex range: 0x2100 VGT_MAX_VTX_INDX, 0x2101 VGT_MIN_VTX_INDX, 0x2102 VGT_INDX_OFFSET, 0x2103 VGT_MULTI_PRIM_IB_RESET_INDX
  //   blend const/alpha:  0x2105 RB_BLEND_RED, 0x2106 RB_BLEND_GREEN, 0x210E RB_ALPHA_REF, 0x2202 RB_COLORCONTROL
  //   viewport depth:     0x2113 PA_CL_VPORT_ZSCALE, 0x2114 PA_CL_VPORT_ZOFFSET, 0x2204 PA_CL_CLIP_CNTL
  //   tessellation:       0x2284 VGT_OUTPUT_PATH_CNTL, 0x2285 VGT_HOS_CNTL, 0x2286/0x2287 VGT_HOS_MAX/MIN_TESS_LEVEL
  //   resolve:            0x2318 RB_COPY_CONTROL
  //   constant blocks:    0x4000 ALU, 0x4800 FETCH, 0x4900 BOOL, 0x4908 LOOP (collapsed block subjects)
  static const uint32_t kRegs[] = {0x2200,0x2201,0x2205,0x2104, 0x2000,0x2001,0x2002,0x2208, 0x2080,0x2081,0x2082,
                                   0x2100,0x2101,0x2102,0x2103, 0x2105,0x2106,0x210E,0x2202, 0x2113,0x2114,0x2204,
                                   0x2284,0x2285,0x2286,0x2287, 0x2318, 0x4000,0x4800,0x4900,0x4908};
  for (uint32_t r : kRegs) {
    MarkPorted(Kind::Register, r);
    MarkReplaces(Kind::Register, r, "rexgpu-xenos d3d12/pipeline_cache.cpp + render_target_cache.cpp + command_processor.cpp (register->D3D12 state apply)", kDead);
  }
}

// ---- Autonomous reporter ----------------------------------------------------
// The census must report whether or not any specific native hook fires (the
// hybrid path may not call OnBridgeSwap or GetTexture at all). A detached poller
// owned by the census itself removes that dependency: it reports on its own
// clock whenever ngpu_census is on, so the coverage numbers land without a game
// drive. Default OFF => the thread only ever sleeps and checks a cvar.
namespace {
std::atomic<bool> g_reporter_running{false};
struct AutoReporter {
  AutoReporter() {
    if (g_reporter_running.exchange(true)) return;
    std::thread([]{
      bool seeded = false;
      double next = 0.0;
      for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!REXCVAR_GET(ngpu_census)) { next = 0.0; continue; }
        if (!seeded) { EnsureLoaded(); SeedKnownPorted(); seeded = true; }
        int secs = REXCVAR_GET(ngpu_census_report_secs);
        if (secs <= 0) secs = 15;
        const double now = NowSecs();
        if (next == 0.0) { next = now + secs; continue; }
        if (now < next) continue;
        next = now + secs;
        Report();
      }
    }).detach();
  }
} g_auto_reporter;
}  // namespace

}  // namespace census
}  // namespace ngpu
}  // namespace fable2
