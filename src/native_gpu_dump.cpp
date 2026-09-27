// Native-GPU draw dump (M4 census 3). For a window of frames it writes one
// line per draw with everything the draw reads from the XDK device struct:
//
//   D f<frame> <kind> prim=<D3DPT> base=<n> start=<n> count=<n>
//     ib=<object>:<word0>/<word6> vs=<shader obj> ps=<shader obj>
//     ct=<table>+<base>:<n> rt=<surface>:<index>
//     fc<i>=<word0>/<word1>/<word2>  (each of the 32 fetch constants at
//                                     device+0x480 whose word 0 is not 0)
//   S f<frame> shader=<obj> type=<t>          (SetShader)
//   C f<frame> table=<t> base=<b> base2=<b2> n=<n>   (LoadShaderConstants)
//   R f<frame> surface=<s> index=<i>          (SetRenderTarget)
//   F <frame>                                  (Present)
//
// Cvars (FABLE2_TUNE): ngpu_dump_at_frame = first frame to record (0 = off),
// ngpu_dump_frames = how many frames. Output: ngpu_dump_<serial>.txt beside
// the executable. The object identity key the NG2 frame-interpolation work
// uses is (fc vertex slots' word0, ib word0, vs, ps).
#include "native_gpu_dump.h"
#include "native_gpu_present.h"
#include "native_gpu_census.h"
#include "native_gpu_shader_census.h"

namespace ngpu { static void CensusShaderObject(uint32_t shader); }  // defined beside OnSetShader
namespace ngpu { extern uint64_t g_cso_calls, g_cso_null, g_cso_dup, g_cso_new, g_cso_null_vs, g_cso_null_ps; }

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#define XXH_INLINE_ALL
#include "xxhash.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <vector>

REXCVAR_DEFINE_INT32(ngpu_dump_at_frame, 0, "GPU", "Native-GPU draw dump: first frame to record (0 = off)");
REXCVAR_DEFINE_INT32(ngpu_dump_frames, 2, "GPU", "Native-GPU draw dump: number of frames to record");

namespace ngpu {
namespace {

constexpr uint32_t kFetchConstants = 0x480;   // 32 x 24 bytes
constexpr uint32_t kIndexBuffer = 0x3094;     // current index buffer object
constexpr uint32_t kPredication = 0x31A4;

std::atomic<uint32_t> g_frame{0};
uint32_t g_vs = 0, g_ps = 0;             // last SetShader per type (render thread)
uint32_t g_ct = 0, g_ct_base = 0, g_ct_n = 0;
uint32_t g_rt = 0, g_rt_index = 0;
FILE* g_file = nullptr;
std::mutex g_mutex;
uint32_t g_serial = 0;
uint32_t g_draws = 0;

const uint8_t* Base() {
  static const uint8_t* base = nullptr;
  if (!base) base = rex::system::kernel_state()->memory()->virtual_membase();
  return base;
}

// A guest read that cannot fault: the host page is checked with VirtualQuery
// once per 4 KB page (cached), so a stale pointer in the device or an object
// yields 0 instead of an access violation on the render thread (the first
// dump run died on a read of 0x4D46019F).
bool PageReadable(const uint8_t* host) {
  static std::set<uintptr_t> ok;
  static std::mutex m;
  const uintptr_t page = reinterpret_cast<uintptr_t>(host) & ~uintptr_t(0xFFF);
  {
    std::lock_guard<std::mutex> lock(m);
    if (ok.count(page)) return true;
  }
  MEMORY_BASIC_INFORMATION mbi{};
  if (!VirtualQuery(reinterpret_cast<const void*>(page), &mbi, sizeof(mbi))) return false;
  if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_NOACCESS) || (mbi.Protect & PAGE_GUARD)) return false;
  std::lock_guard<std::mutex> lock(m);
  ok.insert(page);
  return true;
}

