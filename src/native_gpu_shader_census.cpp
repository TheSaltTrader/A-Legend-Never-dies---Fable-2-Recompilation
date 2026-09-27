#include "native_gpu_shader_census.h"
#include "native_gpu_census.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/graphics/format/ucode.h>
#include <rex/graphics/pipeline/shader/dxbc_translator.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/pipeline/shader/translator.h>
#include <rex/graphics/xenos.h>
#include <rex/string/buffer.h>
#include <rex/ui/graphics_provider.h>

#define XXH_INLINE_ALL
#include "xxhash.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

REXCVAR_DEFINE_INT32(ngpu_shader_census_failures, 24, "GPU",
    "Native-GPU shader census: how many translation failures to name in the report (each with the SDK translator's own error text)");
REXCVAR_DEFINE_STRING(ngpu_shader_diff_dir, "", "GPU",
    "Native-GPU shader census: a directory the PLUGIN dumped shaders into (its dump_shaders cvar) - the in-app translator "
    "re-translates every ucode there with the dumped modification and compares its DXBC byte-for-byte with the plugin's "
    "(the oracle). Runs once on the census worker; results in the census report");

#include <filesystem>
namespace fable2::ngpu::xlat { void LogCvarOwnership(); }

namespace ngc = fable2::ngpu::census;

namespace fable2 {
namespace ngpu {
namespace shader_census {
namespace {

using namespace rex::graphics;

// The SEEN axis for the shader ISA, from the SDK's parser: ShaderTranslator
// decodes every control-flow, fetch and ALU instruction of an analysed shader
// and hands each one to these hooks. Nothing is decoded here - the opcode
// fields come from the SDK's ParsedXxxInstruction structs.
struct OpSet {
  std::vector<std::pair<ngc::Kind, uint32_t>> ops;
  void Add(ngc::Kind k, uint32_t id) {
    for (const auto& o : ops) if (o.first == k && o.second == id) return;
    ops.emplace_back(k, id);
  }
};

class OpCensusTranslator : public ShaderTranslator {
 public:
  OpCensusTranslator() = default;
  OpSet set;

 protected:
  void PreProcessControlFlowInstructions(std::vector<ucode::ControlFlowInstruction> instrs) override {
    for (const auto& cf : instrs) set.Add(ngc::Kind::ShaderControlFlow, uint32_t(cf.opcode()));
  }
  void ProcessVertexFetchInstruction(const ParsedVertexFetchInstruction& instr) override {
    set.Add(ngc::Kind::ShaderFetch, uint32_t(instr.opcode));
  }
  void ProcessTextureFetchInstruction(const ParsedTextureFetchInstruction& instr) override {
    set.Add(ngc::Kind::ShaderFetch, uint32_t(instr.opcode));
  }
  void ProcessAluInstruction(const ParsedAluInstruction& instr, uint8_t /*memexport*/) override {
    // The two halves of an ALU instruction are separate subjects; a half that
    // is the encoding's default no-op is not an op the shader uses.
    if (!instr.IsVectorOpDefaultNop()) set.Add(ngc::Kind::ShaderAluVector, uint32_t(instr.vector_opcode));
    if (!instr.IsScalarOpDefaultNop()) set.Add(ngc::Kind::ShaderAluScalar, uint32_t(instr.scalar_opcode));
  }
};

struct Job { bool pixel = false; std::vector<uint32_t> be; uint32_t addr = 0; uint64_t hash = 0; bool diff = false; bool from_object = false; };

std::mutex g_mu;
std::condition_variable g_cv;
std::deque<Job> g_queue;
std::unordered_set<uint64_t> g_seen_hashes;
std::thread g_thread;
bool g_thread_started = false;

// Results (under g_mu).
struct Stats {
  uint64_t noted = 0, unique = 0, vs = 0, ps = 0;
  uint64_t analyzed_ok = 0, analyzed_fail = 0;
  uint64_t xlat_ok = 0, xlat_fail = 0;
  uint64_t refused = 0;   // blobs the structural guard would not hand to the SDK analyser (counted, on disk)
  // THE M5 COHERENCE QUESTION, measured per shader from the SDK's own analysis:
  // is the set of guest ranges a draw reads statically predictable at the draw
  // hook? Breakers: memexport (the shader WRITES guest memory through the
  // mirror), register dynamic addressing, and a vertex fetch whose index does
  // not come from the vertex-index register (r0.x at shader start) - a
  // runtime-computed address reads bytes no prediction uploaded.
  uint64_t coh_shaders = 0, coh_memexport = 0, coh_dyn_addr = 0;
  uint64_t coh_vfetch = 0, coh_vfetch_r0x = 0, coh_vfetch_other = 0, coh_shaders_vfetch_other = 0;
  std::vector<std::string> coh_notes;   // the first shaders that break predictability, named
  uint64_t noted_objects = 0, unique_objects = 0;   // fed from the SetShader hook's shader object (the D3D9-level source)
  uint64_t confirmed_by_dump = 0;   // object-derived blobs whose hash names one of the plugin's own ucode dumps: offset+length exactly the GPU's load
  uint64_t dxbc_bytes = 0;
  uint64_t ops_seen = 0, ops_marked_ported = 0;
  std::vector<std::string> failures;   // "<hash> v/p addr: error" - capped by the cvar
} g_stats;

const char* kReplaces =
    "rexgpu-xenos pipeline/shader/dxbc_translator*.cpp (in-app copy under src/native_gpu_xlat; the plugin's copy "
    "still translates the same shaders for the main window - the native draw path does not consume this DXBC yet)";

// The SDK's analyser TRUSTS its input: an exec block whose instructions lie
// outside the ucode is read out of bounds, and a texture-fetch opcode it does
// not name leaves ParsedTextureFetchInstruction::opcode_name null, which the
// disassembler then strlen()s - the crash that killed two runs (blob
// FCE031900C090BE7: 27 dwords of guest POINTERS, an IM_LOAD the ring parser
// mis-read, not a shader). This guard makes those structural checks first,
// using the SDK's own unions for every field and mirroring its walk exactly
// (translator.cpp AnalyzeUcode / GatherExecInformation). It decides only
// whether the blob is handed on; a refusal is COUNTED, never silent.
static const char* ValidateUcode(const std::vector<uint32_t>& u) {
  using namespace ucode;
  // The SDK tolerates a size that is not a multiple of 3 (it uses size/3 and
  // ignores the tail), and IM_LOAD sizes in the wild are not always multiples;
  // the first version refused 33 real shaders on that. Mirror the SDK.
  if (u.size() < 3) return "shorter than one 3-dword instruction";
  const uint32_t units = uint32_t(u.size() / 3);
  // The control-flow program's length is found the way the SDK finds it: walk
  // pairs while i < bound, SHRINKING the bound to the first exec's address as
  // it goes - so the ALU/fetch instruction data past the program is never read
  // as control flow. (The first version walked every unit and read instruction
  // data as CF: garbage 'exec' opcodes with address 0 made every real shader
  // look programless - 390 of 406 plugin dumps and 170 of 175 live blobs were
  // refused for that. A guard that refuses the real thing is worse than none.)
  uint32_t bound = units;
  for (uint32_t i = 0; i < bound; ++i) {
    ControlFlowInstruction ab[2];
    UnpackControlFlowInstructions(u.data() + i * 3, ab);
    for (uint32_t j = 0; j < 2; ++j)
      if (IsControlFlowOpcodeExec(ab[j].opcode())) bound = std::min(bound, ab[j].exec.address());
  }
  if (bound == 0) return "first exec block starts at instruction 0 - no control-flow program";
  // The hardware limit is 4096 instructions (3 dwords each); a blob past that
  // is not a Xenos shader whatever else it passes.
  if (units > 4096 * 2) return "longer than any Xenos shader can be";
  auto check_exec = [&](uint32_t addr, uint32_t count, uint32_t sequence) -> const char* {
    if (uint64_t(addr) + count > units) return "exec block reaches outside the ucode";
    // Instruction data lives AFTER the control-flow program; an exec pointing
    // into the CF program itself is the mark of a blob that is not a shader.
    if (addr < bound) return "exec block points into the control-flow program";
    for (uint32_t k = 0; k < count; ++k, sequence >>= 2) {
      if (!(sequence & 1)) continue;   // ALU: both opcode tables are complete (32/64 entries)
      const auto& op = *reinterpret_cast<const FetchInstruction*>(u.data() + (addr + k) * 3);
      switch (op.opcode()) {
        case FetchOpcode::kVertexFetch: case FetchOpcode::kTextureFetch: case FetchOpcode::kGetTextureBorderColorFrac:
        case FetchOpcode::kGetTextureComputedLod: case FetchOpcode::kGetTextureGradients: case FetchOpcode::kGetTextureWeights:
        case FetchOpcode::kSetTextureLod: case FetchOpcode::kSetTextureGradientsHorz: case FetchOpcode::kSetTextureGradientsVert:
          break;
        default: return "fetch opcode the SDK does not name";
      }
    }
    return nullptr;
  };
  for (uint32_t i = 0; i < bound; ++i) {
    ControlFlowInstruction ab[2];
    UnpackControlFlowInstructions(u.data() + i * 3, ab);
    for (uint32_t j = 0; j < 2; ++j) {
      const ControlFlowInstruction& cf = ab[j];
      const char* why = nullptr;
      switch (cf.opcode()) {
        case ControlFlowOpcode::kExec: case ControlFlowOpcode::kExecEnd:
          why = check_exec(cf.exec.address(), cf.exec.count(), cf.exec.sequence()); break;
        case ControlFlowOpcode::kCondExec: case ControlFlowOpcode::kCondExecEnd:
        case ControlFlowOpcode::kCondExecPredClean: case ControlFlowOpcode::kCondExecPredCleanEnd:
          why = check_exec(cf.cond_exec.address(), cf.cond_exec.count(), cf.cond_exec.sequence()); break;
        case ControlFlowOpcode::kCondExecPred: case ControlFlowOpcode::kCondExecPredEnd:
          why = check_exec(cf.cond_exec_pred.address(), cf.cond_exec_pred.count(), cf.cond_exec_pred.sequence()); break;
        case ControlFlowOpcode::kCondCall: if (cf.cond_call.address() >= bound * 2) why = "call target outside the control-flow program"; break;
        case ControlFlowOpcode::kCondJmp: if (cf.cond_jmp.address() >= bound * 2) why = "jump target outside the control-flow program"; break;
        case ControlFlowOpcode::kLoopStart: if (cf.loop_start.address() >= bound * 2) why = "loop target outside the control-flow program"; break;
        case ControlFlowOpcode::kLoopEnd: if (cf.loop_end.address() >= bound * 2) why = "loop target outside the control-flow program"; break;
        default: break;
      }
      if (why) return why;
    }
  }
  return nullptr;
}

void Process(const Job& job) {
  const xenos::ShaderType type = job.pixel ? xenos::ShaderType::kPixel : xenos::ShaderType::kVertex;
  // NAME THE INPUT BEFORE TOUCHING IT. The first run with native draws died in
  // AnalyzeUcode (strlen of a null name under StringBuffer::Append) on this
  // thread, with nothing in the log to say which shader; a crash must name its
  // input. The raw big-endian bytes are also written out so the offending blob
  // can be analysed offline, and the phase is logged so the fault localises.
  {
    static uint32_t logged = 0;
    if (logged < 400) {
      ++logged;
      REXLOG_INFO("[ngpu-census] shader {:016X} {} @{:08X} {} dw, head {:08X} {:08X} {:08X} {:08X}: analysing",
                  job.hash, job.pixel ? 'p' : 'v', job.addr, job.be.size(),
                  job.be.size() > 0 ? job.be[0] : 0, job.be.size() > 1 ? job.be[1] : 0,
                  job.be.size() > 2 ? job.be[2] : 0, job.be.size() > 3 ? job.be[3] : 0);
    }
    std::error_code ec;
    std::filesystem::create_directories("ngpu_shader_census", ec);
    char name[96];
    std::snprintf(name, sizeof(name), "ngpu_shader_census/%016llX_%c.ucode.be.bin", (unsigned long long)job.hash, job.pixel ? 'p' : 'v');
    if (FILE* f = std::fopen(name, "wb")) { std::fwrite(job.be.data(), sizeof(uint32_t), job.be.size(), f); std::fclose(f); }
  }
  Shader shader(type, job.hash, job.be.data(), job.be.size(), std::endian::big);
  if (const char* why = ValidateUcode(shader.ucode_data())) {
    // Not a shader (or not one the SDK can walk): refused, counted on the
    // census's refused axis and here, never analysed. The blob is on disk above.
    ngc::Refused(ngc::Kind::ShaderControlFlow, uint32_t(job.hash));
    static uint32_t logged = 0;
    if (logged < 64) { ++logged; REXLOG_INFO("[ngpu-census] shader {:016X} {} @{:08X} ({} dw) REFUSED before analysis: {} - blob written to ngpu_shader_census/", job.hash, job.pixel ? 'p' : 'v', job.addr, job.be.size(), why); }
    std::lock_guard<std::mutex> lk(g_mu);
    ++g_stats.unique;
    ++g_stats.refused;
    return;
  }
  rex::string::StringBuffer disasm(4096);
  shader.AnalyzeUcode(disasm);
  const bool analyzed = shader.is_ucode_analyzed();
  {
    static uint32_t logged = 0;
    if (logged < 400) { ++logged; REXLOG_INFO("[ngpu-census] shader {:016X} {}: analysed={} ({} bytes of disassembly); translating", job.hash, job.pixel ? 'p' : 'v', analyzed, disasm.length()); }
  }
  // THE SDK'S BINDING MAP for this shader, written beside the blob so the
  // unverified translator (XenosRecomp, whose HLSL names each texture fetch's
  // fetch-constant slot through g_Sampler<N>_Texture<dim>DescriptorIndex) can
  // be differentialled against the verified one offline, shader by shader:
  // texture bindings (fetch constant, dimension) and vertex bindings (fetch
  // constant, stride in words, attribute count). A slot XenosRecomp samples
  // that the SDK says the microcode never fetches - or the reverse - is a
  // translation divergence, the class the striped branches would be in.
  if (analyzed) {
    char name[96];
    std::snprintf(name, sizeof(name), "ngpu_shader_census/%016llX_%c.bindings.txt", (unsigned long long)job.hash, job.pixel ? 'p' : 'v');
    if (FILE* f = std::fopen(name, "wb")) {
      for (const auto& tb : shader.texture_bindings())
        std::fprintf(f, "tex %u dim %u\n", tb.fetch_constant, uint32_t(tb.fetch_instr.dimension));
      for (const auto& vb : shader.vertex_bindings())
        std::fprintf(f, "vtx %u stride %u attrs %zu\n", vb.fetch_constant, vb.stride_words, vb.attributes.size());
      std::fprintf(f, "memexport %d dynaddr %d\n", shader.memexport_stream_constants().empty() ? 0 : 1,
                   (shader.uses_register_dynamic_addressing() || shader.constant_register_map().float_dynamic_addressing) ? 1 : 0);
      std::fclose(f);
    }
  }
  // Coherence facts for this shader (see Stats::coh_*): read from the SDK's
  // analysis, not decoded here.
  bool coh_memexport = false, coh_dyn = false;
  uint32_t coh_vf = 0, coh_vf_r0x = 0, coh_vf_other = 0;
  std::string coh_note;
  if (analyzed) {
    coh_memexport = !shader.memexport_stream_constants().empty();
    coh_dyn = shader.uses_register_dynamic_addressing() || shader.constant_register_map().float_dynamic_addressing;
    for (const auto& vb : shader.vertex_bindings()) {
      for (const auto& attr : vb.attributes) {
        const auto& fi = attr.fetch_instr;
        if (fi.is_mini_fetch) continue;   // inherits the full fetch's index operand
        ++coh_vf;
        const auto& op = fi.operands[0];
        const bool r0x = fi.operand_count >= 1 && op.storage_source == InstructionStorageSource::kRegister && op.storage_index == 0 &&
                         op.storage_addressing_mode == InstructionStorageAddressingMode::kAbsolute && op.components[0] == SwizzleSource::kX;
        if (r0x) ++coh_vf_r0x; else ++coh_vf_other;
      }
    }
    if (coh_memexport || coh_dyn || coh_vf_other) {
      char b[160];
      std::snprintf(b, sizeof(b), "%016llX %c: memexport=%d dynamic_addressing=%d vfetch from non-index register %u of %u", (unsigned long long)job.hash, job.pixel ? 'p' : 'v', coh_memexport ? 1 : 0, coh_dyn ? 1 : 0, coh_vf_other, coh_vf);
      coh_note = b;
    }
  }

  // SEEN: the SDK parser walks the shader; the hooks record the ops.
  OpCensusTranslator census;
  Shader::Translation* tc = shader.GetOrCreateTranslation(0xC0DEC0DEull);
  if (analyzed && tc) census.TranslateAnalyzedShader(*tc);
  for (const auto& op : census.set.ops) ngc::See(op.first, op.second);

  // PORTED (by the in-app DXBC translator): translate with the default
  // modification for this stage. dynamic_addressable_register_count is the
  // hardware maximum here (SQ_PROGRAM_CNTL is not known at census time) - that
  // affects the generated code's register bound, not whether an op translates.
  bool ok = false;
  std::string err;
  size_t dxbc = 0;
  if (analyzed) {
    DxbcShaderTranslator dx(rex::ui::GraphicsProvider::GpuVendorID::kNvidia,
                            /*bindless_resources_used=*/false, /*edram_rov_used=*/false);
    const uint64_t mod = job.pixel
        ? dx.GetDefaultPixelShaderModification(xenos::kMaxShaderTempRegisters)
        : dx.GetDefaultVertexShaderModification(xenos::kMaxShaderTempRegisters, Shader::HostVertexShaderType::kVertex);
    Shader::Translation* t = shader.GetOrCreateTranslation(mod);
    if (t) {
      const bool ret = dx.TranslateAnalyzedShader(*t);
      ok = ret && t->is_valid() && !t->translated_binary().empty();
      dxbc = t->translated_binary().size();
      for (const auto& e : t->errors()) { if (!err.empty()) err += " | "; err += e.is_fatal ? "FATAL: " : "warn: "; err += e.message; }
      if (!ok && err.empty()) err = ret ? "translator returned true but the translation is not valid / empty" : "translator returned false without an error record";
    }
  } else {
    err = "AnalyzeUcode did not analyse the shader";
  }
  {
    static uint32_t logged = 0;
    if (logged < 400) { ++logged; REXLOG_INFO("[ngpu-census] shader {:016X} {}: translated ok={} ({} bytes DXBC){}{}", job.hash, job.pixel ? 'p' : 'v', ok, dxbc, err.empty() ? "" : " - ", err); }
  }

  // Census marks OUTSIDE this module's lock: census::Report() holds the census
  // lock while calling shader_census::Report() (census -> shader order), so
  // taking them in the other order here would be a deadlock waiting to happen.
  if (ok) {
    // Every op this shader contains was handled by the in-app translator.
    for (const auto& op : census.set.ops) {
      ngc::MarkPorted(op.first, op.second);
      ngc::MarkReplaces(op.first, op.second, kReplaces, /*emulated_dead=*/false);
    }
  }

  // Object-derived blobs: does the hash name one of the plugin's own ucode
  // dumps? If so the object's 0x80 offset and psize-0x80 length are EXACTLY the
  // range the GPU loaded - the plugin hashes that range - byte for byte.
  bool confirmed = false;
  if (job.from_object) {
    const std::string dir = REXCVAR_GET(ngpu_shader_diff_dir);
    if (!dir.empty()) {
      char nm[80];
      std::snprintf(nm, sizeof(nm), "shader_%016llX.ucode.bin.%s", (unsigned long long)job.hash, job.pixel ? "frag" : "vert");
      std::error_code ec;
      confirmed = std::filesystem::exists(std::filesystem::path(dir) / nm, ec);
    }
  }

  std::lock_guard<std::mutex> lk(g_mu);
  ++g_stats.unique;
  if (job.from_object) { ++g_stats.unique_objects; if (confirmed) ++g_stats.confirmed_by_dump; }
  if (job.pixel) ++g_stats.ps; else ++g_stats.vs;
  if (analyzed) ++g_stats.analyzed_ok; else ++g_stats.analyzed_fail;
  g_stats.ops_seen += census.set.ops.size();
  if (analyzed) {
    ++g_stats.coh_shaders;
    if (coh_memexport) ++g_stats.coh_memexport;
    if (coh_dyn) ++g_stats.coh_dyn_addr;
    g_stats.coh_vfetch += coh_vf; g_stats.coh_vfetch_r0x += coh_vf_r0x; g_stats.coh_vfetch_other += coh_vf_other;
    if (coh_vf_other) ++g_stats.coh_shaders_vfetch_other;
    if (!coh_note.empty() && g_stats.coh_notes.size() < 24) g_stats.coh_notes.push_back(coh_note);
  }
  if (ok) {
    ++g_stats.xlat_ok;
    g_stats.dxbc_bytes += dxbc;
    g_stats.ops_marked_ported += census.set.ops.size();
  } else {
    ++g_stats.xlat_fail;
    if (g_stats.failures.size() < size_t(std::max(0, REXCVAR_GET(ngpu_shader_census_failures)))) {
      char head[64];
      std::snprintf(head, sizeof(head), "%016llX %c @%08X (%zu dw): ", (unsigned long long)job.hash, job.pixel ? 'p' : 'v', job.addr, job.be.size());
      g_stats.failures.emplace_back(std::string(head) + err);
    }
  }
}

// ---- The DXBC differential against the plugin's OWN translation ------------
// The plugin (rexgpu-xenos.dll) is the oracle: with its `dump_shaders=<dir>`
// cvar set it writes, for every shader it translates, the ucode
// (shader_<hash>.ucode.bin.<vert|frag>, host-endian dwords) and its DXBC
// (shader_<hash>_<modification>.d3d12[_rtv|_rov].bin.<vert|frag>). Same ucode,
// same modification, two copies of the translator, byte comparison. Where the
// bytes match, every op in that shader is VERIFIED against a named oracle for
// the parameter instances the game uses; where they differ, the first
// differing offset is named. The oracle's constructor parameters that the dump
// does not record (bindless, gamma-as-unorm8, msaa2x, resolution scale) are
// RECOVERED by trying the combinations - a match names the combination, and a
// shader that matches under none is a real divergence (or a scale the sweep
// did not try; the report says which scales were tried).
// The process start, for the same-run reconciliation against the plugin's dump.
static const std::filesystem::file_time_type g_process_start = std::filesystem::file_time_type::clock::now();

struct DiffStats {
  uint64_t dxbc_files = 0, no_ucode = 0, match = 0, mismatch = 0, unreadable = 0;
  uint64_t ops_verified = 0;
  std::vector<std::string> notes;
  bool ran = false;
  std::string dir;
} g_diff;

static bool ReadAll(const std::filesystem::path& p, std::vector<uint8_t>& out) {
  FILE* f = nullptr;
  const std::string s = p.string();
  f = std::fopen(s.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  out.resize(n > 0 ? size_t(n) : 0);
  const size_t got = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
  std::fclose(f);
  return got == out.size();
}

// WHAT BYTE-IDENTITY PROVES, AND WHAT IT DOES NOT. The vendored translator and
// the plugin were built from the same rexglue-src commit, so identical output
// is the EXPECTED result: it proves the LIFT IS FAITHFUL - the transplant is
// unmodified, the integration feeds the same inputs, the build configuration
// (bindless / gamma / msaa / scale) was correctly recovered. It does not prove
// any op correct in a deeper sense: both sides are one code compiled twice.
// And it is a LIFT-PHASE oracle only: the moment an op is OPTIMISED, this test
// fails by design, and a test meant to fail is indistinguishable from a
// regression. Optimisation needs a SEMANTIC oracle - same rendered result on
// the same inputs - not the same bytes. Decided here so it is not discovered
// mid-optimisation.
static const char* kOracle =
    "VERIFIED AS A FAITHFUL LIFT (not 'verified correct'): byte-identical DXBC vs the plugin's own translation of the "
    "same ucode+modification (rexgpu-xenos dump_shaders; both built from rexglue-src 0cb9040), for the parameter "
    "instances in the game's shaders. LIFT-PHASE oracle only - optimisation needs a semantic (rendered-result) oracle";

void RunDiff(const std::string& dir) {
  namespace fs = std::filesystem;
  DiffStats st;
  st.dir = dir;
  st.ran = true;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    st.notes.push_back("directory not found: " + dir);
    std::lock_guard<std::mutex> lk(g_mu); g_diff = std::move(st); return;
  }
  const int scales[] = {1, 2, 3, 4};
  std::vector<uint8_t> ucode_bytes, oracle;
  for (const auto& e : fs::directory_iterator(dir, ec)) {
    const std::string name = e.path().filename().string();
    // shader_<hash16>_<mod16>.<prefix>.bin.<vert|frag>
    if (name.rfind("shader_", 0) != 0 || name.find(".bin.") == std::string::npos || name.find(".ucode.") != std::string::npos) continue;
    if (name.size() < 7 + 16 + 1 + 16) continue;
    const std::string hash_s = name.substr(7, 16), mod_s = name.substr(24, 16);
    if (name[23] != '_') continue;
    const bool pixel = name.size() >= 4 && name.compare(name.size() - 4, 4, "frag") == 0;
    const bool rov = name.find(".d3d12_rov.") != std::string::npos;
    uint64_t hash = 0, mod = 0;
    try { hash = std::stoull(hash_s, nullptr, 16); mod = std::stoull(mod_s, nullptr, 16); } catch (...) { continue; }
    ++st.dxbc_files;
    const fs::path ucode_path = fs::path(dir) / ("shader_" + hash_s + ".ucode.bin." + (pixel ? "frag" : "vert"));
    if (!ReadAll(ucode_path, ucode_bytes) || ucode_bytes.size() < 4 || (ucode_bytes.size() & 3)) { ++st.no_ucode; continue; }
    if (!ReadAll(e.path(), oracle) || oracle.empty()) { ++st.unreadable; continue; }
    const xenos::ShaderType type = pixel ? xenos::ShaderType::kPixel : xenos::ShaderType::kVertex;
    const uint32_t* ucode = reinterpret_cast<const uint32_t*>(ucode_bytes.data());
    const size_t ucode_n = ucode_bytes.size() / 4;
    {
      Shader probe(type, hash, ucode, ucode_n, std::endian::native);
      if (ValidateUcode(probe.ucode_data())) { ++st.unreadable; continue; }   // the same guard as the live path
      rex::string::StringBuffer disasm(4096);
      probe.AnalyzeUcode(disasm);
      if (!probe.is_ucode_analyzed()) { ++st.unreadable; continue; }
    }
    bool matched = false;
    std::string best;   // the closest attempt, for the report
    size_t best_at = SIZE_MAX;
    OpSet matched_ops;
    for (int scale : scales) {
      for (uint32_t combo = 0; combo < 8 && !matched; ++combo) {
        const bool bindless = combo & 1, gamma = combo & 2, msaa2x = !(combo & 4);
        DxbcShaderTranslator dx(rex::ui::GraphicsProvider::GpuVendorID::kNvidia, bindless, rov, gamma, msaa2x, uint32_t(scale), uint32_t(scale));
        // A FRESH Shader per attempt: a Shader caches one Translation per
        // modification value and the translator reads the modification from
        // the Translation, so re-translating the same (shader, mod) would hit
        // the cached object. Analysis is milliseconds; correctness first.
        Shader shader(type, hash, ucode, ucode_n, std::endian::native);
        rex::string::StringBuffer disasm(4096);
        shader.AnalyzeUcode(disasm);
        Shader::Translation* real = shader.GetOrCreateTranslation(mod);
        if (!real) continue;
        const bool ret = dx.TranslateAnalyzedShader(*real);
        const auto& mine = real->translated_binary();
        if (ret && real->is_valid() && !mine.empty() && mine.size() == oracle.size() && std::memcmp(mine.data(), oracle.data(), mine.size()) == 0) {
          OpCensusTranslator census;
          Shader::Translation* tc = shader.GetOrCreateTranslation(0xC0DEC0DEull);
          if (tc) census.TranslateAnalyzedShader(*tc);
          matched_ops = census.set;
        }
        if (ret && real->is_valid() && !mine.empty()) {
          size_t at = 0;
          const size_t n = std::min(mine.size(), oracle.size());
          while (at < n && mine[at] == oracle[at]) ++at;
          if (at == n && mine.size() == oracle.size()) {
            matched = true;
            char b[160];
            std::snprintf(b, sizeof(b), "bindless=%d gamma_unorm8=%d msaa2x=%d scale=%d", bindless ? 1 : 0, gamma ? 1 : 0, msaa2x ? 1 : 0, scale);
            best = b;
          } else if (at < best_at) {
            best_at = at;
            char b[200];
            std::snprintf(b, sizeof(b), "closest: bindless=%d gamma_unorm8=%d msaa2x=%d scale=%d differs at byte %zu of %zu (oracle %zu bytes)",
                          bindless ? 1 : 0, gamma ? 1 : 0, msaa2x ? 1 : 0, scale, at, mine.size(), oracle.size());
            best = b;
          }
        } else if (best.empty()) {
          best = "in-app translation FAILED";
          for (const auto& err : real->errors()) best += " | " + err.message;
        }
      }
      if (matched) break;
    }
    if (matched) {
      ++st.match;
      // Every op this shader uses is verified for these parameter instances.
      for (const auto& op : matched_ops.ops) {
        ngc::MarkVerified(op.first, op.second, kOracle);
        ngc::MarkReplaces(op.first, op.second, kReplaces, /*emulated_dead=*/false);   // still DUPLICATED: the plugin translates too
        ++st.ops_verified;
      }
      if (st.notes.size() < 4) st.notes.push_back(name + ": MATCH (" + best + ")");
    } else {
      ++st.mismatch;
      if (st.notes.size() < size_t(std::max(0, REXCVAR_GET(ngpu_shader_census_failures))))
        st.notes.push_back(name + ": MISMATCH " + best);
    }
  }
  std::lock_guard<std::mutex> lk(g_mu);
  g_diff = std::move(st);
}

void Worker() {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lk(g_mu);
      g_cv.wait(lk, [] { return !g_queue.empty(); });
      job = std::move(g_queue.front());
      g_queue.pop_front();
    }
    if (job.diff) RunDiff(REXCVAR_GET(ngpu_shader_diff_dir));
    else Process(job);
  }
}

}  // namespace

// The structural guard, for the phase A SDK draw path too (native_gpu_sdk_xlat.cpp): a blob it refuses is never
// handed to AnalyzeUcode (ALL1, 2026-09-25: an unguarded analysis strlen'd a null fetch-opcode name and crashed).
const char* ValidateShaderUcode(const std::vector<uint32_t>& host_endian_ucode) { return ValidateUcode(host_endian_ucode); }

void Note(bool pixel, const uint32_t* be_dwords, size_t n, uint32_t guest_addr, bool from_object) {
  if (!be_dwords || n == 0 || n > 65536) return;
  const uint64_t hash = XXH3_64bits(be_dwords, n * sizeof(uint32_t));
  std::lock_guard<std::mutex> lk(g_mu);
  ++g_stats.noted;
  if (from_object) ++g_stats.noted_objects;
  if (!g_seen_hashes.insert(hash ^ (pixel ? 0x9E3779B97F4A7C15ull : 0)).second) return;
  Job job;
  job.pixel = pixel;
  job.be.assign(be_dwords, be_dwords + n);
  job.addr = guest_addr;
  job.hash = hash;
  job.from_object = from_object;
  g_queue.push_back(std::move(job));
  if (!g_thread_started) { g_thread_started = true; g_thread = std::thread(Worker); g_thread.detach(); }
  g_cv.notify_one();
}

void Report() {
  fable2::ngpu::xlat::LogCvarOwnership();
  // The DXBC differential against the plugin's dump: queued ONCE, on the worker.
  {
    static bool queued = false;
    const std::string dir = REXCVAR_GET(ngpu_shader_diff_dir);
    if (!queued && !dir.empty()) {
      queued = true;
      std::lock_guard<std::mutex> lk(g_mu);
      Job job; job.diff = true;
      g_queue.push_back(std::move(job));
      if (!g_thread_started) { g_thread_started = true; g_thread = std::thread(Worker); g_thread.detach(); }
      g_cv.notify_one();
    }
  }
  std::lock_guard<std::mutex> lk(g_mu);
  if (g_diff.ran) {
    const DiffStats& d = g_diff;
    REXLOG_INFO("[ngpu-census] DXBC DIFFERENTIAL vs the plugin's dump ({}): {} plugin DXBC files; MATCH {} (byte-identical, every op in those shaders "
                "marked VERIFIED - {} op marks), MISMATCH {}, no ucode {}, unreadable {}. Oracle constructor parameters not in the dump "
                "(bindless / gamma-as-unorm8 / msaa2x, scale 1-4) were recovered by search; a MATCH names the combination.",
                d.dir, d.dxbc_files, d.match, d.ops_verified, d.mismatch, d.no_ucode, d.unreadable);
    for (const auto& n : d.notes) REXLOG_INFO("[ngpu-census] DXBC diff: {}", n);
    static bool verdict_written = false;
    if (!verdict_written) {
      verdict_written = true;
      char v[300];
      std::snprintf(v, sizeof(v), "DXBC DIFFERENTIAL vs the plugin's dump: %llu files, MATCH %llu, MISMATCH %llu, no-ucode %llu, unreadable %llu (VERIFIED AS A FAITHFUL LIFT: same source compiled twice; lift-phase oracle only)",
                    (unsigned long long)d.dxbc_files, (unsigned long long)d.match, (unsigned long long)d.mismatch, (unsigned long long)d.no_ucode, (unsigned long long)d.unreadable);
      ngc::Verdict(v);
    }
  }
  const Stats& s = g_stats;
  // THE SAME-RUN RECONCILIATION: the plugin re-dumps every shader it
  // translates in a process, so the ucode files with a modification time after
  // this process started ARE the plugin's own count of unique shaders THIS
  // run - printed beside this feed's count so a hole in the feed is a number,
  // not a suspicion (leg E: plugin 238, hook feed 127 - the SetShader hooks
  // miss a setter path; the device's shader pair is now read at every draw).
  uint64_t plugin_run_vs = 0, plugin_run_ps = 0;
  {
    // Captured at STATIC INIT (g_process_start below), not at the first report:
    // the plugin translates and dumps every shader during the boot and menu,
    // BEFORE the first report 10 s in, and a start time taken there counted
    // 1 of 238 (leg F). The whole run's window, or the count is a lie.
    const auto process_start = g_process_start;
    const std::string dir = REXCVAR_GET(ngpu_shader_diff_dir);
    if (!dir.empty()) {
      std::error_code ec;
      for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        const std::string name = e.path().filename().string();
        if (name.find(".ucode.bin.") == std::string::npos) continue;
        if (e.last_write_time(ec) < process_start) continue;
        if (name.compare(name.size() - 4, 4, "vert") == 0) ++plugin_run_vs; else ++plugin_run_ps;
      }
    }
  }
  {
    static std::string last;
    char v[360];
    std::snprintf(v, sizeof(v), "SHADER ISA: noted %llu (objects %llu) -> unique %llu (objects %llu, confirmed by plugin dump %llu), refused %llu, translated %llu ok / %llu failed | "
                  "PLUGIN dumped THIS RUN: %llu VS + %llu PS = %llu (the same-run reference count; the feed's unique below it is a HOLE)",
                  (unsigned long long)s.noted, (unsigned long long)s.noted_objects, (unsigned long long)s.unique, (unsigned long long)s.unique_objects,
                  (unsigned long long)s.confirmed_by_dump, (unsigned long long)s.refused, (unsigned long long)s.xlat_ok, (unsigned long long)s.xlat_fail,
                  (unsigned long long)plugin_run_vs, (unsigned long long)plugin_run_ps, (unsigned long long)(plugin_run_vs + plugin_run_ps));
    if (last != v) { last = v; ngc::Verdict(v); REXLOG_INFO("[ngpu-census] {}", v); }
  }
  {
    // The M5 coherence answer, from the SDK's analysis of every shader seen.
    static std::string last;
    char v[420];
    std::snprintf(v, sizeof(v), "M5 COHERENCE (is every draw's guest read-set statically predictable?): %llu shaders analysed; memexport %llu; register/constant dynamic addressing %llu; "
                  "vertex fetches %llu of which %llu index from r0.x (the vertex index) and %llu from another register (runtime-computed address) in %llu shaders. "
                  "All three zero => predict-and-upload is sound for these shaders; any non-zero => named below, a resident coherent mirror or an exception list is needed for those",
                  (unsigned long long)s.coh_shaders, (unsigned long long)s.coh_memexport, (unsigned long long)s.coh_dyn_addr,
                  (unsigned long long)s.coh_vfetch, (unsigned long long)s.coh_vfetch_r0x, (unsigned long long)s.coh_vfetch_other, (unsigned long long)s.coh_shaders_vfetch_other);
    if (last != v) {
      last = v; ngc::Verdict(v); REXLOG_INFO("[ngpu-census] {}", v);
      for (const auto& n : s.coh_notes) { REXLOG_INFO("[ngpu-census] M5 coherence breaker: {}", n); std::string vv = "M5 coherence breaker: " + n; ngc::Verdict(vv.c_str()); }
    }
  }
  REXLOG_INFO("[ngpu-census] shader ISA (SDK parser = SEEN, in-app DxbcShaderTranslator = PORTED): microcode noted {} (from SetShader objects {}) -> unique blobs {} "
              "(from objects {}, of which {} CONFIRMED by a plugin ucode dump of the same hash = the object offset/length is exactly the GPU's load); "
              "vs {}, ps {}; REFUSED by the structural guard {} (not shaders / not walkable - on disk); analysed {} ok / {} failed; translated {} ok / {} FAILED "
              "({} KB of DXBC); distinct ops seen {} (per shader, summed), ops marked ported {}. A failed translation leaves its ops seen-unported "
              "(a FLOOR: the failing op is not identified). Ledger: DUPLICATED - the plugin still translates the same shaders for the main window; "
              "REPLACED only when the native draw path consumes this DXBC. Pending in queue: {}.",
              s.noted, s.noted_objects, s.unique, s.unique_objects, s.confirmed_by_dump, s.vs, s.ps, s.refused, s.analyzed_ok, s.analyzed_fail,
              s.xlat_ok, s.xlat_fail, s.dxbc_bytes >> 10, s.ops_seen, s.ops_marked_ported, g_queue.size());
  for (const auto& f : s.failures) REXLOG_INFO("[ngpu-census] shader translation FAILED: {}", f);
}

}  // namespace shader_census
}  // namespace ngpu
}  // namespace fable2