inline uint32_t Load32(uint32_t guest) {
  if (guest < 0x1000 || guest >= 0xC0000000u) return 0;
  const uint8_t* p = Base() + guest;
  if (!PageReadable(p) || ((guest & 0xFFF) > 0xFFC && !PageReadable(p + 4))) return 0;
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// Copy n bytes from host memory to a file, page by page, skipping what is not mapped.
void WriteSafe(FILE* f, const uint8_t* host, uint32_t n) {
  while (n) {
    const uint32_t chunk = std::min<uint32_t>(n, 0x1000 - (reinterpret_cast<uintptr_t>(host) & 0xFFF));
    if (PageReadable(host)) std::fwrite(host, 1, chunk, f);
    host += chunk;
    n -= chunk;
  }
}

bool Active() {
  const int32_t at = REXCVAR_GET(ngpu_dump_at_frame);
  if (at <= 0) return false;
  const uint32_t f = g_frame.load(std::memory_order_relaxed);
  return f >= uint32_t(at) && f < uint32_t(at) + uint32_t(REXCVAR_GET(ngpu_dump_frames));
}

void Open() {
  if (g_file) return;
  char name[64];
  std::snprintf(name, sizeof(name), "ngpu_dump_%u.txt", ++g_serial);
  g_file = std::fopen(name, "w");
  if (g_file) REXLOG_INFO("[ngpu] draw dump -> {}", name);
}

void Close() {
  if (!g_file) return;
  std::fclose(g_file);
  g_file = nullptr;
  REXLOG_INFO("[ngpu] draw dump closed: {} draws", g_draws);
  g_draws = 0;
}

// The current vertex declaration object (device+0x2E2C): its D3DVERTEXELEMENT9
// array is what the native input layout is built from. Written once per
// distinct object as a VD line with its first 64 words.
void DumpDeclaration(uint32_t decl) {
  static std::set<uint32_t> seen;
  if (!decl || !g_file || !seen.insert(decl).second) return;
  std::string line = "VD decl=";
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%08X", decl);
  line += buf;
  for (uint32_t i = 0; i < 64; ++i) {
    std::snprintf(buf, sizeof(buf), " %08X", Load32(decl + i * 4));
    line += buf;
  }
  line += '\n';
  std::fputs(line.c_str(), g_file);
}

void WriteDraw(uint32_t dev, const char* kind, uint32_t prim, uint32_t a, uint32_t b, uint32_t c) {
  if (!g_file) return;
  const uint32_t ib = Load32(dev + kIndexBuffer);
  std::string line;
  char buf[200];
  // vc = FNV-1a of vertex float constants c0..c15 (device+0x780, 256 bytes): the
  // per-object transform block, if the engine writes it through the device
  // shadow; changes per object when it does, constant when LOAD packets carry it.
  uint64_t vc = 1469598103934665603ull;
  for (uint32_t i = 0; i < 64; ++i) {
    vc ^= Load32(dev + 0x780 + i * 4);
    vc *= 1099511628211ull;
  }
  // vs/ps = the shader objects the device holds at the draw (device+0x3198 /
  // +0x3194, written by the SetVertexShader / SetPixelShader setters
  // sub_82232510 / sub_82208BB0); the S lines show what sub_82221858 loaded.
  std::snprintf(buf, sizeof(buf), "D f%u %s prim=%u base=%u start=%u count=%u ib=%08X:%08X/%08X vs=%08X ps=%08X ct=%08X+%08X:%u rt=%08X:%u pred=%08X vc=%016llX vd=%08X",
                g_frame.load(std::memory_order_relaxed), kind, prim, a, b, c, ib, ib ? Load32(ib + 0x18) : 0,
                ib ? Load32(ib + 0x1C) : 0, g_vs, g_ps, g_ct, g_ct_base, g_ct_n,
                g_rt, g_rt_index, Load32(dev + kPredication), (unsigned long long)vc, Load32(dev + 0x2E2C));
  line = buf;
  DumpDeclaration(Load32(dev + 0x2E2C));
  for (uint32_t i = 0; i < 32; ++i) {
    const uint32_t s = dev + kFetchConstants + i * 24;
    const uint32_t w0 = Load32(s);
    if (!w0) continue;
    std::snprintf(buf, sizeof(buf), " fc%u=%08X/%08X/%08X/%08X/%08X/%08X", i, w0, Load32(s + 4), Load32(s + 8),
                  Load32(s + 12), Load32(s + 16), Load32(s + 20));
    line += buf;
  }
  line += '\n';
  std::fputs(line.c_str(), g_file);
  if ((++g_draws & 63) == 0) std::fflush(g_file);  // a crash mid-dump keeps most of it
}

}  // namespace

struct PendingDraw { uint32_t dev, prim, base_vertex, start, count; bool armed; };
thread_local PendingDraw g_pending = {};

void OnDrawIndexedDone(uint32_t) {
  if (!g_pending.armed) return;
  g_pending.armed = false;
  ShadowDrawIndexed(g_pending.dev, g_pending.prim, g_pending.base_vertex, g_pending.start, g_pending.count);
}

void OnDrawIndexed(uint32_t dev, uint32_t prim, uint32_t base_vertex, uint32_t start, uint32_t count) {
  NoteIssuedDraw(0, prim);
  fable2::ngpu::census::See(fable2::ngpu::census::Kind::PrimitiveType, prim & 0x3F);  // the D3D9 primitive the game asked for (same encoding as VGT_DRAW_INITIATOR); the hook argument, not the ring parse
  // The shader pair the DEVICE holds at this draw (+0x3198 VS, +0x3194 PS): the SetShader hooks miss
  // the setters some paths use (SetVertexShader/SetPixelShader, the UI), and in one leg the plugin
  // translated 238 shaders while the hook feed saw 127 - a floor with a hole. The device is the truth
  // at draw time; the seen-set makes this two loads and two lookups per draw.
  {
    // Nulls split BY FIELD: 51.3% of all reads null is the shape of one field always null
    // plus a tail, not of half the draws having no shader (claudecode-76's arithmetic).
    const uint32_t vso = Load32(dev + 0x3198), pso = Load32(dev + 0x3194);
    if (!vso) ++g_cso_null_vs; if (!pso) ++g_cso_null_ps;
    CensusShaderObject(vso);
    CensusShaderObject(pso);
  }
  // The native draw waits for the exit hook (the XDK writes this draw's
  // state and DRAW packets between here and there).
  if (DrawAtExit()) g_pending = {dev, prim, base_vertex, start, count, true};
  else ShadowDrawIndexed(dev, prim, base_vertex, start, count);
  if (!Active()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  Open();
  WriteDraw(dev, "DI", prim, base_vertex, start, count);
}

thread_local PendingDraw g_pending_dv = {};
void OnDrawVerticesDone(uint32_t) {
  if (!g_pending_dv.armed) return;
  g_pending_dv.armed = false;
  ShadowDrawVertices(g_pending_dv.dev, g_pending_dv.prim, g_pending_dv.start, g_pending_dv.count);
}

void OnDrawVertices(uint32_t dev, uint32_t prim, uint32_t start, uint32_t count) {
  NoteIssuedDraw(1, prim);
  fable2::ngpu::census::See(fable2::ngpu::census::Kind::PrimitiveType, prim & 0x3F);  // the D3D9 primitive the game asked for (same encoding as VGT_DRAW_INITIATOR); the hook argument, not the ring parse
  // The shader pair the DEVICE holds at this draw (+0x3198 VS, +0x3194 PS): the SetShader hooks miss
  // the setters some paths use (SetVertexShader/SetPixelShader, the UI), and in one leg the plugin
  // translated 238 shaders while the hook feed saw 127 - a floor with a hole. The device is the truth
  // at draw time; the seen-set makes this two loads and two lookups per draw.
  {
    // Nulls split BY FIELD: 51.3% of all reads null is the shape of one field always null
    // plus a tail, not of half the draws having no shader (claudecode-76's arithmetic).
    const uint32_t vso = Load32(dev + 0x3198), pso = Load32(dev + 0x3194);
    if (!vso) ++g_cso_null_vs; if (!pso) ++g_cso_null_ps;
    CensusShaderObject(vso);
    CensusShaderObject(pso);
  }
  if (DrawAtExit()) g_pending_dv = {dev, prim, 0, start, count, true};  // drawn at the exit hook
  else ShadowDrawVertices(dev, prim, start, count);
  if (!Active()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  Open();
  WriteDraw(dev, "DV", prim, 0, start, count);
}

// DrawIndexedVerticesUP(dev, prim, minIndex, numVertices, indexCount, r8, stride, vertexData, [indexData]):
// recorded at the entry, rendered at the exit hook (0x822182CC) from the ring's packet.
struct PendingUP { uint32_t dev, prim, num_vertices, stride, vdata, index_count, min_index, iptr_out; bool armed; };
thread_local PendingUP g_pending_up = {};
void OnDrawUPDone(uint32_t hr) {
  if (!g_pending_up.armed) return;
  g_pending_up.armed = false;
  const PendingUP& q = g_pending_up;
  // The begin returned the reserved ring addresses through the two out
  // pointers (r10 = vertices, [r1+0x54] = indices); the engine fills them next.
  // Run 122 read float bits as indices: the first out pointer (r10) receives the
  // index area and the stack one ([r1+0x54]) the vertex area (which comes first
  // in the ring: 4 x 20 bytes, then the 12 index bytes).
  if (hr == 0) DeferUP(q.dev, q.prim, q.min_index, q.num_vertices, q.index_count, q.stride, Load32(q.iptr_out), Load32(q.vdata));
}

// The XDK's DrawIndexedVerticesUP wrapper sub_8222E120 (dev, prim, minIndex,
// numVertices, indexCount, pIndexData, indexFormat, pVertexData, [stride at
// r1+0x54]): it reserves ring space (sub_82217DB8), copies both arrays into
// it and commits the write pointer; the native draw runs at its exit
// (0x8222E1B8) from the arrays themselves.
void DumpMicrocode(uint32_t shader, uint32_t entry);
struct PendingUP2 { uint32_t dev, prim, min_index, num_vertices, index_count, idx_ptr, idx_fmt, vdata, stride; bool armed; };
thread_local PendingUP2 g_pending_up2 = {};
void OnDrawUPBegin(uint32_t dev, uint32_t prim, uint32_t min_index, uint32_t num_vertices, uint32_t index_count, uint32_t idx_ptr, uint32_t idx_fmt,
                   uint32_t vdata, uint32_t r1) {
  const uint32_t stride = Load32(r1 + 0x54);
  if (DrawUpEnabled()) g_pending_up2 = {dev, prim, min_index, num_vertices, index_count, idx_ptr, idx_fmt, vdata, stride, true};
  // The shaders these draws use are set through a setter the hooks miss:
  // dump the device's current pair (once per object) while dumping is on.
  if (REXCVAR_GET(ngpu_dump_at_frame) > 0) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const uint32_t vs = Load32(dev + 0x3198), ps = Load32(dev + 0x3194);
    if (vs) DumpMicrocode(vs, 0);
    if (ps) DumpMicrocode(ps, 0);
  }
}
void OnDrawUPEnd(uint32_t hr) {
  if (!g_pending_up2.armed) return;
  g_pending_up2.armed = false;
  const PendingUP2& q = g_pending_up2;
  // r3 at the exit is memcpy's return in the success path (a guest pointer), E_OUTOFMEMORY when the ring reserve failed.
  if (hr != 0x8007000Eu) ShadowDrawUP2(q.dev, q.prim, q.min_index, q.num_vertices, q.index_count, q.idx_ptr, q.idx_fmt, q.vdata, q.stride);
}

void OnDrawUP(uint32_t dev, uint32_t prim, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8, uint32_t r9,
              uint32_t r10, uint32_t r1) {
  NoteIssuedDraw(2, prim);
  fable2::ngpu::census::See(fable2::ngpu::census::Kind::PrimitiveType, prim & 0x3F);  // the D3D9 primitive the game asked for (same encoding as VGT_DRAW_INITIATOR); the hook argument, not the ring parse
  // The shader pair the DEVICE holds at this draw (+0x3198 VS, +0x3194 PS): the SetShader hooks miss
  // the setters some paths use (SetVertexShader/SetPixelShader, the UI), and in one leg the plugin
  // translated 238 shaders while the hook feed saw 127 - a floor with a hole. The device is the truth
  // at draw time; the seen-set makes this two loads and two lookups per draw.
  {
    // Nulls split BY FIELD: 51.3% of all reads null is the shape of one field always null
    // plus a tail, not of half the draws having no shader (claudecode-76's arithmetic).
    const uint32_t vso = Load32(dev + 0x3198), pso = Load32(dev + 0x3194);
    if (!vso) ++g_cso_null_vs; if (!pso) ++g_cso_null_ps;
    CensusShaderObject(vso);
    CensusShaderObject(pso);
  }
  // The XDK begin: (dev, prim, minIndex, numVertices, indexCount, flags, stride, &pVertices, [r1+0x54] = &pIndices).
  if (DrawUpEnabled()) g_pending_up = {dev, prim, r6, r9, r10, r7, r5, Load32(r1 + 0x54), true};
  (void)r8;
  // The UI shaders are set through a setter the SetShaders hooks miss: dump
  // the device's current pair on sight while dumping is on (once per object).
  if (REXCVAR_GET(ngpu_dump_at_frame) > 0) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const uint32_t vs = Load32(dev + 0x3198), ps = Load32(dev + 0x3194);
    if (vs) DumpMicrocode(vs, 0);
    if (ps) DumpMicrocode(ps, 0);
  }
  if (!Active()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  Open();
  WriteDraw(dev, "UP", prim, r5, r6, r7);
  if (g_file) std::fprintf(g_file, "  up r8=%08X r9=%08X r10=%08X\n", r8, r9, r10);
}

// The XDK shader object, as read live (peek.py on 0x4D06EF50) and as
// sub_82221858 walks it:
//   +0x00  D3D common word: low nibble = resource type (6 vertex shader,
//          7 pixel shader; the index buffer object carries 2)
//   +0x20  physical base of the microcode (0xFDxxxxxx: physical address
//          space, host = physical_membase + (addr & 0x1FFFFFFF))
//   +0x380 table of 8-byte entries; sub_82221858's third argument is a
//          pointer to one entry, [entry] = offset (from the object) of the
//          VARIANT RECORD it loads:
//   rec+872 microcode offset from the physical base, rec+876 byte length,
//   rec+880/884/888 register counts, rec+892 the constant list offset the
//   loader (sub_82221B90) walks (it receives rec+872 as "table").
// While a dump is active every distinct microcode is written once to
// ngpu_shaders/<physical address>_<v|p>.bin (XenosRecomp input) with the
// first 4 KB of the object beside it (<obj>_<v|p>.obj).
const uint8_t* PhysBase() {
  static const uint8_t* base = nullptr;
  if (!base) base = rex::system::kernel_state()->memory()->physical_membase();
  return base;
}

// Read from a dumped object (4D05EBD0_v.obj): the XDK SHADER CONTAINER
// (XenosRecomp's ShaderContainer: flags 0x102A11xx, virtualSize,
// physicalSize, fieldC, constantTableOffset, definitionTableOffset,
// shaderOffset ...) starts at obj+872; its virtual part (virtualSize bytes)
// is right there, its physical part (physicalSize bytes: the microcode,
// with a 0x80-byte header) is at the physical address obj+0x20. The XDK
// compiled-shader file XenosRecomp parses is the two parts concatenated, so
// that is what ngpu_shaders/<obj>_<v|p>.xvu holds.
void DumpMicrocode(uint32_t shader, uint32_t entry) {
  static std::set<uint32_t> seen;
  if (!shader) return;
  const uint32_t type = Load32(shader) & 0xF;
  const char tag = type == 7 ? 'p' : 'v';
  // Vertex shader objects keep the container at +872 and the physical part
  // at +0x20; pixel shader objects (type 7) at +0x28 and +0x18 (read from
  // the dumped .obj files: magic 0x102A1100 at 0x28).
  const uint32_t c = shader + (type == 7 ? 0x28 : 872);
  const uint32_t magic = Load32(c), vsize = Load32(c + 4), psize = Load32(c + 8);
  const uint32_t phys = Load32(shader + (type == 7 ? 0x18 : 0x20));
  if (g_file) {
    std::fprintf(g_file, "M shader=%08X type=%u entry=%08X magic=%08X vsize=%u psize=%u phys=%08X ctab=+%u dtab=+%u shdr=+%u\n",
                 shader, type, entry, magic, vsize, psize, phys, Load32(c + 16), Load32(c + 20), Load32(c + 24));
    if (entry) {
      // `entry` is the pixel shader object the flush loads alongside the
      // vertex shader (run 20's raw records: type nibble 7): container at
      // +0x28, block at +0x18, Shader header at container + shaderOffset.
      const uint32_t pc = entry + 0x28, phdr = pc + Load32(pc + 24);
      std::fprintf(g_file, "V shader=%08X ps=%08X block=%08X code=+%u len=%u\n", shader, entry, Load32(entry + 0x18), Load32(phdr),
                   Load32(phdr + 4));
    }
  }
  // The pixel shader SetShaders passes alongside the vertex shader is a
  // container of its own: dump it too (the engine's material pairs).
  if (entry && (Load32(entry) & 0xF) == 7 && !seen.count(entry)) DumpMicrocode(entry, 0);
  if (!seen.insert(shader).second) return;
  if (seen.size() == 1) std::filesystem::create_directories("ngpu_shaders");
  char name[64];
  std::snprintf(name, sizeof(name), "ngpu_shaders/%08X_%c.obj", shader, tag);
  if (FILE* f = std::fopen(name, "wb")) {
    WriteSafe(f, Base() + shader, 4096);
    std::fclose(f);
  }
  if ((magic >> 16) != 0x102A || !vsize || vsize > (1u << 20) || psize > (1u << 20) || phys < 0xC0000000u) return;
  std::snprintf(name, sizeof(name), "ngpu_shaders/%08X_%c.xvu", shader, tag);
  if (FILE* f = std::fopen(name, "wb")) {
    WriteSafe(f, Base() + c, vsize);
    // The block address is a CPU-form 0xE0..-range virtual address: the host
    // keeps that mirror one page above its raw physical offset
    // (rex::memory::detail::PhysicalHostOffset), and the game adds 0x1000
    // when it forms the GPU address. Runs 9-20 read the raw offset and
    // dumped whatever sat one page below (floats, other shaders).
    WriteSafe(f, Base() + phys + (phys >= 0xE0000000u ? 0x1000u : 0u), psize);
    std::fclose(f);
  }
}

uint32_t g_vs_entry = 0;
uint32_t CurrentVertexShader() { return g_vs; }
uint32_t CurrentVertexShaderEntry() { return g_vs_entry; }
uint32_t CurrentRenderTarget() { return g_rt_index == 0 ? g_rt : 0; }
uint32_t CurrentPixelShader() { return g_ps; }

// Shader-ISA census fed from the XDK shader OBJECT the SetShader hook receives:
// a D3D9-level source. The ring parse was the first feed and it hallucinated
// IM_LOADs out of the user-pointer draws' inline vertex data (a 6068-dword
// 'shader' of vertex bytes passed every structural check and killed the SDK
// analyser). The object carries its own plausibility: the 0x102A container
// magic, sane sizes, a physical block address, and the microcode at a fixed
// 0x80 past that block (DumpMicrocode's layout). Whether that offset and
// length are EXACTLY the range the GPU loads is verified downstream: a hash
// that matches one of the plugin's own ucode dumps confirms it byte for byte.
// DID-IT-RUN, value-independently: how many objects arrived, how many were null, how many were
// duplicates, how many new - so a unique count that does not move can be told apart from a
// feed that contributes nothing (a unique count is a change detector and cannot tell those apart).
uint64_t g_cso_calls = 0, g_cso_null = 0, g_cso_dup = 0, g_cso_new = 0, g_cso_null_vs = 0, g_cso_null_ps = 0;
static void CensusShaderObject(uint32_t shader) {
  ++g_cso_calls;
  if (!shader) { ++g_cso_null; return; }
  static std::set<uint32_t> seen;   // once per object address (a floor: an object re-created at the same address is missed)
  if (!seen.insert(shader).second) { ++g_cso_dup; return; }
  ++g_cso_new;
  const uint32_t type = Load32(shader) & 0xF;
  const uint32_t c = shader + (type == 7 ? 0x28 : 872);
  const uint32_t magic = Load32(c), vsize = Load32(c + 4), psize = Load32(c + 8);
  const uint32_t phys = Load32(shader + (type == 7 ? 0x18 : 0x20));
  // REJECTIONS ARE COUNTED AND THE FIRST FEW NAMED: an object this extractor
  // cannot read is a hole in the SEEN axis that would otherwise be silent
  // (leg F: plugin 238 vs feed 127 - is the residual 'never drawn with' or
  // objects of another layout rejected here?). The count answers that.
  static uint32_t rejected = 0;
  if ((magic >> 16) != 0x102A || !vsize || vsize > (1u << 20) || psize < 4 || psize > (1u << 20) || phys < 0xC0000000u) {
    ++rejected;
    if (rejected <= 8 || rejected == 64 || rejected == 512)
      REXLOG_INFO("[ngpu-census] shader object {:08X} REJECTED by the extractor (#{}): common {:08X} type {} container {:08X} magic {:08X} vsize {} psize {} phys {:08X}",
                  shader, rejected, Load32(shader), type, c, magic, vsize, psize, phys);
    return;
  }
  // The microcode's offset and length come from the container's own Shader
  // header (container + shaderOffset at +24: [0] code offset within the
  // physical block, [4] byte length) - the same fields DumpMicrocode's 'V'
  // line reads. The first version assumed a fixed 0x80 header: 11 of 32
  // objects then hashed to a plugin dump, 21 did not, 70 were refused as
  // garbage - the offset is per object, not fixed.
  const uint32_t shdr = c + Load32(c + 24);
  const uint32_t code_off = Load32(shdr), code_len = Load32(shdr + 4);
  static uint32_t logged = 0;
  if (logged < 12) { ++logged; REXLOG_INFO("[ngpu-census] shader object {:08X} type {} container {:08X} magic {:08X} vsize {} psize {} phys {:08X} shdr +{} code +{} len {}", shader, type, c, magic, vsize, psize, phys, Load32(c + 24), code_off, code_len); }
  if (!code_len || (code_len & 3) || uint64_t(code_off) + code_len > psize) return;
  const uint8_t* block = Base() + phys + (phys >= 0xE0000000u ? 0x1000u : 0u);
  const uint8_t* mc = block + code_off;
  // TWO HASHES, ONE OBJECT: the JIT (XenosRecomp path) names its cache entries by
  // XXH3 over the whole physical block (psize bytes, header included); the census
  // and the plugin hash the microcode range only. They coincide only when the
  // code starts at 0 and fills the block. The pair is appended to
  // ngpu_shader_census/hashmap.txt so the offline differential can pair a
  // XenosRecomp HLSL with the SDK bindings of the same shader.
  {
    static std::mutex hm;
    std::lock_guard<std::mutex> lk(hm);
    std::error_code ec;
    std::filesystem::create_directories("ngpu_shader_census", ec);
    // The whole compiled-shader container (vsize bytes of container + psize bytes
    // of physical block) - what XenosRecomp takes as input and what the native
    // cache is keyed by (its XXH3) - written as <ucode>_<v|p>.xvu so XenosRecomp
    // can be run OFFLINE on exactly the shaders the game used and its HLSL
    // paired with the SDK's bindings by the ucode hash, no address mapping.
    const uint64_t ucode_hash = XXH3_64bits(mc, code_len);
    std::vector<uint8_t> xvu;
    if (vsize + psize <= (2u << 20)) {
      xvu.resize(vsize + psize);
      const uint8_t* cont = Base() + c;
      bool ok = true;
      for (uint32_t off = 0; off < vsize; off += 0x1000) if (!PageReadable(cont + off)) ok = false;
      for (uint32_t off = 0; off < psize; off += 0x1000) if (!PageReadable(block + off)) ok = false;
      if (ok && PageReadable(cont + vsize - 1) && PageReadable(block + psize - 1)) {
        std::memcpy(xvu.data(), cont, vsize);
        std::memcpy(xvu.data() + vsize, block, psize);
        char name[96];
        std::snprintf(name, sizeof(name), "ngpu_shader_census/%016llX_%c.xvu", (unsigned long long)ucode_hash, type == 7 ? 'p' : 'v');
        if (FILE* f = std::fopen(name, "wb")) { std::fwrite(xvu.data(), 1, xvu.size(), f); std::fclose(f); }
      } else {
        xvu.clear();
      }
    }
    if (FILE* f = std::fopen("ngpu_shader_census/hashmap.txt", "ab")) {
      std::fprintf(f, "%c jit %016llX ucode %016llX xvu %016llX obj %08X block %08X psize %u code_off %u len %u vsize %u\n", type == 7 ? 'p' : 'v',
                   (unsigned long long)XXH3_64bits(block, psize), (unsigned long long)ucode_hash,
                   (unsigned long long)(xvu.empty() ? 0ull : XXH3_64bits(xvu.data(), xvu.size())), shader, phys, psize, code_off, code_len, vsize);
      std::fclose(f);
    }
  }
  fable2::ngpu::shader_census::Note(type == 7, reinterpret_cast<const uint32_t*>(mc), code_len / 4, phys + code_off, /*from_object=*/true);
}

void OnSetShader(uint32_t dev, uint32_t shader, uint32_t entry) {
  (void)dev;
  CensusShaderObject(shader);
  if (entry) CensusShaderObject(entry);
  // SetPixelShader passes the pixel shader as `shader`; SetShaders(dev, VS,
  // PS) passes the vertex shader and, as `entry`, the pixel shader (type
  // nibble 7) - the pair the engine's own draws use.
  if (shader && (Load32(shader) & 0xF) == 7) g_ps = shader;
  else {
    g_vs = shader;
    if (entry) g_vs_entry = entry;
    if (entry && (Load32(entry) & 0xF) == 7) g_ps = entry;
  }
  // The shader files are written on first sight whether or not a dump window
  // is open (the seen-sets throttle it to once per shader / variant); the S
  // line only inside the window.
  if (!Active()) {
    if (REXCVAR_GET(ngpu_dump_at_frame) > 0) {
      std::lock_guard<std::mutex> lock(g_mutex);
      DumpMicrocode(shader, entry);
    }
    return;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  Open();
  if (g_file) std::fprintf(g_file, "S f%u shader=%08X entry=%08X common=%08X\n", g_frame.load(), shader, entry,
                           shader ? Load32(shader) : 0);
  DumpMicrocode(shader, entry);
}

void OnLoadConstants(uint32_t dev, uint32_t table, uint32_t base, uint32_t base2, uint32_t n) {
  (void)dev; (void)base2;
  g_ct = table; g_ct_base = base; g_ct_n = n;
  if (!Active()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  Open();
  if (g_file) {
    // the table: +20 = offset of the (count, start) pair list, list +16 = its byte length
    const uint32_t list = table ? table + Load32(table + 20) + 20 : 0;
    std::fprintf(g_file, "C f%u table=%08X base=%08X base2=%08X n=%u list=%08X len=%u\n", g_frame.load(), table,
                 base, base2, n, list, list ? Load32(list - 4) : 0);
  }
}

// The first 16 words of each distinct render-target surface and resolve
// destination, once, for the M6 (render target) model.
void LogObjectOnce(const char* what, uint32_t obj, std::set<uint32_t>& seen) {
  if (!obj || !seen.insert(obj).second || seen.size() > 64) return;
  std::string w;
  for (uint32_t i = 0; i < 16; ++i) { char b[16]; std::snprintf(b, sizeof(b), " %08X", Load32(obj + i * 4)); w += b; }
  REXLOG_INFO("[ngpu] {} {:08X}:{}", what, obj, w);
  // Surfaces keep their GPU-side descriptor in the 0xE0.. mirror (word 7):
  // read it through the host page offset.
  const uint32_t desc = Load32(obj + 28);
  if (desc >= 0xE0000000u) {
    const uint8_t* h = Base() + desc + 0x1000;
    if (PageReadable(h) && PageReadable(h + 63)) {
      std::string d;
      for (uint32_t i = 0; i < 16; ++i) {
        const uint8_t* p = h + i * 4;
        char b[16];
        std::snprintf(b, sizeof(b), " %08X", (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]);
        d += b;
      }
      REXLOG_INFO("[ngpu]   descriptor at {:08X}:{}", desc, d);
    }
  }
}

void OnResolve(uint32_t dev, uint32_t flags, uint32_t src_rect, uint32_t dest, uint32_t dest_point, uint32_t level, uint32_t slice, uint32_t clear) {
  // Fable's engine wrapper (12 per frame): r5 is an engine object, not the
  // XDK argument list. Counted only; the XDK call below carries the facts.
  (void)dev; (void)flags; (void)src_rect; (void)dest; (void)dest_point; (void)level; (void)slice; (void)clear;
}

// The XDK's D3DDevice_Resolve (sub_82206888): flags bits 0..2 = the render
// target index, 0x10 = the depth buffer; the dest texture's fetch constant
// sits at +0x1C (dwords 0..5). The current surfaces live at dev+0x3098+4*i
// (colour) and dev+0x30A8 (depth).
void OnResolveXdk(uint32_t dev, uint32_t flags, uint32_t src_rect, uint32_t dest, uint32_t dest_point, uint32_t level, uint32_t slice, uint32_t clear) {
  static uint32_t calls = 0;
  static std::set<uint32_t> seen_dest, seen_surf;
  uint32_t fc[6] = {};
  for (uint32_t i = 0; i < 6; ++i) fc[i] = dest ? Load32(dest + 0x1C + i * 4) : 0;
  if (dest) NoteResolveDest((fc[1] >> 12) << 12, (fc[2] & 0x1FFF) + 1, ((fc[2] >> 13) & 0x1FFF) + 1, fc[1] & 0x3F, flags, Load32(dev + 0x2880), Load32(dev + 0x2884));
  if (calls++ >= 60) return;
  const uint32_t rt_index = flags & 7;
  const uint32_t surface = Load32(dev + 0x3098 + 4 * rt_index), depth = Load32(dev + 0x30A8);
  REXLOG_INFO("[ngpu] xdk resolve flags {:08X} src ({} {} {} {}) dest {:08X} = {}x{} format {} {} base {:08X} pitch {} destPoint ({} {}) level {} slice {} clear {:08X} rt{} {:08X} depth {:08X}",
              flags, src_rect ? int32_t(Load32(src_rect)) : -1, src_rect ? int32_t(Load32(src_rect + 4)) : -1, src_rect ? int32_t(Load32(src_rect + 8)) : -1,
              src_rect ? int32_t(Load32(src_rect + 12)) : -1, dest, (fc[2] & 0x1FFF) + 1, ((fc[2] >> 13) & 0x1FFF) + 1, fc[1] & 0x3F, (fc[0] >> 31) ? "tiled" : "linear",
              (fc[1] >> 12) << 12, ((fc[0] >> 22) & 0x1FF) * 32, dest_point ? int32_t(Load32(dest_point)) : -1, dest_point ? int32_t(Load32(dest_point + 4)) : -1, level, slice,
              clear, rt_index, surface, depth);
  LogObjectOnce("resolve dest texture", dest, seen_dest);
  LogObjectOnce("resolve source surface", (flags & 0x10) ? depth : surface, seen_surf);
}

void OnSetRenderTarget(uint32_t dev, uint32_t surface, uint32_t index) {
  (void)dev;
  static std::set<uint32_t> seen;
  LogObjectOnce("render target surface", surface, seen);
  g_rt = surface; g_rt_index = index;
  if (!Active()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  Open();
  if (g_file) std::fprintf(g_file, "R f%u surface=%08X index=%u w0=%08X\n", g_frame.load(), surface, index,
                           surface ? Load32(surface) : 0);
}

void OnPresent(uint32_t dev) {
  {
    static uint32_t frames = 0;
    if ((++frames % 300) == 0)
      REXLOG_INFO("[ngpu-census] shader-object feed did-it-run: {} object arrivals ({} null: VS field null {} / PS field null {}; {} duplicates, {} new objects) over {} presents - a feed with 0 arrivals contributes nothing whatever the unique count says",
                  g_cso_calls, g_cso_null, g_cso_null_vs, g_cso_null_ps, g_cso_dup, g_cso_new, frames);
  }
  (void)dev;
  ShadowPresent();
  const uint32_t f = g_frame.fetch_add(1, std::memory_order_relaxed) + 1;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_file) {
    std::fprintf(g_file, "F %u\n", f);
    if (!Active()) Close();
  }
}

}  // namespace ngpu
