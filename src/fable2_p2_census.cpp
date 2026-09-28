// P2 ATTRIBUTION CENSUS, guest side. See fable2_p2_census.h.
#include "fable2_p2_census.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <condition_variable>
#include <deque>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "native_gpu_frontend.h"   // [p3 draw] FrontEndDraw / FrontEndSwap
#include "fable2_native_gs.h"     // [gs] the game's own graphics system

namespace fable2::p2 {
namespace {

struct Rec {
  uint32_t frame;
  uint32_t tid;
  uint16_t hook;
  uint16_t flags;   // bit 0: exit matched a different hook than the stack top (nesting mismatch)
  uint32_t cur_in, cur_out;     // device+cursor at entry / exit (guest virtual addresses as the library holds them)
  uint32_t cur2_in, cur2_out;   // device+cursor2 (the +0x3484 copy)
};
static_assert(sizeof(Rec) == 28, "record layout is the file format");

struct Open {
  uint16_t hook;
  uint32_t cur_in, cur2_in;
  uint32_t args[8];   // r3..r10 at entry (source census)
  bool wrote;         // [p3 src] this call or a call it contains moved the write cursor (it wrote packets)
};

bool g_on = false;
bool g_discover = false;
uint32_t g_start = 0, g_count = 0;
uint32_t g_cursor_off = 0x30, g_cursor2_off = 0x3484;
std::atomic<uint32_t> g_frame{0};
std::atomic<uint32_t> g_device{0};
std::mutex g_mu;
std::vector<Rec> g_recs;
bool g_written = false;
// LAST WRITER per 64-byte block of physical memory, kept from the first frame on (not only the packet window):
// the game records small command buffers once and replays them every frame through INDIRECT_BUFFER (Chapter 1:
// 1128 of 1759 draws per frame from a 184-byte buffer recorded before any 60-frame window), so a packet's
// producer is the last hooked call that wrote its address, whenever that was.
struct Writer { uint32_t frame; uint16_t hook; uint16_t pad; };
std::unordered_map<uint32_t, Writer> g_blocks;   // key = physical address >> 6
uint32_t g_discover_hi_logged = 0;
std::atomic<uint32_t> g_discover_logged{0};
std::atomic<uint64_t> g_enters{0}, g_exits{0}, g_mismatch{0}, g_overflow{0}, g_noread{0};
// Per-hook balance (Fable 2026-09-27: 188 M stack overflows - some hook enters far more often than it exits, e.g. a
// first-instruction hook that is also a loop target). Logged at Write(): the most unbalanced hooks.
std::atomic<uint64_t> g_hook_in[512], g_hook_out[512];
// [p3 map] register-map discovery state (see the header).
bool g_p3 = false;
uint32_t g_p3_frame = 0, g_p3_max = 0, g_p3_recorded = 0, g_p3_bridge = 0;
FILE* g_p3_guest = nullptr;
FILE* g_p3_bridge_f = nullptr;
std::mutex g_p3_mu;
std::vector<std::pair<uint32_t, uint32_t>> g_p3_ranges;   // physical [lo, hi) of the recorded calls
constexpr uint32_t kDevBytes = 0x5000;

// [p3 fe] FULL-NATIVE P3 STAGE 1, VALIDATION (FABLE2_P3FE=<sample every N draws>, needs FABLE2_P2 on): a front end on
// the GAME thread. At the exit of every hooked call, the packets that call wrote are decoded into a native register
// file (type 0 / type 1 writes, SET_CONSTANT, LOAD_ALU_CONSTANT). At every Nth DRAW packet the file is snapshotted,
// keyed by the packet's physical address; when the bridge executes that packet (BridgeDraw), the plugin's register
// file is compared register by register. Mismatch counts per register are logged - the gate for letting the front
// end draw is 0 outside the known per-tile registers.
bool g_fe = false;
uint32_t g_fe_every = 64;
uint32_t g_fe_residue = 0;   // FABLE2_P3FE_RES: which residue of (addr>>2) % every is sampled (aliasing control)
uint32_t g_fe_regs[0x5000];
uint32_t g_fe_src[0x5000];
// [p3 src] SOURCE CENSUS (FABLE2_P3SRC=<guest frame>, ported from NG2 4d52ba8/e44defa): every hooked call's r3-r10 plus
// 256 bytes behind each pointer-like argument (p3_src.bin), and every draw the kick-mode front end decodes in that
// frame with its register file and the INDIRECT_BUFFER packet that led to it (p3_fe.bin) - joined offline
// (tools/native_gpu/p3_sources.py) with the device dumps of FABLE2_P3MAP (p3_guest.bin, same frame) to find each
// per-draw register's source: stage 2 reads those sources at the draw call instead of decoding packets.
bool g_src = false;
uint32_t g_src_frame = 0, g_src_calls = 0, g_src_draws = 0;
FILE* g_src_f = nullptr;
FILE* g_src_fe_f = nullptr;
FILE* g_src_fesrc_f = nullptr;
FILE* g_src_wpkt_f = nullptr;
// TEMPORAL JOIN (SRC11, 17:55: at the writing call's exit the bytes at a draw's c0 packet address were floats, not the
// SET_CONSTANT the kick decoded - command-buffer memory is REUSED within a frame, so an address-only join can pick a
// different use of the same bytes). Parallel files, one uint32 per record: the kick count at the call's exit
// (p3_src_kick.bin, p3_guest_kick.bin) and at the draw's decode (p3_fe_kick.bin); joins keep calls that exited
// between the draw's previous kicks and its own.
FILE* g_src_kick_f = nullptr;
FILE* g_fe_kick_f = nullptr;
FILE* g_guest_kick_f = nullptr;   // p3_fe_wpkt.bin: per draw, the last-writer packet address of 0x2000-0x23FF + 0x4000-0x4927   // p3_fe_src.bin: per draw, the LOAD_ALU_CONSTANT source address of each 0x4000-0x4927 register (0 = packet)
std::mutex g_src_mu;
thread_local uint32_t t_fe_issuer = 0;   // the INDIRECT_BUFFER packet's address the current decode was entered through
std::atomic<uint32_t>* g_src_frame_counter = nullptr;   // the census's guest frame count (set in Init)
// SRC2/SRC3 (16:33-16:37): sampling every hooked call on EVERY thread, with two VirtualQuery calls per pointer argument
// under the census lock, starved the render thread - the census frame never ended and the guest froze (cdb: thread 70
// in VirtualQuery holding g_src_mu, the game and 3D-engine threads waiting on it). Now: only calls on the thread that
// kicks the ring (the draw issuer) are sampled, and page readability is cached per 4 KB host page.
std::atomic<uint32_t> g_kick_tid{0};
std::unordered_map<uintptr_t, bool> g_src_page_ok;   // under g_src_mu
// [p3 draw] FABLE2_P3DRAW=1 (stage 1b, NG2's fd2c392 ported): the front end DRAWS - at each DRAW packet it decodes,
// the bridge's FrontEndDraw with this register file (only the registers written since the last draw, by the dirty
// bitmap) and the shaders tracked from IM_LOAD / IM_LOAD_IMMEDIATE; at XE_SWAP the bridge's FrontEndSwap. All on the
// guest thread that kicks the ring; the plugin's callbacks are compare-only then.
bool g_p3draw = false;
uint32_t g_fe_vs = 0, g_fe_vs_dwords = 0, g_fe_ps = 0, g_fe_ps_dwords = 0;
bool g_fe_vs_inline = false, g_fe_ps_inline = false;
std::vector<uint8_t> g_fe_imm_vs, g_fe_imm_ps;
uint64_t g_fe_dirty[(0x5000 + 63) / 64];
std::atomic<uint64_t> g_fe_draws_issued{0}, g_fe_swaps_issued{0};
std::mutex g_fe_kick_mu;
// [p3 rec] FABLE2_P3DRAW=2 - DESIGN (b) (2026-09-27, FD A/B: front-end drawing on the guest thread cannot hold 60 at
// Fairfax, and it reads the shared per-frame constants block at 1FAAF000 an animation step early). At each kick the
// GUEST thread only FLATTENS the new ring words into a self-contained packet stream - indirect buffers inlined (the game
// recycles them within the frame, FE_F21), each draw packet preceded by a marker carrying its guest address - and a
// RECORDING thread runs the decoder on it: LOAD_ALU_CONSTANT memory and shader microcode are read at record time (the
// plugin's, and the GPU's, read point), and the draws / swaps go through the same FrontEndDraw / FrontEndSwap. The
// plugin's draw callback waits until the recorder has recorded that far, so the plugin's fence writes still follow
// the recording (the invariant the plugin-fed path had).
bool g_p3rec = false;
std::mutex g_rec_mu;
std::condition_variable g_rec_cv;
struct RecBatch { std::vector<uint8_t> bytes; uint32_t rptr_end; };
std::deque<RecBatch> g_rec_queue;
std::atomic<uint64_t> g_rec_draws_done{0}, g_rec_batches{0}, g_rec_bytes{0}, g_rec_waits{0}, g_rec_wait_timeouts{0};
std::atomic<uint64_t> g_rec_wait_us{0}, g_br_draws_seen{0};
// EXACT ORDERING (FR_C10/FR_C11: comparing global draw counts let the plugin run up to 15 draws ahead - the front end
// counts a few draws the plugin never calls back). The recorder publishes each recorded draw's identity (guest address,
// initiator) in order; the plugin walks the same sequence with its own cursor, waiting for its draw, skipping the
// front end's extras (an identity not followed by the plugin within a window).
constexpr uint32_t kRecKeys = 1u << 16;
uint64_t g_rec_keys[kRecKeys];
std::atomic<uint64_t> g_rec_head{0};
std::atomic<uint64_t> g_rec_skipped{0}, g_rec_plugin_extra{0};
constexpr uint32_t kFeAddrMarker = 0xC0007F00u;   // type-3, op 0x7F (unused by the CP), one payload dword: the address
uint32_t g_fe_next_addr = 0;                      // the recorder's decoder: the next draw packet's guest address
bool g_fe_next_addr_valid = false;   // kicks can come from more than one guest thread: one decoder (and one recorder) at a time
// [p5 exec] FABLE2_P5EXEC=1 (with FABLE2_P3DRAW=2; NG2 alt 6fce132 ported to design (b)): the plugin stops parsing
// the ring; the RECORDING thread, as it decodes each flattened kick, collects the side effects the command processor
// would have performed - fences, interrupts, memory waits, register writes outside the draw ranges, occlusion / extent
// events, the swap - and pushes them after that batch's draws are recorded, ending with the read pointer the game polls
// to reclaim ring space. So every fence the game sees follows the recording of the draws before it.
bool g_p5exec = false;
thread_local std::vector<uint32_t> t_px;
using PushFn = void (*)(const uint32_t*, uint32_t);
PushFn g_push = nullptr;
std::atomic<uint64_t> g_px_recs{0}, g_px_regs{0}, g_px_waits{0}, g_px_batches{0};
inline void Px(uint32_t k, uint32_t a, uint32_t b = 0, uint32_t c = 0) {
  t_px.push_back(k); t_px.push_back(a); t_px.push_back(b); t_px.push_back(c);
  g_px_recs.fetch_add(1, std::memory_order_relaxed);
}
// [p3 src] the guest physical address of the packet that last wrote each register (the census joins each per-draw
// register to the call that wrote THAT packet - a SetShaderConstant-style call before the draw, not the draw call).
uint32_t g_fe_wpkt[0x5000];
thread_local uint32_t t_fe_pkt = 0;
inline void FeSet(uint32_t reg, uint32_t v) {
  if (g_p5exec && reg >= 0x1921 && reg <= 0x1927)   // [gs] the gamma port: the backend keeps the ramp itself
    ::fable2::ngpu::FrontEndRegisterNow(reg, v);
  if (g_p5exec && !((reg >= 0x2000 && reg < 0x2400) || (reg >= 0x4000 && reg < 0x4928))) {
    Px(7, reg, v);   // [p5 exec] outside the draw ranges: the plugin's register file keeps it (scratch, gamma port, ...)
    g_px_regs.fetch_add(1, std::memory_order_relaxed);
  }
  if (g_src) g_fe_wpkt[reg] = t_fe_pkt;
  g_fe_regs[reg] = v;
  g_fe_src[reg] = 0;
  g_fe_dirty[reg >> 6] |= uint64_t(1) << (reg & 63);
}   // for LOAD_ALU_CONSTANT-loaded registers: the guest physical address it came from (0 = packet)
struct FeSnap {
  uint32_t r2[0x400]; uint32_t r4[0x928]; uint32_t src4[0x928]; uint32_t init; uint32_t frame;
  // READ-POINT TEST (stage 1b design): the memory-loaded constants re-read at the NEXT kick and at the frame's XE_SWAP,
  // to learn at which point the source memory holds what the GPU path (the plugin) used. Guarded by g_fe_mu.
  uint32_t k1[0x928]; uint32_t sw[0x928]; bool has_k1 = false, has_sw = false; uint32_t kick = 0;
  int refs = 2;   // the pairing queue + the front end's frame list; the last release deletes (under g_fe_mu)
};
std::atomic<uint64_t> g_fe_kicks{0};
std::vector<FeSnap*> g_fe_frame_snaps;
// DEFERRED FRONT END (FABLE2_P3FE_DEFER=1, stage 1b's decode point): kicks only buffer the ring words; the buffered
// frame is decoded when a kick carries its XE_SWAP (FE_F19: Fable fills LOAD_ALU_CONSTANT sources late in the frame).
bool g_fe_defer = false;
std::atomic<uint32_t> g_fe_marker{0};   // guest swap calls (FrameMarker); a change triggers the deferred decode
uint32_t g_fe_marker_done = 0;
std::vector<uint8_t> g_fe_pending;   // ring words not yet decoded
uint32_t g_fe_pending_base = 0;      // guest physical address of g_fe_pending[0]
std::unordered_map<uint64_t, std::deque<FeSnap*>> g_br_snaps;   // bridge values of sampled draws it ran first
uint32_t g_bridge_frames = 0;        // bridge swaps (GPU thread; read under g_fe_mu)
uint64_t g_br_expired = 0, g_fe_second = 0;
bool FeSampled(uint32_t addr) {
  if (!g_fe_every) return false;   // FABLE2_P3FE=0: the front end draws, nothing is compared
  // Hash of the address, not its dword position: the plain (addr>>2) % N residue aliased with the stream layout
  // (FE_F17: residue 0 and residue 7 saw different packet populations).
  uint32_t h = addr * 0x9E3779B1u;
  h ^= h >> 15; h *= 0x85EBCA77u; h ^= h >> 13;
  return (h % g_fe_every) == g_fe_residue;
}   // this front-end frame's snapshots (guest thread; under g_fe_mu)
uint64_t g_rp_regs = 0, g_rp_dec = 0, g_rp_k1 = 0, g_rp_k1_absent = 0, g_rp_sw = 0, g_rp_sw_absent = 0;
// POOLED (FR_P1 profile, 15:53: the recorder spent 81% of its time in operator new - a ~39 KB snapshot per sampled
// draw goes to the heap's locked large-block path, and with the plugin waiting on every draw that latency serialised
// the frame: 2 fps). Released snapshots go back to a free list and are reused.
std::mutex g_fe_mu;               // snapshots, queues, pool (moved up for FeAlloc)
std::vector<FeSnap*> g_fe_pool;   // under g_fe_mu
void FeRelease(FeSnap* p) { if (--p->refs == 0) { if (g_fe_pool.size() < 4096) g_fe_pool.push_back(p); else delete p; } }   // caller holds g_fe_mu
FeSnap* FeAlloc() {
  FeSnap* p = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_fe_mu);
    if (!g_fe_pool.empty()) { p = g_fe_pool.back(); g_fe_pool.pop_back(); }
  }
  if (!p) p = new FeSnap;
  p->has_k1 = p->has_sw = false; p->kick = 0; p->refs = 2;
  return p;
}
// [p6] BULK register writes (GSP profile, 20:35: FeDecode was 23% of the saturated recorder, ~300,000 FeSet calls a
// frame): a run [base, base+n) that lies wholly inside one draw range (0x2000-0x23FF or 0x4000-0x4927) has none of
// FeSet's side cases (no side-effect forwarding, no gamma port), so it is a byte-swapped copy, the source map cleared
// only when a comparison runs, and the dirty bits set word by word.
inline bool FeInDrawRange(uint32_t base, uint32_t n) {
  return (base >= 0x2000 && base + n <= 0x2400) || (base >= 0x4000 && base + n <= 0x4928);
}
inline void FeSetRun(uint32_t base, const uint8_t* be_words, uint32_t n) {
  for (uint32_t k = 0; k < n; ++k) {
    uint32_t v;
    std::memcpy(&v, be_words + size_t(k) * 4, 4);
    g_fe_regs[base + k] = _byteswap_ulong(v);
  }
  if (g_fe_every || g_src) {
    std::memset(g_fe_src + base, 0, size_t(n) * 4);
    if (g_src) for (uint32_t k = 0; k < n; ++k) g_fe_wpkt[base + k] = t_fe_pkt;
  }
  for (uint32_t r = base, end = base + n; r < end;) {
    const uint32_t word = r >> 6, lo = r & 63, hi = std::min<uint32_t>(64, lo + (end - r));
    const uint64_t mask = (hi == 64 ? ~0ull : ((1ull << hi) - 1)) & ~((1ull << lo) - 1);
    g_fe_dirty[word] |= mask;
    r += hi - lo;
  }
}
void FeCompare(FeSnap* s, const uint32_t* b2, const uint32_t* b4, uint32_t packet_addr, uint32_t draw_initiator);
// p3_bridge.bin (FABLE2_P3MAP): at most this many bridge draws - the cap was 4 x the call cap, and with a call cap of
// 200,000 one leg wrote 10.8 GB (SRC4): command buffers reuse their addresses, so later frames keep landing in the ranges.
constexpr uint32_t kP3BridgeMax = 24000;
uint32_t FeLoadBE(uint32_t phys) {
  auto* ks = rex::system::kernel_state();
  const uint8_t* p = ks && ks->memory() ? ks->memory()->TranslatePhysical<const uint8_t*>(phys) : nullptr;
  return p ? (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]) : 0u;
}
void FeReread(FeSnap* p, uint32_t* dst) {   // the memory-loaded constants as the source holds them NOW
  for (uint32_t k = 0; k < 0x928; ++k) dst[k] = p->src4[k] ? FeLoadBE(p->src4[k]) : p->r4[k];
}
// Keyed by (packet address, execution ordinal within the frame) - NG2's fix (alt b6347fa), ported 2026-09-27: both
// sides count an address's executions since their own last swap, so a snapshot pairs with the same execution. A
// per-address FIFO paired Fable's template draws (1FA98980/9C0/A00, run many times a frame at different surface
// sizes - FE_F16: pitch 1280 against 320) with the wrong execution, and kept the bridge's pre-registration
// executions at the front for good.
std::unordered_map<uint64_t, std::deque<FeSnap*>> g_fe_snaps;
std::atomic<uint32_t> g_fe_frame{0};                  // XE_SWAP packets the front end decoded
std::unordered_map<uint32_t, uint32_t> g_fe_ord;      // front end: address -> executions this frame (guest thread)
std::unordered_map<uint32_t, uint32_t> g_br_ord;      // bridge: the same, reset at BridgeSwap (GPU thread)
std::atomic<uint64_t> g_fe_expired{0};
std::atomic<uint64_t> g_fe_calls{0}, g_fe_skipped{0}, g_fe_draws{0}, g_fe_compared{0}, g_fe_unmatched{0};
uint32_t g_fe_mis[0x5000];
uint64_t g_fe_cmp_total = 0;
std::atomic<uint64_t> g_fe_ops[128];
uint64_t g_fe_align_drops = 0, g_fe_align_none = 0;
uint64_t g_fe_mis_memsrc = 0, g_fe_mis_memsrc_now_bridge = 0, g_fe_mis_packet = 0;

thread_local int t_fe_depth = 0;
uint64_t g_fe_bin_mask = ~0ull, g_fe_bin_select = ~0ull;
std::atomic<uint64_t> g_fe_predicated_skips{0};
// [p3 src] VALUE HUNT (SRC8: the per-draw VS c0-c3 are written into the packets by the XDK's dirty-state flush, hook
// 65/64, and appear in no argument, no 256-byte sample and no device word at its exit): for the first draws of the
// census frame with a non-trivial c0-c3, scan guest physical memory for those 16 dwords (big-endian, as stored) and log
// every hit - the hits outside the command buffer are the candidate sources.
void FeHuntValues(uint32_t draw_addr) {
  // HUNT1 (17:23): the first six were all the start-up template draw (one c0-c3), found only in command-buffer memory.
  // Now: 8 draws with DISTINCT c0 rows; two patterns each - row c0 (4 dwords as stored) and column 0 (c0.x c1.x c2.x
  // c3.x, a transposed source); hits inside the command-buffer area (0x1F400000-0x1FC00000) are counted apart.
  static int hunted = 0;
  static std::vector<uint32_t> seen;
  static const bool on = std::getenv("FABLE2_P3HUNT") != nullptr;   // off by default: 8 whole-memory scans stall the frame
  if (!on || hunted >= 8) return;
  const uint32_t* c = g_fe_regs + 0x4000;
  int nontrivial = 0;
  for (int k = 0; k < 16; ++k) if (c[k] != 0 && c[k] != 0x3F800000u && c[k] != 0xBF800000u) ++nontrivial;
  if (nontrivial < 8 || std::find(seen.begin(), seen.end(), c[0] ^ c[5] ^ c[10]) != seen.end()) return;
  seen.push_back(c[0] ^ c[5] ^ c[10]);
  ++hunted;
  auto* ks = rex::system::kernel_state();
  const uint8_t* base = ks && ks->memory() ? ks->memory()->TranslatePhysical<const uint8_t*>(0) : nullptr;
  if (!base) return;
  auto be4 = [](uint8_t* d, const uint32_t* v, int stride) {
    for (int k = 0; k < 4; ++k) { const uint32_t x = v[k * stride]; d[k * 4] = uint8_t(x >> 24); d[k * 4 + 1] = uint8_t(x >> 16); d[k * 4 + 2] = uint8_t(x >> 8); d[k * 4 + 3] = uint8_t(x); }
  };
  uint8_t row[16], col[16];
  be4(row, c, 1);
  be4(col, c, 4);
  std::string hr, hc;
  int nr = 0, nc = 0, nr_cb = 0, nc_cb = 0;
  for (uint64_t off = 0; off < 0x20000000ull;) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(base + off, &mbi, sizeof(mbi))) break;
    const uint64_t rend = std::min<uint64_t>(0x20000000ull, uint64_t(static_cast<const uint8_t*>(mbi.BaseAddress) - base) + mbi.RegionSize);
    const DWORD pr = mbi.Protect & 0xFF;
    if (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) && (pr == PAGE_READONLY || pr == PAGE_READWRITE || pr == PAGE_EXECUTE_READ || pr == PAGE_EXECUTE_READWRITE))
      for (uint64_t q = off; q + 16 <= rend; q += 4) {
        const bool cb = q >= 0x1F400000ull && q < 0x1FC00000ull;
        if (std::memcmp(base + q, row, 16) == 0) { if (cb) ++nr_cb; else { if (nr < 8) hr += fmt::format(" {:08X}", uint32_t(q)); ++nr; } }
        if (std::memcmp(base + q, col, 16) == 0) { if (cb) ++nc_cb; else { if (nc < 8) hc += fmt::format(" {:08X}", uint32_t(q)); ++nc; } }
      }
    off = rend;
  }
  // HUNT2: physical memory held the row only inside command buffers. The game's heaps and stacks are in the VIRTUAL
  // ranges (0x00000000-0x9FFFFFFF, separate host views): scan those too.
  std::string vr, vc;
  int vnr = 0, vnc = 0;
  const uint8_t* vbase = ks->memory()->TranslateVirtual<const uint8_t*>(0);
  if (vbase)
    for (uint64_t off = 0; off < 0xA0000000ull;) {
      MEMORY_BASIC_INFORMATION mbi;
      if (!VirtualQuery(vbase + off, &mbi, sizeof(mbi))) break;
      const uint64_t rend = std::min<uint64_t>(0xA0000000ull, uint64_t(static_cast<const uint8_t*>(mbi.BaseAddress) - vbase) + mbi.RegionSize);
      const DWORD pr = mbi.Protect & 0xFF;
      if (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) && (pr == PAGE_READONLY || pr == PAGE_READWRITE || pr == PAGE_EXECUTE_READ || pr == PAGE_EXECUTE_READWRITE))
        for (uint64_t q = off; q + 16 <= rend; q += 4) {
          if (std::memcmp(vbase + q, row, 16) == 0) { if (vnr < 8) vr += fmt::format(" {:08X}", uint32_t(q)); ++vnr; }
          if (std::memcmp(vbase + q, col, 16) == 0) { if (vnc < 8) vc += fmt::format(" {:08X}", uint32_t(q)); ++vnc; }
        }
      off = rend;
    }
  REXLOG_INFO("[p3hunt] draw {:08X} c0 = {:08X} {:08X} {:08X} {:08X}: PHYSICAL row outside command buffers {} (in them {}):{} column {}:{} | VIRTUAL row {}:{} column {}:{}",
              draw_addr, c[0], c[1], c[2], c[3], nr, nr_cb, hr, nc, hc, vnr, vr, vnc, vc);
}
// [p5 exec] The side-effect packets, as the command processor performs them (NG2's decoding; predicated-off packets
// never reach here). Register effects go through FeSet (which forwards the ones outside the draw ranges).
inline uint32_t FeGpuSwap(uint32_t v, uint32_t endian) {
  switch (endian & 3) {
    case 1: return ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
    case 2: return _byteswap_ulong(v);        // k8in32 (the two cases were swapped until 2026-09-27 evening -
    case 3: return (v << 16) | (v >> 16);     // k16in32   read from NG2's GS; only COND_WRITE polls used them)
    default: return v;
  }
}
template <typename BeFn>
void FeSideEffect(uint32_t op, uint32_t cnt, uint32_t i, uint32_t w, BeFn&& be) {
  auto* ks = rex::system::kernel_state();
  auto* ksm = ks ? ks->memory() : nullptr;
  if (op == 0x3D && cnt >= 2) {                                    // MEM_WRITE
    const uint32_t a0 = be(i + 1);
    for (uint32_t k = 0; k + 1 < cnt && i + 2 + k < w; ++k) Px(1, a0 + 4 * k, be(i + 2 + k));
  } else if (op == 0x45 && cnt >= 6 && i + 6 < w) {                // COND_WRITE
    const uint32_t wi = be(i + 1), poll = be(i + 2), ref = be(i + 3), mask = be(i + 4), wa = be(i + 5), wd = be(i + 6);
    uint32_t v = 0;
    if (wi & 0x10) {
      const uint8_t* pp = ksm ? ksm->TranslatePhysical<const uint8_t*>(poll & ~3u) : nullptr;
      uint32_t raw = 0;
      if (pp) std::memcpy(&raw, pp, 4);
      v = FeGpuSwap(raw, poll & 3);
    } else {
      v = poll < 0x5000 ? g_fe_regs[poll] : 0;
    }
    bool m = false;
    switch (wi & 7) {
      case 1: m = (v & mask) < ref; break;
      case 2: m = (v & mask) <= ref; break;
      case 3: m = (v & mask) == ref; break;
      case 4: m = (v & mask) != ref; break;
      case 5: m = (v & mask) >= ref; break;
      case 6: m = (v & mask) > ref; break;
      case 7: m = true; break;
      default: break;
    }
    if (m) {
      if (wi & 0x100) Px(1, wa, wd);
      else if (wa < 0x5000) FeSet(wa, wd);
    }
  } else if (op == 0x3E && cnt >= 2 && i + 2 < w) {                // REG_TO_MEM
    const uint32_t r = be(i + 1);
    Px(1, be(i + 2), r < 0x5000 ? g_fe_regs[r] : 0);
  } else if (op == 0x54 && cnt >= 1 && i + 1 < w) {                // INTERRUPT
    Px(4, be(i + 1));
  } else if (op == 0x21 && cnt >= 3 && i + 3 < w) {                // REG_RMW
    const uint32_t info = be(i + 1), am = be(i + 2), om = be(i + 3), r = info & 0x1FFF;
    uint32_t v = r < 0x5000 ? g_fe_regs[r] : 0;
    v &= ((info >> 31) & 1) ? ((am & 0x1FFF) < 0x5000 ? g_fe_regs[am & 0x1FFF] : 0) : am;
    v |= ((info >> 30) & 1) ? ((om & 0x1FFF) < 0x5000 ? g_fe_regs[om & 0x1FFF] : 0) : om;
    if (r < 0x5000) FeSet(r, v);
  } else if (op == 0x3C && cnt >= 4 && i + 4 < w) {                // WAIT_REG_MEM: memory waits honoured in order
    if (be(i + 1) & 0x10) {
      Px(6, be(i + 1), be(i + 2), be(i + 3));
      Px(0, be(i + 4));
      g_px_waits.fetch_add(1, std::memory_order_relaxed);
    }
  } else if (op == 0x58 && cnt >= 3 && i + 3 < w) {                // EVENT_WRITE_SHD: a fence
    const uint32_t ini = be(i + 1);
    FeSet(0x21F9, ini & 0x3F);
    if (ini >> 31) Px(2, be(i + 2)); else Px(1, be(i + 2), be(i + 3));
  } else if (op == 0x5A && cnt >= 2 && i + 2 < w) {                // EVENT_WRITE_EXT: screen extents
    FeSet(0x21F9, be(i + 1) & 0x3F);
    Px(9, be(i + 1), be(i + 2));
  } else if (op == 0x5B && cnt >= 1 && i + 1 < w) {                // EVENT_WRITE_ZPD: occlusion query samples
    FeSet(0x21F9, be(i + 1) & 0x3F);
    Px(8, be(i + 1), g_fe_regs[0x2325]);   // RB_SAMPLE_COUNT_ADDR at this packet
  }
}
uint32_t FeDecode(const uint8_t* body, uint32_t n, uint32_t phys_base) {
  // HOT PATH in recorder mode (P5P / PFP profiles, 19:08-19:30: FeDecode was 19% of the saturated recorder against the
  // plugin's ~15% parse): one unaligned load + byte swap per dword, no locked increments per packet, census-only
  // bookkeeping only when a census runs.
  auto be = [&](uint32_t i) {
    uint32_t v;
    std::memcpy(&v, body + size_t(i) * 4, 4);
    return _byteswap_ulong(v);
  };
  const uint32_t w = n / 4;
  auto* ks = rex::system::kernel_state();
  uint32_t i = 0;
  while (i < w) {
    const uint32_t h = be(i);
    if (g_src) t_fe_pkt = (phys_base + i * 4) & 0x1FFFFFFFu;   // [p3 src] this packet's address, for FeSet's last-writer map
    // 0xFFFFFFFF is an UNFILLED header: the library reserves a packet, writes its payload (a UP draw's raw vertices)
    // and patches the header afterwards. Skipping it decoded the payload as packets (FE_F3: vertex floats written
    // into 0x2002..0x2029). Stop here; the next decode resumes once the header is real.
    if (h == 0xFFFFFFFFu) break;
    const uint32_t t = h >> 30;
    // A packet that runs past what has been written yet: stop BEFORE it; the next decode resumes here.
    const uint32_t need = t == 0 ? ((h >> 16) & 0x3FFF) + 2 : t == 1 ? 3 : t == 2 ? 1 : ((h >> 16) & 0x3FFF) + 2;
    if (i + need > w) break;
    if (t == 0) {
      const uint32_t base = h & 0x7FFF, cnt = ((h >> 16) & 0x3FFF) + 1, one = (h >> 15) & 1;
      const uint32_t avail = std::min<uint32_t>(cnt, w - (i + 1));
      if (!one && FeInDrawRange(base, avail)) {
        FeSetRun(base, body + size_t(i + 1) * 4, avail);   // [p6] the common case, in bulk
      } else {
        for (uint32_t k = 0; k < cnt && i + 1 + k < w; ++k) {
          const uint32_t reg = one ? base : base + k;
          if (reg < 0x5000) FeSet(reg, be(i + 1 + k));
        }
      }
      i += 1 + cnt;
    } else if (t == 1) {
      if (i + 2 < w) { FeSet(h & 0x7FF, be(i + 1)); FeSet((h >> 11) & 0x7FF, be(i + 2)); }
      i += 3;
    } else if (t == 2) {
      ++i;
    } else {
      const uint32_t op = (h >> 8) & 0x7F, cnt = ((h >> 16) & 0x3FFF) + 1;
      g_fe_ops[op].store(g_fe_ops[op].load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);   // one decoder thread: no lock prefix
      {   // [coverage] a packet type this front end does not act on - logged at its FIRST sight, with the address
        static const uint64_t kKnown[2] = {
            // 0x10 NOP 0x21 REG_RMW 0x22 DRAW_INDX 0x23 VIZ_QUERY 0x26 WAIT_FOR_IDLE 0x27 IM_LOAD 0x2B IM_LOAD_IMMEDIATE
            // 0x2D SET_CONSTANT 0x2F LOAD_ALU_CONSTANT 0x36 DRAW_INDX_2 0x37 IB_PFD 0x3B INVALIDATE_STATE
            // 0x3C WAIT_REG_MEM 0x3D MEM_WRITE 0x3E REG_TO_MEM 0x3F INDIRECT_BUFFER
            (1ull << 0x10) | (1ull << 0x21) | (1ull << 0x22) | (1ull << 0x23) | (1ull << 0x26) | (1ull << 0x27) |
                (1ull << 0x2B) | (1ull << 0x2D) | (1ull << 0x2F) | (1ull << 0x36) | (1ull << 0x37) | (1ull << 0x3B) |
                (1ull << 0x3C) | (1ull << 0x3D) | (1ull << 0x3E) | (1ull << 0x3F),
            // 0x45 COND_WRITE 0x46 EVENT_WRITE 0x48 ME_INIT 0x50/0x51 BIN 0x54 INTERRUPT 0x55 SET_CONSTANT2
            // 0x56 SET_SHADER_CONSTANTS 0x58 SHD 0x5A EXT 0x5B ZPD 0x5E CONTEXT_UPDATE 0x60-0x63 BIN LO/HI
            // 0x64 XE_SWAP 0x7F the flattener's address marker
            (1ull << (0x45 - 64)) | (1ull << (0x46 - 64)) | (1ull << (0x48 - 64)) | (1ull << (0x50 - 64)) |
                (1ull << (0x51 - 64)) | (1ull << (0x54 - 64)) | (1ull << (0x55 - 64)) | (1ull << (0x56 - 64)) |
                (1ull << (0x58 - 64)) | (1ull << (0x5A - 64)) | (1ull << (0x5B - 64)) | (1ull << (0x5E - 64)) |
                (1ull << (0x60 - 64)) | (1ull << (0x61 - 64)) | (1ull << (0x62 - 64)) | (1ull << (0x63 - 64)) |
                (1ull << (0x64 - 64)) | (1ull << (0x7F - 64))};
        if (!((kKnown[op >> 6] >> (op & 63)) & 1)) {
          static uint64_t seen[2] = {0, 0};
          if (!((seen[op >> 6] >> (op & 63)) & 1)) {
            seen[op >> 6] |= 1ull << (op & 63);
            REXLOG_WARN("[coverage] UNHANDLED PM4 type-3 op {:02X} (count {}) at {:08X} - first sight", op, cnt,
                        (phys_base + i * 4) & 0x1FFFFFFFu);
          }
        }
      }
      if (op == 0x7F && h == kFeAddrMarker && i + 1 < w) {   // [p3 rec] the flattener's address of the next draw
        g_fe_next_addr = be(i + 1);
        g_fe_next_addr_valid = true;
        i += 2;
        continue;
      }
      if (op == 0x64) {   // [coverage] every 300 frames: every packet type the decoder counted since the last report
        static uint32_t cov_frames = 0;
        static uint64_t last[128] = {};
        if ((++cov_frames % 300) == 0) {
          std::string ops;
          for (int o = 0; o < 128; ++o) {
            const uint64_t v = g_fe_ops[o].load(std::memory_order_relaxed);
            if (v != last[o]) ops += fmt::format(" {:02X}:{}", o, v - last[o]);
            last[o] = v;
          }
          REXLOG_INFO("[coverage] packet types decoded in the last 300 frames:{}", ops);
        }
      }
      if (op == 0x64) {   // XE_SWAP: a new frame for the ordinals; the read-point test's "at swap" reading
        g_fe_frame.fetch_add(1, std::memory_order_relaxed);
        g_fe_ord.clear();
        std::lock_guard<std::mutex> lock(g_fe_mu);
        for (FeSnap* p : g_fe_frame_snaps) { FeReread(p, p->sw); p->has_sw = true; FeRelease(p); }
        g_fe_frame_snaps.clear();
        // Unmatched keys never come back (command buffers rotate addresses): expire what is > 3 frames old, counted,
        // every 60 frames, so the map cannot grow for good (FR_P2: it did, and every snapshot became a fresh 39 KB new).
        const uint32_t fr = g_fe_frame.load(std::memory_order_relaxed);
        if ((fr % 60) == 0)
          for (auto it = g_fe_snaps.begin(); it != g_fe_snaps.end();) {
            auto& q = it->second;
            while (!q.empty() && q.front()->frame + 3 < fr) { FeRelease(q.front()); q.pop_front(); g_fe_expired.fetch_add(1, std::memory_order_relaxed); }
            it = q.empty() ? g_fe_snaps.erase(it) : std::next(it);
          }
      }
      // Predicated tiling, as the GPU (and the plugin) apply it: SET_BIN_MASK/SELECT LO/HI keep the bin state; a
      // predicated packet (header bit 0) runs only when mask & select overlap.
      if ((op == 0x50 || op == 0x51) && i + 2 < w) {   // SET_BIN_MASK / SET_BIN_SELECT, 64-bit (hi, lo)
        (op == 0x50 ? g_fe_bin_mask : g_fe_bin_select) = (uint64_t(be(i + 1)) << 32) | be(i + 2);
        i += 1 + cnt;
        continue;
      }
      if (op >= 0x60 && op <= 0x63 && i + 1 < w) {
        const uint64_t v = be(i + 1);
        uint64_t& tgt = op <= 0x61 ? g_fe_bin_mask : g_fe_bin_select;
        tgt = (op & 1) ? ((tgt & 0xFFFFFFFFull) | (v << 32)) : ((tgt & ~0xFFFFFFFFull) | v);
        i += 1 + cnt;
        continue;
      }
      if ((h & 1) && (g_fe_bin_mask & g_fe_bin_select) == 0) {
        g_fe_predicated_skips.fetch_add(1, std::memory_order_relaxed);
        g_fe_next_addr_valid = false;   // [p3 rec] a skipped draw's marker must not label the next draw (FR_C8)
        i += 1 + cnt;
        continue;
      }
      static const uint32_t kSpace[5] = {0x4000, 0x4800, 0x4900, 0x4908, 0x2000};
      if ((op == 0x55 || op == 0x56) && i + 1 < w) {       // SET_CONSTANT2 / SET_SHADER_CONSTANTS: a register range
        const uint32_t base = be(i + 1) & 0xFFFF;
        const uint32_t avail = std::min<uint32_t>(cnt - 1, w - std::min<uint32_t>(w, i + 2));
        if (FeInDrawRange(base, avail)) FeSetRun(base, body + size_t(i + 2) * 4, avail);
        else for (uint32_t k = 0; k < avail; ++k) if (base + k < 0x5000) FeSet(base + k, be(i + 2 + k));
      } else if (op == 0x23 && i + 1 < w) {                // VIZ_QUERY, as the command processor does it
        const uint32_t d0 = be(i + 1), id = d0 & 0x3F;
        if (!(d0 & 0x100)) {
          FeSet(0x21F9, 7);   // VGT_EVENT_INITIATOR = VIZQUERY_START
        } else {
          FeSet(0x21F9, 8);   // VIZQUERY_END
          const uint32_t reg = id < 32 ? 0x0C44 : 0x0C45;   // PA_SC_VIZ_QUERY_STATUS_0 / _1
          FeSet(reg, g_fe_regs[reg] | (1u << (id & 31)));
        }
      } else if (op == 0x2D && i + 1 < w) {                       // SET_CONSTANT
        const uint32_t d = be(i + 1), idx = d & 0x7FF, typ = (d >> 16) & 0xFF;
        if (typ < 5) {
          const uint32_t base = kSpace[typ] + idx;
          const uint32_t avail = std::min<uint32_t>(cnt - 1, w - std::min<uint32_t>(w, i + 2));
          if (FeInDrawRange(base, avail)) {
            FeSetRun(base, body + size_t(i + 2) * 4, avail);   // [p6] SET_CONSTANT in bulk
          } else {
            for (uint32_t k = 0; k + 1 < cnt && i + 2 + k < w; ++k) {
              const uint32_t reg = base + k;
              if (reg < 0x5000) FeSet(reg, be(i + 2 + k));
            }
          }
        }
      } else if (op == 0x2F && i + 3 < w && ks && ks->memory()) {   // LOAD_ALU_CONSTANT: from guest memory
        const uint32_t addr = be(i + 1) & 0x3FFFFFFF, d = be(i + 2), size = be(i + 3) & 0xFFF;
        const uint32_t idx = d & 0x7FF, typ = (d >> 16) & 0xFF;
        const uint8_t* src = ks->memory()->TranslatePhysical<const uint8_t*>(addr);
        if (src && typ < 5)
          for (uint32_t k = 0; k < size; ++k) {
            const uint32_t reg = kSpace[typ] + idx + k;
            const uint8_t* p = src + k * 4;
            if (reg < 0x5000) {
              FeSet(reg, (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]));
              g_fe_src[reg] = addr + k * 4;
            }
          }
      } else if ((op == 0x3F || op == 0x37) && i + 2 < w && ks && ks->memory() && t_fe_depth < 4) {   // INDIRECT_BUFFER(_PFD): execute it now
        const uint32_t ib = be(i + 1) & 0x1FFFFFFFu, ibn = be(i + 2) & 0xFFFFF;
        const uint8_t* src = ks->memory()->TranslatePhysical<const uint8_t*>(ib);
        if (src && ibn) {
          ++t_fe_depth;
          const uint32_t saved_issuer = t_fe_issuer;
          t_fe_issuer = (phys_base + i * 4) & 0x1FFFFFFFu;   // this IB packet
          FeDecode(src, ibn * 4, ib);
          t_fe_issuer = saved_issuer;
          --t_fe_depth;
        }
      } else if (g_p5exec && (op == 0x3D || op == 0x45 || op == 0x3E || op == 0x54 || op == 0x21 || op == 0x3C ||
                              op == 0x58 || op == 0x5A || op == 0x5B)) {
        FeSideEffect(op, cnt, i, w, be);   // [p5 exec] (0x58-0x5B also set VGT_EVENT_INITIATOR there)
      } else if ((op == 0x46 || (op >= 0x58 && op <= 0x5B)) && i + 1 < w) {   // EVENT_WRITE family, as the CP does
        FeSet(0x21F9, be(i + 1) & 0x3F);                                        // VGT_EVENT_INITIATOR
      } else if (op == 0x27 && cnt >= 2 && i + 2 < w) {                  // IM_LOAD: shader microcode at an address
        const uint32_t addr_type = be(i + 1), dwords = be(i + 2) & 0xFFFF;
        if ((addr_type & 3) == 0) { g_fe_vs = addr_type & ~3u; g_fe_vs_dwords = dwords; g_fe_vs_inline = false; }
        else { g_fe_ps = addr_type & ~3u; g_fe_ps_dwords = dwords; g_fe_ps_inline = false; }
      } else if (op == 0x2B && cnt >= 2 && i + 2 < w) {                  // IM_LOAD_IMMEDIATE: microcode in the packet
        const bool ps = (be(i + 1) & 3) != 0;
        const uint32_t dwords = std::min<uint32_t>(be(i + 2) & 0xFFFF, cnt - 2);
        std::vector<uint8_t>& dst = ps ? g_fe_imm_ps : g_fe_imm_vs;
        dst.assign(body + (i + 3) * 4, body + (i + 3 + dwords) * 4);   // copied: the ring words are reused later
        (ps ? g_fe_ps_inline : g_fe_vs_inline) = true;
        (ps ? g_fe_ps_dwords : g_fe_vs_dwords) = dwords;
      } else if (op == 0x64 && cnt >= 4 && g_p3draw && i + 4 < w) {      // XE_SWAP: magic, front buffer, width, height
        ::fable2::ngpu::FrontEndSwap(be(i + 2), be(i + 3), be(i + 4), g_fe_regs, g_fe_dirty);
        if (g_p5exec) Px(5, be(i + 2), be(i + 3), be(i + 4));   // [p5 exec] the plugin's swap: gamma, present, counter
        g_fe_swaps_issued.fetch_add(1, std::memory_order_relaxed);
      } else if (op == 0x22 || op == 0x36) {                         // DRAW_INDX / DRAW_INDX_2
        // The draw packet's own registers, as the CP writes them before drawing: VGT_DRAW_INITIATOR, and for an
        // indexed (DMA) draw VGT_DMA_BASE / VGT_DMA_SIZE. DRAW_INDX has a viz-query dword first.
        const uint32_t d0 = op == 0x22 ? i + 2 : i + 1;
        const uint32_t this_init = d0 < w ? be(d0) : 0;
        g_fe_draws.fetch_add(1, std::memory_order_relaxed);
        if (g_p3draw) {   // BEFORE the packet's own registers, like the plugin's callback (it sees the previous 21FA-C)
          ::fable2::ngpu::FeDrawInfo d{};
          d.draw_initiator = this_init;
          if (((this_init >> 6) & 3) == 0 && d0 + 2 < w) { d.index_addr = be(d0 + 1); d.index_size = be(d0 + 2); }
          d.vs_addr = g_fe_vs; d.vs_dwords = g_fe_vs_dwords; d.ps_addr = g_fe_ps; d.ps_dwords = g_fe_ps_dwords;
          d.vs_inline = g_fe_vs_inline; d.ps_inline = g_fe_ps_inline;
          d.vs_code = g_fe_imm_vs.empty() ? nullptr : g_fe_imm_vs.data();
          d.ps_code = g_fe_imm_ps.empty() ? nullptr : g_fe_imm_ps.data();
          d.vs_code_dwords = uint32_t(g_fe_imm_vs.size() / 4); d.ps_code_dwords = uint32_t(g_fe_imm_ps.size() / 4);
          ::fable2::ngpu::FrontEndDraw(g_fe_regs, g_fe_dirty, d);
          g_fe_draws_issued.fetch_add(1, std::memory_order_relaxed);
        }
        const uint32_t addr_s = g_fe_next_addr_valid ? g_fe_next_addr : ((phys_base + i * 4) & 0x1FFFFFFFu);
        g_fe_next_addr_valid = false;
        const uint32_t ord = g_fe_every ? g_fe_ord[addr_s]++ : 0;   // ordinals only matter to the comparison
        if (g_src && g_src_frame_counter && g_src_frame_counter->load(std::memory_order_relaxed) == g_src_frame) {
          FeHuntValues(addr_s);   // [p3 src] where do the per-draw VS constants live? (a few draws, whole-memory scan)
          std::lock_guard<std::mutex> lock(g_src_mu);   // [p3 src] every draw of the census frame
          if (!g_src_fe_f) g_src_fe_f = std::fopen("p3_fe.bin", "wb");
          if (g_src_fe_f && g_src_draws < 40000) {
            const uint32_t hdr[4] = {addr_s, t_fe_issuer, ord, g_fe_frame.load(std::memory_order_relaxed)};
            std::fwrite(hdr, sizeof(hdr), 1, g_src_fe_f);
            std::fwrite(g_fe_regs + 0x2000, 4, 0x400, g_src_fe_f);
            std::fwrite(g_fe_regs + 0x4000, 4, 0x928, g_src_fe_f);
            if (!g_src_fesrc_f) g_src_fesrc_f = std::fopen("p3_fe_src.bin", "wb");
            if (g_src_fesrc_f) std::fwrite(g_fe_src + 0x4000, 4, 0x928, g_src_fesrc_f);
            if (!g_fe_kick_f) g_fe_kick_f = std::fopen("p3_fe_kick.bin", "wb");
            if (g_fe_kick_f) { const uint32_t kk = uint32_t(g_fe_kicks.load(std::memory_order_relaxed)); std::fwrite(&kk, 4, 1, g_fe_kick_f); }
            if (!g_src_wpkt_f) g_src_wpkt_f = std::fopen("p3_fe_wpkt.bin", "wb");
            if (g_src_wpkt_f) { std::fwrite(g_fe_wpkt + 0x2000, 4, 0x400, g_src_wpkt_f); std::fwrite(g_fe_wpkt + 0x4000, 4, 0x928, g_src_wpkt_f); }
            ++g_src_draws;
          }
        }
        // Sampled BY ADDRESS (every execution of a sampled packet is snapshotted), through a HASH of the address: the
        // plain (addr>>2) % N residue aliased with the stream's layout (FE_F17: residue 0 and residue 7 saw different
        // packet populations - 0.329 vs 0.000 packet-sourced mismatches per comparison, and r7's memory-load rate 25%
        // higher). FABLE2_P3FE_RES still picks which hash residue.
        if (FeSampled(addr_s)) {
          // Snapshot the front end's registers for this draw, keyed by the packet's physical address.
          auto* s = FeAlloc();
          std::memcpy(s->r2, g_fe_regs + 0x2000, sizeof(s->r2));
          std::memcpy(s->r4, g_fe_regs + 0x4000, sizeof(s->r4));
          std::memcpy(s->src4, g_fe_src + 0x4000, sizeof(s->src4));
          s->frame = g_fe_frame.load(std::memory_order_relaxed);
          s->init = this_init;   // THIS packet's initiator (the plugin's callback gets it in RexNgpuDraw::draw_initiator)
          const uint32_t addr = addr_s;   // (the flattener's marker in recorder mode - never recomputed from the buffer)
          {   // [p3 rec] key diagnostic: the first sampled keys after 500,000 draws, front-end side
            static std::atomic<int> shown{0};
            if (g_fe_draws.load(std::memory_order_relaxed) > 500000 && shown.fetch_add(1) < 12)
              REXLOG_INFO("[p3key] FE  addr {:08X} ord {} fe-frame {} init {:08X}", addr, ord, g_fe_frame.load(), this_init);
          }
          FeSnap* br = nullptr;
          if (g_fe_defer || g_p3rec) {   // the plugin may have run it already: then the front end compares, now
            std::lock_guard<std::mutex> lock(g_fe_mu);
            auto it = g_br_snaps.find((uint64_t(addr) << 32) | ord);
            if (it != g_br_snaps.end()) {
              auto& bq = it->second;
              while (!bq.empty() && bq.front()->frame + 3 < g_bridge_frames) { FeRelease(bq.front()); bq.pop_front(); ++g_br_expired; }
              while (!bq.empty() && bq.front()->init != this_init) { FeRelease(bq.front()); bq.pop_front(); ++g_fe_align_drops; }
              if (!bq.empty()) { br = bq.front(); bq.pop_front(); }
              if (bq.empty()) g_br_snaps.erase(it);
            }
            if (br) {
              s->kick = uint32_t(g_fe_kicks.load(std::memory_order_relaxed));
              g_fe_frame_snaps.push_back(s);   // refs 2: the frame list + this comparison
            }
          }
          if (br) {
            ++g_fe_second;
            FeCompare(s, br->r2, br->r4, addr, this_init);
            std::lock_guard<std::mutex> lock(g_fe_mu);
            FeRelease(br);
          } else {
          std::lock_guard<std::mutex> lock(g_fe_mu);
          // NO wholesale clear: clearing pending snapshots put every queue out of step (FE_F8: 0x21FC, read from the
          // same packet by both sides, "differed" in 75% of comparisons). Emptied queues are erased at the pop, so
          // the map holds only addresses with pending snapshots; a runaway is logged, never silently realigned.
          if (g_fe_snaps.size() > (1u << 20)) {
            static bool warned = false;
            if (!warned) { warned = true; REXLOG_INFO("[p3fe] snapshot map over 1 M addresses - the bridge is not consuming"); }
          }
          auto& q = g_fe_snaps[(uint64_t(addr) << 32) | ord];
          q.push_back(s);
          s->kick = uint32_t(g_fe_kicks.load(std::memory_order_relaxed));
          g_fe_frame_snaps.push_back(s);
          if (q.size() > 8) { FeRelease(q.front()); q.pop_front(); }
          }
        }
        // [p3 rec] Recorded AND snapshotted: only now may the plugin (waiting in WaitRecorder) look this draw up.
        if (g_p3rec && g_p3draw && !g_p5exec) {   // the plugin waits on these only while it still parses (not in P5)
          const uint64_t h0 = g_rec_head.load(std::memory_order_relaxed);
          g_rec_keys[h0 % kRecKeys] = (uint64_t(addr_s) << 32) | this_init;
          g_rec_head.store(h0 + 1, std::memory_order_release);
          g_rec_draws_done.fetch_add(1, std::memory_order_release);
        }
        // The CP writes the draw packet's own registers AFTER the plugin's draw callback (the callback sees the
        // PREVIOUS draw's VGT_DRAW_INITIATOR / VGT_DMA_*) - so the front end applies them after its snapshot too.
        if (d0 < w) {
          FeSet(0x21FC, this_init);
          if (((this_init >> 6) & 3) == 0 && d0 + 2 < w) { FeSet(0x21FA, be(d0 + 1)); FeSet(0x21FB, be(d0 + 2)); }
        }
      }
      i += 1 + cnt;
    }
  }
  return i * 4;
}
uint32_t g_fe_ptr = 0;   // the front end's read position in the software ring (guest address), sequential across calls
// [p3 rec] Flatten: copy the packets of body (guest physical phys_base) into out, INDIRECT_BUFFERs replaced by their
// contents (read now - the game recycles them), each draw packet preceded by the address marker. Returns the bytes
// consumed (stops before an incomplete or unfilled packet, like FeDecode).
thread_local int t_flat_depth = 0;
uint64_t g_flat_bin_mask = ~0ull, g_flat_bin_select = ~0ull;   // the flattener's own bin state (kick thread)
uint32_t FeFlatten(const uint8_t* body, uint32_t n, uint32_t phys_base, std::vector<uint8_t>& out) {
  // Contiguous RUNS of packets are copied with one memcpy each, breaking only at a draw (its address marker goes
  // first) and at an INDIRECT_BUFFER (inlined); the per-packet append was ~10% of the D3D thread (P5P profile).
  auto be = [&](uint32_t i) {
    const uint8_t* p = body + i * 4;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
  };
  auto put = [&](uint32_t v) {
    const uint8_t b[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
    out.insert(out.end(), b, b + 4);
  };
  auto flush = [&](uint32_t from, uint32_t to) {
    if (to <= from) return;
    const size_t at = out.size();
    out.resize(at + size_t(to - from) * 4);
    std::memcpy(out.data() + at, body + size_t(from) * 4, size_t(to - from) * 4);
  };
  const uint32_t w = n / 4;
  auto* ks = rex::system::kernel_state();
  uint32_t i = 0, run = 0;
  while (i < w) {
    const uint32_t h = be(i);
    if (h == 0xFFFFFFFFu) break;
    const uint32_t t = h >> 30;
    const uint32_t need = t == 0 ? ((h >> 16) & 0x3FFF) + 2 : t == 1 ? 3 : t == 2 ? 1 : ((h >> 16) & 0x3FFF) + 2;
    if (i + need > w) break;
    if (t == 3) {
      const uint32_t op = (h >> 8) & 0x7F;
      if ((op == 0x50 || op == 0x51) && i + 2 < w)
        (op == 0x50 ? g_flat_bin_mask : g_flat_bin_select) = (uint64_t(be(i + 1)) << 32) | be(i + 2);
      if (op >= 0x60 && op <= 0x63 && i + 1 < w) {
        const uint64_t v = be(i + 1);
        uint64_t& tgt = op <= 0x61 ? g_flat_bin_mask : g_flat_bin_select;
        tgt = (op & 1) ? ((tgt & 0xFFFFFFFFull) | (v << 32)) : ((tgt & ~0xFFFFFFFFull) | v);
      }
      if (op == 0x3F || op == 0x37) {   // INDIRECT_BUFFER / INDIRECT_BUFFER_PFD (the plugin treats both alike)
        flush(run, i);
        const bool skip = (h & 1) && (g_flat_bin_mask & g_flat_bin_select) == 0;   // predicated off: not inlined
        if (!skip && i + 2 < w && ks && ks->memory() && t_flat_depth < 4) {
          const uint32_t ib = be(i + 1) & 0x1FFFFFFFu, ibn = be(i + 2) & 0xFFFFF;
          const uint8_t* src = ks->memory()->TranslatePhysical<const uint8_t*>(ib);
          if (src && ibn) {
            ++t_flat_depth;
            FeFlatten(src, ibn * 4, ib, out);
            --t_flat_depth;
          }
        }
        i += need;
        run = i;
        continue;
      }
      if (op == 0x22 || op == 0x36) {
        flush(run, i);
        put(kFeAddrMarker);
        put((phys_base + i * 4) & 0x1FFFFFFFu);
        run = i;
      }
    }
    i += need;
  }
  flush(run, i);
  return i * 4;
}
void RecorderThread() {
  SetThreadDescription(GetCurrentThread(), L"fable2 p3 recorder");
  RecBatch batch;
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(g_rec_mu);
      g_rec_cv.wait(lock, [] { return !g_rec_queue.empty(); });
      batch = std::move(g_rec_queue.front());
      g_rec_queue.pop_front();
    }
    std::lock_guard<std::mutex> kick_lock(g_fe_kick_mu);   // the decoder's state is the recorder's alone in this mode
    // BUSY SHARE (the readout that keeps working at the frame cap, where fps stops moving): time spent decoding and
    // recording per 5 s window, as a share of wall time.
    static LARGE_INTEGER qf{}, win0{};
    static uint64_t busy = 0;
    if (!qf.QuadPart) { QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&win0); }
    LARGE_INTEGER b0, b1;
    QueryPerformanceCounter(&b0);
    FeDecode(batch.bytes.data(), uint32_t(batch.bytes.size()), 0);
    if (g_p5exec && g_push) {   // [p5 exec] this batch's side effects, after its draws are recorded; the read pointer last
      Px(3, batch.rptr_end);
      g_push(t_px.data(), uint32_t(t_px.size() / 4));
      t_px.clear();
      g_px_batches.fetch_add(1, std::memory_order_relaxed);
    }
    QueryPerformanceCounter(&b1);
    busy += uint64_t(b1.QuadPart - b0.QuadPart);
    if (b1.QuadPart - win0.QuadPart >= 5 * qf.QuadPart) {
      REXLOG_INFO("[p3rec] recorder busy {:.1f}% of the last {:.1f} s", 100.0 * double(busy) / double(b1.QuadPart - win0.QuadPart),
                  double(b1.QuadPart - win0.QuadPart) / double(qf.QuadPart));
      busy = 0;
      win0 = b1;
    }
  }
}
void RecorderReport() {
  static uint64_t n = 0;
  if ((++n % 600) != 0) return;
  if (g_p5exec)
    REXLOG_INFO("[p5x] recorder handed the executor {} batches, {} records ({} register writes, {} memory waits)",
                g_px_batches.load(), g_px_recs.load(), g_px_regs.load(), g_px_waits.load());
  REXLOG_INFO("[p3rec] recorder: {} batches ({} MB flattened), {} draws recorded; the plugin waited {} times ({} ms total, {} timeouts); front-end-only draws skipped {}, plugin-only draws {}",
              g_rec_batches.load(), g_rec_bytes.load() >> 20, g_rec_draws_done.load(), g_rec_waits.load(),
              g_rec_wait_us.load() / 1000, g_rec_wait_timeouts.load(), g_rec_skipped.load(), g_rec_plugin_extra.load());
}

// KICK MODE (plugin export RexNgpuSetKickCallback): the hardware ring is decoded from the front end's own read index
// to the kicked write index, on the guest thread that kicked, expanding INDIRECT_BUFFERs where they execute - the
// execution order, with exact packet boundaries. When active, the per-call stream decode is off.
std::atomic<bool> g_fe_kick_mode{false};
uint32_t g_fe_rptr = 0;
std::vector<uint8_t> g_fe_ringcopy;
std::atomic<uint64_t> g_fe_ring_resets{0}, g_fe_resyncs{0};   // (g_fe_kicks is declared with FeSnap)
void FeKick(uint32_t ring_ptr, uint32_t ring_bytes, uint32_t wptr) {
  if (!g_fe) return;
  if (wptr == 0xFFFFFFFFu) {   // [gs] InitializeRingBuffer: the ring restarts at 0 whatever its address (NG2 ed71ba62)
    std::lock_guard<std::mutex> lk(g_fe_kick_mu);
    g_fe_rptr = 0;
    g_fe_ring_resets.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  g_kick_tid.store(GetCurrentThreadId(), std::memory_order_relaxed);   // [p3 src] the draw-issuing thread
  std::unique_lock<std::mutex> kick_lock(g_fe_kick_mu, std::defer_lock);
  if (!g_p3rec) kick_lock.lock();   // [p3 rec] the recorder owns the decoder state; the flattener's state is the kick's
  auto* ks = rex::system::kernel_state();
  if (!ks || !ks->memory() || !ring_bytes) return;
  const uint8_t* ring = ks->memory()->TranslatePhysical<const uint8_t*>(ring_ptr);
  if (!ring) return;
  const uint32_t nd = ring_bytes / 4;
  wptr %= nd;
  g_fe_kicks.fetch_add(1, std::memory_order_relaxed);
  // A ring (re)initialisation - new address or size - restarts the read index (NG2: a stale index re-decoded one
  // stretch forever and drifted every snapshot queue). Fable initialises its ring more than once at start-up.
  static uint32_t last_ptr = 0, last_bytes = 0, stuck = 0;
  if (ring_ptr != last_ptr || ring_bytes != last_bytes) {
    last_ptr = ring_ptr; last_bytes = ring_bytes; g_fe_rptr = 0; stuck = 0;
    g_fe_ring_resets.fetch_add(1, std::memory_order_relaxed);
  }
  if (g_fe_rptr == wptr) return;
  {   // read-point test: snapshots decoded at an EARLIER kick get their "next kick" reading now
    const uint32_t kn = uint32_t(g_fe_kicks.load(std::memory_order_relaxed));
    std::lock_guard<std::mutex> lock(g_fe_mu);
    for (FeSnap* p : g_fe_frame_snaps)
      if (!p->has_k1 && p->kick < kn) { FeReread(p, p->k1); p->has_k1 = true; }
  }
  // Contiguous copy of the new ring dwords (the ring wraps; a packet may straddle the end).
  g_fe_ringcopy.clear();
  for (uint32_t k = g_fe_rptr; k != wptr; k = (k + 1) % nd)
    g_fe_ringcopy.insert(g_fe_ringcopy.end(), ring + k * 4, ring + k * 4 + 4);
  const uint32_t base = ring_ptr + g_fe_rptr * 4;   // exact for draws before a wrap; ring-resident draws are rare
  if (g_p3rec) {
    std::vector<uint8_t> flat;
    flat.reserve(g_fe_ringcopy.size() * 4);
    const uint32_t used = FeFlatten(g_fe_ringcopy.data(), uint32_t(g_fe_ringcopy.size()), base, flat);
    g_fe_rptr = (g_fe_rptr + used / 4) % nd;
    stuck = used ? 0 : stuck + 1;
    if (stuck >= 16) { g_fe_rptr = wptr; stuck = 0; g_fe_resyncs.fetch_add(1, std::memory_order_relaxed); }
    if (!flat.empty()) {
      g_rec_batches.fetch_add(1, std::memory_order_relaxed);
      g_rec_bytes.fetch_add(flat.size(), std::memory_order_relaxed);
      {
        std::lock_guard<std::mutex> lock(g_rec_mu);
        g_rec_queue.push_back(RecBatch{std::move(flat), g_fe_rptr});
      }
      g_rec_cv.notify_one();
    }
    return;
  }
  if (g_fe_defer) {
    // Buffer; decode the whole frame when this kick carries its XE_SWAP (a top-level type-3 op 0x64).
    if (g_fe_pending.empty()) g_fe_pending_base = base;
    g_fe_pending.insert(g_fe_pending.end(), g_fe_ringcopy.begin(), g_fe_ringcopy.end());
    g_fe_rptr = wptr;
    // The frame's XE_SWAP is inside an indirect buffer, not at the ring's top level (FE_F20: a top-level scan never
    // fired). Trigger on the game's own swap call instead: the first kick after it carries the swap's packets.
    const uint32_t mk = g_fe_marker.load(std::memory_order_relaxed);
    const bool swap = mk != g_fe_marker_done;
    if (swap) g_fe_marker_done = mk;
    if (!swap && g_fe_pending.size() < (64u << 20)) return;
    const uint32_t pused = FeDecode(g_fe_pending.data(), uint32_t(g_fe_pending.size()), g_fe_pending_base);
    g_fe_pending.erase(g_fe_pending.begin(), g_fe_pending.begin() + std::min<size_t>(pused, g_fe_pending.size()));
    g_fe_pending_base += pused;
    return;
  }
  const uint32_t used = FeDecode(g_fe_ringcopy.data(), uint32_t(g_fe_ringcopy.size()), base);
  g_fe_rptr = (g_fe_rptr + used / 4) % nd;
  // No progress for 16 kicks: the read index is off a packet boundary (a re-initialisation at the same address, or a
  // stop before a packet that never completes) - resynchronise at the write index, counted.
  stuck = used ? 0 : stuck + 1;
  if (stuck >= 16) { g_fe_rptr = wptr; stuck = 0; g_fe_resyncs.fetch_add(1, std::memory_order_relaxed); }
}
thread_local Open t_stack[32];
thread_local int t_depth = 0;

bool SafeRead32(const uint8_t* p, uint32_t* out) {
  __try {
    *out = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool ReadGuest(uint32_t va, uint32_t* out) {
  auto* ks = rex::system::kernel_state();
  if (!ks || !ks->memory()) return false;
  const uint8_t* p = ks->memory()->TranslateVirtual<const uint8_t*>(va);
  if (!p) return false;
  return SafeRead32(p, out);
}

void Init() {
  static bool done = false;
  if (done) return;
  done = true;
  // The front end (P3) has its own switches, independent of the P2 census: FABLE2_P3DRAW=1 draws from it
  // (FABLE2_P3FE=0 there = no comparison, the release-shaped measurement).
  if (const char* m = std::getenv("FABLE2_P3DRAW"); m && (*m == '1' || *m == '2')) {
    g_p3draw = true;
    if (*m == '2') {
      g_p3rec = true;
      std::thread(RecorderThread).detach();
      REXLOG_INFO("[p3rec] DESIGN (b): the guest thread flattens each kick; a recording thread decodes and draws");
    }
    g_fe = true;
    if (!std::getenv("FABLE2_P3FE")) g_fe_every = 64;   // the comparison stays on, sparser
    REXLOG_INFO("[p3draw] THE FRONT END DRAWS: the guest thread records every draw it decodes and swaps at XE_SWAP; the plugin's draw and swap callbacks are compare-only");
  }
  if (const char* m = std::getenv("FABLE2_P3FE"); m && *m) {
    g_fe_every = uint32_t(std::strtoul(m, nullptr, 0));   // 0 = no comparison (only with FABLE2_P3DRAW)
    g_fe = g_fe_every != 0 || g_p3draw;
    if (const char* r = std::getenv("FABLE2_P3FE_RES"); r && *r && g_fe_every) g_fe_residue = uint32_t(std::strtoul(r, nullptr, 0)) % g_fe_every;
    if (const char* d = std::getenv("FABLE2_P3FE_DEFER"); d && *d == '1') g_fe_defer = true;
    REXLOG_INFO("[p3fe] decode point: {}", g_fe_defer ? "DEFERRED to the frame's XE_SWAP kick" : "at each kick");
    REXLOG_INFO("[p3fe] guest-thread front end ON (validation): draws at (addr>>2) % {} == {} compared with the bridge", g_fe_every, g_fe_residue);
  }
  const char* e = std::getenv("FABLE2_P2");
  if (!e || !*e) return;
  if (const char* m = std::getenv("FABLE2_P3SRC"); m && *m) {
    g_src_frame = uint32_t(std::strtoul(m, nullptr, 0));
    g_src = true;
    g_src_frame_counter = &g_frame;
    REXLOG_INFO("[p3src] source census ON: guest frame {} -> p3_src.bin (calls: args + samples) and p3_fe.bin (draws)", g_src_frame);
  }
  unsigned a = 0, b = 0;
  if (std::sscanf(e, "%u:%u", &a, &b) != 2 || !b) {
    REXLOG_INFO("[p2] FABLE2_P2='{}' is not <first frame>:<frames> - census off", e);
    return;
  }
  g_start = a;
  g_count = b;
  if (const char* c = std::getenv("FABLE2_P2_CURSOR"); c && *c) g_cursor_off = uint32_t(std::strtoul(c, nullptr, 0));
  if (const char* c = std::getenv("FABLE2_P2_CURSOR2"); c && *c) g_cursor2_off = uint32_t(std::strtoul(c, nullptr, 0));
  g_discover = std::getenv("FABLE2_P2_DISCOVER") != nullptr;
  if (const char* m = std::getenv("FABLE2_P3MAP"); m && *m) {
    unsigned f = 0, n = 0;
    if (std::sscanf(m, "%u:%u", &f, &n) == 2 && n) {
      g_p3_frame = f;
      g_p3_max = n;
      g_p3 = true;
      REXLOG_INFO("[p3] register-map discovery ON: guest frame {}, up to {} calls -> p3_guest.bin / p3_bridge.bin", f, n);
    }
  }
  g_recs.reserve(1u << 20);
  g_on = true;
  REXLOG_INFO("[p2] census ON: frames {}..{} (by the swap entry point), cursor device+0x{:X} and +0x{:X}{}", g_start,
              g_start + g_count - 1, g_cursor_off, g_cursor2_off, g_discover ? ", discover mode" : "");
}

bool Recording() {
  const uint32_t f = g_frame.load(std::memory_order_relaxed);
  return f >= g_start && f < g_start + g_count;
}

void Discover(int hook, uint32_t dev) {
  const uint32_t n = g_discover_logged.fetch_add(1);
  if (n >= 48) return;
  char line[600];
  int k = std::snprintf(line, sizeof(line), "[p2] discover hook %d dev %08X:", hook, dev);
  for (uint32_t i = 0; i < 32 && k > 0 && k < int(sizeof(line)) - 12; ++i) {
    uint32_t v = 0;
    const bool ok = ReadGuest(dev + i * 4, &v);
    k += std::snprintf(line + k, sizeof(line) - k, ok ? " %08X" : " ????????", v);
  }
  uint32_t v2 = 0;
  const bool ok2 = ReadGuest(dev + g_cursor2_off, &v2);
  std::snprintf(line + k, sizeof(line) - k, " | +0x%X=%08X%s", g_cursor2_off, v2, ok2 ? "" : "(unreadable)");
  REXLOG_INFO("{}", line);
  if (n < 8) {
    // The second cursor candidates: the +0x3400 area (Fable's +0x3484 copy) - 64 words, for finding NG2's field.
    char hi[800];
    int m = std::snprintf(hi, sizeof(hi), "[p2] discover hook %d dev+0x3400:", hook);
    for (uint32_t i = 0; i < 64 && m > 0 && m < int(sizeof(hi)) - 12; ++i) {
      uint32_t v = 0;
      const bool ok = ReadGuest(dev + 0x3400 + i * 4, &v);
      m += std::snprintf(hi + m, sizeof(hi) - m, ok ? " %08X" : " ????????", v);
    }
    REXLOG_INFO("{}", hi);
  }
}

void Write() {
  if (g_written) return;
  g_written = true;
  if (g_src) {
    std::lock_guard<std::mutex> lock2(g_src_mu);
    if (g_src_f) { std::fclose(g_src_f); g_src_f = nullptr; }
    if (g_src_fe_f) { std::fclose(g_src_fe_f); g_src_fe_f = nullptr; }
    if (g_src_fesrc_f) { std::fclose(g_src_fesrc_f); g_src_fesrc_f = nullptr; }
    if (g_src_wpkt_f) { std::fclose(g_src_wpkt_f); g_src_wpkt_f = nullptr; }
    for (FILE** f : {&g_src_kick_f, &g_fe_kick_f, &g_guest_kick_f}) if (*f) { std::fclose(*f); *f = nullptr; }
    { std::lock_guard<std::mutex> lk(g_p3_mu); if (g_p3_guest) std::fflush(g_p3_guest); }   // the census frame's device dumps reach the file
    REXLOG_INFO("[p3src] wrote p3_src.bin ({} calls) and p3_fe.bin ({} draws) for guest frame {}", g_src_calls, g_src_draws, g_src_frame);
  }
  std::vector<Rec> recs;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    recs.swap(g_recs);
  }
  FILE* f = std::fopen("p2_guest.bin", "wb");
  if (!f) {
    REXLOG_INFO("[p2] COULD NOT OPEN p2_guest.bin - {} records lost", recs.size());
    return;
  }
  const uint32_t header[8] = {0x3250474E /* 'NGP2' */, 2, uint32_t(sizeof(Rec)), g_start, g_count, g_cursor_off,
                              g_cursor2_off, uint32_t(recs.size())};
  std::fwrite(header, sizeof(header), 1, f);
  std::fwrite(recs.data(), sizeof(Rec), recs.size(), f);
  // Section 2 (format version 2): the LAST WRITER of every 64-byte block any hooked call wrote since the first
  // frame - {u32 block (physical >> 6), u32 frame, u16 hook, u16 0}, after a {u32 'BLK1', u32 count} header.
  std::vector<uint32_t> blocks;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    blocks.reserve(g_blocks.size() * 3);
    for (const auto& kv : g_blocks) {
      blocks.push_back(kv.first);
      blocks.push_back(kv.second.frame);
      blocks.push_back(uint32_t(kv.second.hook));
    }
  }
  const uint32_t bhdr[2] = {0x314B4C42u /* 'BLK1' */, uint32_t(blocks.size() / 3)};
  std::fwrite(bhdr, sizeof(bhdr), 1, f);
  std::fwrite(blocks.data(), sizeof(uint32_t), blocks.size(), f);
  std::fclose(f);
  {
    struct B { int hook; uint64_t in, out; };
    std::vector<B> bal;
    for (int h = 0; h < 512; ++h) {
      const uint64_t in = g_hook_in[h].load(), out = g_hook_out[h].load();
      if (in != out) bal.push_back({h, in, out});
    }
    std::sort(bal.begin(), bal.end(), [](const B& a, const B& b) {
      return (a.in > a.out ? a.in - a.out : a.out - a.in) > (b.in > b.out ? b.in - b.out : b.out - b.in);
    });
    std::string s;
    for (size_t k = 0; k < bal.size() && k < 12; ++k)
      s += fmt::format(" hook {} in {} out {};", bal[k].hook, bal[k].in, bal[k].out);
    REXLOG_INFO("[p2] unbalanced hooks (entries vs exits since start): {} of 304{}", bal.size(), s);
  }
  REXLOG_INFO("[p2] wrote p2_guest.bin: {} call ranges for frames {}..{} (enters {}, exits {}, nesting mismatches {}, "
              "stack overflows {}, unreadable cursors {}); LAST WRITER map {} blocks of 64 bytes since frame 1",
              recs.size(), g_start, g_start + g_count - 1, g_enters.load(), g_exits.load(), g_mismatch.load(),
              g_overflow.load(), g_noread.load(), blocks.size() / 3);
}

}  // namespace

void Enter(int hook, uint32_t r3, bool lib, const uint32_t* args8) {
  Init();
  if (g_fe && !g_fe_kick_mode.load(std::memory_order_relaxed)) {
    static bool tried = false;
    if (!tried) {
      tried = true;
      using SetKickFn = void (*)(void (*)(uint32_t, uint32_t, uint32_t));
      HMODULE m = GetModuleHandleA("rexgpu-xenos.dll");
      auto f = m ? reinterpret_cast<SetKickFn>(GetProcAddress(m, "RexNgpuSetKickCallback")) : nullptr;
      if (f) { f(&FeKick); g_fe_kick_mode = true; }
      if (f && g_p3rec) if (const char* x = std::getenv("FABLE2_P5EXEC"); x && *x == '1') {
        auto setx = reinterpret_cast<void (*)(int)>(GetProcAddress(m, "RexNgpuSetExecMode"));
        g_push = reinterpret_cast<PushFn>(GetProcAddress(m, "RexNgpuPushSideEffects"));
        if (setx && g_push) {
          g_p5exec = true;
          setx(1);
          REXLOG_INFO("[p5x] EXECUTOR MODE: the plugin no longer parses the ring; the recorder hands it each kick's side effects");
        } else {
          REXLOG_INFO("[p5x] FABLE2_P5EXEC refused: the plugin has no executor exports (RexNgpuSetExecMode / RexNgpuPushSideEffects)");
        }
      }
      REXLOG_INFO("[p3fe] kick callback {}", f ? "registered - the hardware ring drives the front end (execution order)"
                                              : "MISSING - per-call stream decode");
    }
  }
  if (!g_on) return;
  uint32_t dev = g_device.load(std::memory_order_relaxed);
  if (!dev) {
    // The first library call with a device-shaped first argument: the write cursor at +0x30 and its limit at
    // +0x38 both point into the CPU's view of physical memory, limit above cursor, within 16 MB. The library
    // writes its persistent packet templates at device creation - before the first swap - so waiting for the
    // frame marker missed them (census 2: 1416 replayed draws/frame with no writer).
    if (!lib || r3 < 0x40000000u || r3 >= 0x90000000u) return;
    uint32_t cur = 0, lim = 0;
    if (!ReadGuest(r3 + g_cursor_off, &cur) || !ReadGuest(r3 + 0x38, &lim)) return;
    const uint32_t hi = cur >> 28;
    // 0xA..0xF: Fable's cursor lives in the 0xFFxxxxxx view (NG2's in 0xA..0xE); excluding 0xF meant Fable learned
    // the device only at the first swap and every state write before it was missed (P3 front end, 12:35).
    if (!(hi >= 0xA && hi <= 0xF) || lim <= cur || lim - cur > 0x1000000u) return;
    g_device.store(r3, std::memory_order_relaxed);
    dev = r3;
    REXLOG_INFO("[p2] device object at {:08X} (first library entry, hook {}; cursor {:08X} limit {:08X})", r3, hook,
                cur, lim);
  }
  if (g_discover) Discover(hook, dev);
  if (hook >= 0 && hook < 512) g_hook_in[hook].fetch_add(1, std::memory_order_relaxed);
  // Every call from the first frame on feeds the last-writer map; the per-call records only inside the window.
  if (Recording()) g_enters.fetch_add(1, std::memory_order_relaxed);
  if (t_depth >= 32) {
    g_overflow.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  Open& o = t_stack[t_depth++];
  o.hook = uint16_t(hook);
  if (args8) std::memcpy(o.args, args8, sizeof(o.args)); else std::memset(o.args, 0, sizeof(o.args));
  o.wrote = false;
  if (!ReadGuest(dev + g_cursor_off, &o.cur_in)) { o.cur_in = 0; g_noread.fetch_add(1, std::memory_order_relaxed); }
  // [p3 src] the device's constant shadows (+0x780 VS c0.., +0x1780 PS c0.., 8 KB) at the ENTRY of the dirty-state flush
  // (hooks 64 / 65) in the census frame - does the shadow hold the per-draw constants when the flush starts?
  if (g_src && (hook == 64 || hook == 65) && g_frame.load(std::memory_order_relaxed) == g_src_frame) {
    auto* ks = rex::system::kernel_state();
    const uint8_t* p = ks && ks->memory() ? ks->memory()->TranslateVirtual<const uint8_t*>(dev + 0x780) : nullptr;
    std::lock_guard<std::mutex> lock(g_src_mu);
    static FILE* f = nullptr;
    static uint32_t n = 0;
    if (!f) f = std::fopen("p3_flush_entry.bin", "wb");
    if (f && p && n < 60000) {
      const uint32_t hdr[3] = {uint32_t(hook), GetCurrentThreadId(), o.cur_in};
      std::fwrite(hdr, sizeof(hdr), 1, f);
      std::fwrite(p, 1, 0x2000, f);
      // SRC9: VS c0-c7 are NOT in the shadow at flush entry (14-35%). 4 KB behind each pointer-like argument r3-r10
      // (zeros when unreadable), to see whether the flush reads them from a structure it is handed.
      static uint8_t blk[4096];
      for (int k = 0; k < 8; ++k) {
        const uint32_t a = o.args[k];
        const bool ptr_like = (a >= 0x40000000u && a < 0x90000000u) || (a >= 0xA0000000u && a < 0xE1000000u);
        const uint8_t* q = ptr_like ? ks->memory()->TranslateVirtual<const uint8_t*>(a & ~3u) : nullptr;
        MEMORY_BASIC_INFORMATION mbi;
        const bool ok = q && VirtualQuery(q, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) &&
                        (mbi.Protect & 0xFF) != PAGE_NOACCESS &&
                        static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize >= q + sizeof(blk);
        if (ok) std::memcpy(blk, q, sizeof(blk)); else std::memset(blk, 0, sizeof(blk));
        const uint32_t ah[2] = {uint32_t(k + 3), a};
        std::fwrite(ah, sizeof(ah), 1, f);
        std::fwrite(blk, 1, sizeof(blk), f);
      }
      if ((++n % 256) == 0) std::fflush(f);
    }
  }
  if (!ReadGuest(dev + g_cursor2_off, &o.cur2_in)) o.cur2_in = 0;
}

void Exit(int hook) {
  if (g_on && g_device.load(std::memory_order_relaxed) && hook >= 0 && hook < 512)
    g_hook_out[hook].fetch_add(1, std::memory_order_relaxed);
  if (!g_on || t_depth == 0) return;
  const uint32_t dev = g_device.load(std::memory_order_relaxed);
  // Pop to the matching entry (a tail call or an early return skipped an exit hook: count it, keep going).
  int i = t_depth - 1;
  while (i >= 0 && t_stack[i].hook != uint16_t(hook)) --i;
  if (i < 0) {
    g_mismatch.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  uint16_t flags = i != t_depth - 1 ? 1 : 0;
  Open o = t_stack[i];
  t_depth = i;
  if (!dev) return;
  const bool recording = Recording();
  if (recording) g_exits.fetch_add(1, std::memory_order_relaxed);
  Rec r;
  r.frame = g_frame.load(std::memory_order_relaxed);
  r.tid = GetCurrentThreadId();
  r.hook = o.hook;
  r.flags = flags;
  r.cur_in = o.cur_in;
  r.cur2_in = o.cur2_in;
  if (!ReadGuest(dev + g_cursor_off, &r.cur_out)) r.cur_out = 0;
  if (!ReadGuest(dev + g_cursor2_off, &r.cur2_out)) r.cur2_out = 0;
  // [p3 fe] decode what this call wrote into the front end's register file (every call, from the device on).
  // Only at the OUTERMOST exit on this thread: an outer call's range contains its inner calls' bytes, so decoding at
  // every exit would apply them twice (and count their draws twice).
  // The software ring is decoded as ONE STREAM, from the front end's own read position to the cursor now: packet
  // boundaries carry across calls (a call's range can begin inside a packet an earlier call reserved - the first
  // front end decoded per range and desynchronised). A cursor behind the read position or > 1 MB ahead is a buffer
  // switch: resynchronise at the cursor (counted).
  if (g_fe && !g_fe_kick_mode.load(std::memory_order_relaxed) && t_depth == 0 && r.cur_out) {
    if (!g_fe_ptr) g_fe_ptr = r.cur_in ? r.cur_in : r.cur_out;
    if (r.cur_out < g_fe_ptr || r.cur_out - g_fe_ptr > (1u << 20)) {
      g_fe_skipped.fetch_add(1, std::memory_order_relaxed);
      g_fe_ptr = r.cur_in && r.cur_in <= r.cur_out ? r.cur_in : r.cur_out;
    }
    const uint32_t len = r.cur_out - g_fe_ptr;
    auto* ks = rex::system::kernel_state();
    const uint8_t* q = (len && ks && ks->memory()) ? ks->memory()->TranslateVirtual<const uint8_t*>(g_fe_ptr) : nullptr;
    if (q) {
      g_fe_ptr += FeDecode(q, len, g_fe_ptr);
      g_fe_calls.fetch_add(1, std::memory_order_relaxed);
    }
  }
  // [p3 src] the call's arguments and what they point at, in the census frame.
  // [p3 src] which calls: those that wrote packets and every call enclosing one (SRC4: the kick thread alone joined
  // 418 of 12,180 draws - the packets are written on other threads). A writer marks its open ancestors.
  const bool src_wrote = o.wrote || (r.cur_out != r.cur_in && r.cur_in && r.cur_out);
  if (src_wrote) for (int k = 0; k < t_depth; ++k) t_stack[k].wrote = true;
  if (g_src && r.frame == g_src_frame && g_src_calls < 250000 && src_wrote) {   // a Fable frame is ~91,000 hooked calls (P2: 5.48 M over 60 frames); NG2's 6,000 caught only its start
    std::lock_guard<std::mutex> lock2(g_src_mu);
    if (!g_src_f) g_src_f = std::fopen("p3_src.bin", "wb");
    if (g_src_f) {
      auto* ks = rex::system::kernel_state();
      static uint8_t sample[8][256];
      uint32_t which[8], n = 0;
      // Host pages checked first: the runtime's guest access-violation handler runs before a structured-exception
      // guard (NG2 leg ng2_081 died reading 0x81000008).
      auto readable = [](const void* q) {
        const uintptr_t page = reinterpret_cast<uintptr_t>(q) >> 12;
        auto it = g_src_page_ok.find(page);
        if (it != g_src_page_ok.end()) return it->second;
        MEMORY_BASIC_INFORMATION mbi;
        bool ok = VirtualQuery(q, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD);
        if (ok) {
          const DWORD pr = mbi.Protect & 0xFF;
          ok = pr == PAGE_READONLY || pr == PAGE_READWRITE || pr == PAGE_EXECUTE_READ || pr == PAGE_EXECUTE_READWRITE;
        }
        g_src_page_ok[page] = ok;
        return ok;
      };
      for (uint32_t k = 0; k < 8; ++k) {
        const uint32_t a = o.args[k];
        const bool ptr_like = (a >= 0x40000000u && a < 0x90000000u) || (a >= 0xA0000000u && a < 0xE1000000u);
        if (!ptr_like || !ks || !ks->memory()) continue;
        const uint8_t* q = ks->memory()->TranslateVirtual<const uint8_t*>(a & ~3u);
        if (q && readable(q) && readable(q + 255)) { std::memcpy(sample[n], q, 256); which[n++] = k; }
      }
      const uint32_t hdr[13] = {uint32_t(o.hook), r.tid, r.cur_in, r.cur_out, o.args[0], o.args[1], o.args[2], o.args[3],
                                o.args[4], o.args[5], o.args[6], o.args[7], n};
      std::fwrite(hdr, sizeof(hdr), 1, g_src_f);
      if (!g_src_kick_f) g_src_kick_f = std::fopen("p3_src_kick.bin", "wb");
      if (g_src_kick_f) { const uint32_t kk = uint32_t(g_fe_kicks.load(std::memory_order_relaxed)); std::fwrite(&kk, 4, 1, g_src_kick_f); }
      for (uint32_t k = 0; k < n; ++k) {
        const uint32_t sh[2] = {which[k], o.args[which[k]] & ~3u};
        std::fwrite(sh, sizeof(sh), 1, g_src_f);
        std::fwrite(sample[k], 1, 256, g_src_f);
      }
      ++g_src_calls;
    }
  }
  // [p3 map] the device object at the exit of a call that wrote packets, in the discovery frame.
  if (g_p3 && r.frame == g_p3_frame && r.cur_out != r.cur_in && r.cur_in && r.cur_out) {
    std::lock_guard<std::mutex> lock(g_p3_mu);
    if (g_p3_recorded < g_p3_max) {
      if (!g_p3_guest) g_p3_guest = std::fopen("p3_guest.bin", "wb");
      auto* ks = rex::system::kernel_state();
      const uint8_t* p = ks && ks->memory() ? ks->memory()->TranslateVirtual<const uint8_t*>(dev) : nullptr;
      if (g_p3_guest && p) {
        const uint32_t hdr[5] = {uint32_t(o.hook), r.cur_in, r.cur_out, r.cur2_in, r.cur2_out};
        std::fwrite(hdr, sizeof(hdr), 1, g_p3_guest);
        std::fwrite(p, kDevBytes, 1, g_p3_guest);
        // The packets THIS call wrote, [cur_in, cur_out) up to 16 KB (format v2): decoded offline, each register the
        // call's flush wrote is matched to the device word it came from - a map free of the per-tile transforms.
        uint32_t n = 0;
        const uint8_t* q = nullptr;
        if (r.cur_out > r.cur_in && r.cur_out - r.cur_in <= 16384u)
          q = ks->memory()->TranslateVirtual<const uint8_t*>(r.cur_in);
        if (q) n = r.cur_out - r.cur_in;
        std::fwrite(&n, 4, 1, g_p3_guest);
        if (n) std::fwrite(q, n, 1, g_p3_guest);
        if (!g_guest_kick_f) g_guest_kick_f = std::fopen("p3_guest_kick.bin", "wb");
        if (g_guest_kick_f) { const uint32_t kk = uint32_t(g_fe_kicks.load(std::memory_order_relaxed)); std::fwrite(&kk, 4, 1, g_guest_kick_f); }
        const uint32_t lo = r.cur_in & 0x1FFFFFFFu, hi = r.cur_out & 0x1FFFFFFFu;
        if (hi > lo) g_p3_ranges.push_back({lo, hi});
        if (++g_p3_recorded == g_p3_max) {
          std::fclose(g_p3_guest);
          g_p3_guest = nullptr;
          REXLOG_INFO("[p3] wrote p3_guest.bin: {} calls with the device object ({} bytes each)", g_p3_recorded, kDevBytes);
        }
      }
    }
  }
  std::lock_guard<std::mutex> lock(g_mu);
  if (recording) g_recs.push_back(r);
  // The last-writer map: every 64-byte block the call's cursor range covers (both fields), from the first frame.
  for (int pass = 0; pass < 2; ++pass) {
    const uint32_t a = pass ? r.cur2_in : r.cur_in, b = pass ? r.cur2_out : r.cur_out;
    if (!a || !b || a == b) continue;
    const uint32_t lo = a & 0x1FFFFFFFu, hi = b & 0x1FFFFFFFu;
    if (hi < lo || hi - lo > (1u << 22)) continue;   // a wrap or a nonsense range (>4 MB): not from this call
    for (uint32_t blk = lo >> 6; blk <= ((hi - 1) >> 6); ++blk) g_blocks[blk] = Writer{r.frame, o.hook, 0};
  }
}

void FrameMarker(uint32_t r3) {
  Init();
  g_fe_marker.fetch_add(1, std::memory_order_relaxed);   // [p3fe] deferred decode trigger
  if (!g_on) return;
  if (!g_device.load(std::memory_order_relaxed) && r3 >= 0x40000000u && r3 < 0x90000000u) {
    g_device.store(r3, std::memory_order_relaxed);
    REXLOG_INFO("[p2] device object at {:08X} (the swap entry point's first argument)", r3);
  }
  const uint32_t f = g_frame.fetch_add(1, std::memory_order_relaxed) + 1;
  if (f == g_start + g_count) Write();
}

// One comparison, front-end snapshot against the bridge's register values for the same execution. Called by
// whichever side arrives second (the deferred front end often decodes a draw after the plugin executed it). The
// caller must NOT hold g_fe_mu; g_cmp_mu serialises the counters between the two threads.
namespace {   // (the anonymous namespace FeSnap / FeDecode live in)
std::mutex g_cmp_mu;
void FeCompare(FeSnap* s, const uint32_t* b2, const uint32_t* b4, uint32_t packet_addr, uint32_t draw_initiator) {
  std::lock_guard<std::mutex> cmp_lock(g_cmp_mu);
    for (uint32_t k = 0; k < 0x400; ++k)
      if (s->r2[k] != b2[k]) ++g_fe_mis[0x2000 + k];
    for (uint32_t k = 0; k < 0x928; ++k)
      if (s->r4[k] != b4[k]) {
        ++g_fe_mis[0x4000 + k];
        // Timing test: a memory-loaded constant whose SOURCE now holds the bridge's value = the plugin read the
        // memory later than the front end (the game had rewritten it in between).
        if (s->src4[k]) {
          ++g_fe_mis_memsrc;
          auto* ks = rex::system::kernel_state();
          const uint8_t* p = ks && ks->memory() ? ks->memory()->TranslatePhysical<const uint8_t*>(s->src4[k]) : nullptr;
          if (p && ((uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3])) == b4[k])
            ++g_fe_mis_memsrc_now_bridge;
        } else {
          ++g_fe_mis_packet;
        }
      }
    // The first compared draws in full: which registers differ, with both values (front end vs bridge).
    // After the warm-up only, and only draws that differ in a register NOT loaded from memory (the timing effect
    // is understood; the packet-set residue is the question).
    bool packet_diff = false;
    for (uint32_t k = 0; k < 0x400 && !packet_diff; ++k) packet_diff = s->r2[k] != b2[k];
    for (uint32_t k = 0; k < 0x928 && !packet_diff; ++k) packet_diff = s->r4[k] != b4[k] && !s->src4[k];
    static std::atomic<int> dumped{0};
    if (g_fe_compared.load(std::memory_order_relaxed) > 100000 && packet_diff && dumped.fetch_add(1) < 6) {
      std::string d;
      int shown = 0;
      for (uint32_t k = 0; k < 0x400 && shown < 40; ++k)
        if (s->r2[k] != b2[k]) { d += fmt::format(" {:04X}:{:08X}/{:08X}", 0x2000 + k, s->r2[k], b2[k]); ++shown; }
      for (uint32_t k = 0; k < 0x928 && shown < 60; ++k)
        if (s->r4[k] != b4[k]) { d += fmt::format(" {:04X}:{:08X}/{:08X}", 0x4000 + k, s->r4[k], b4[k]); ++shown; }
      REXLOG_INFO("[p3fe] steady-state draw at {:08X} initiator {:08X}: front-end/bridge differing registers:{}", packet_addr, draw_initiator, d);
    }
    {   // [p3fe] what the stale memory-loaded constants ARE: a few examples after the warm-up, as floats, with the
        // source address (is the difference an animation step or a jump?)
      static std::atomic<int> mem_dumped{0};
      if (g_fe_compared.load(std::memory_order_relaxed) > 100000 && mem_dumped.load() < 12) {
        std::string d;
        int shown = 0;
        for (uint32_t k = 0; k < 0x928 && shown < 12; ++k)
          if (s->src4[k] && s->r4[k] != b4[k]) {
            float fa, fb;
            std::memcpy(&fa, &s->r4[k], 4);
            std::memcpy(&fb, &b4[k], 4);
            d += fmt::format(" {:04X}@{:08X}:{:g}/{:g}", 0x4000 + k, s->src4[k], fa, fb);
            ++shown;
          }
        if (shown && mem_dumped.fetch_add(1) < 12)
          REXLOG_INFO("[p3fe] stale memory-loaded constants at draw {:08X} (front end / bridge):{}", packet_addr, d);
      }
    }
    {   // read-point test, memory-loaded registers only: which reading matches what the plugin used
      std::lock_guard<std::mutex> lock(g_fe_mu);
      if (g_fe_compared.load(std::memory_order_relaxed) >= 100000)
        for (uint32_t k = 0; k < 0x928; ++k) {
          if (!s->src4[k]) continue;
          ++g_rp_regs;
          const uint32_t b = b4[k];
          if (s->r4[k] != b) ++g_rp_dec;
          if (!s->has_k1) ++g_rp_k1_absent; else if (s->k1[k] != b) ++g_rp_k1;
          if (!s->has_sw) ++g_rp_sw_absent; else if (s->sw[k] != b) ++g_rp_sw;
        }
      FeRelease(s);
    }
    const uint64_t c = g_fe_compared.fetch_add(1, std::memory_order_relaxed) + 1;
    // Steady state: the counts restart after the first 100,000 comparisons (start-up, before the front end has seen
    // the state the game set before its first kick, is not the question).
    if (c == 100000) { std::memset(g_fe_mis, 0, sizeof(g_fe_mis)); g_fe_mis_memsrc = g_fe_mis_memsrc_now_bridge = g_fe_mis_packet = 0; REXLOG_INFO("[p3fe] warm-up over: mismatch counts restart"); }
    if (c % 500 == 0) {
      std::vector<std::pair<uint32_t, uint32_t>> top;
      uint32_t regs_bad = 0;
      for (uint32_t r = 0; r < 0x5000; ++r)
        if (g_fe_mis[r]) { ++regs_bad; top.push_back({g_fe_mis[r], r}); }   // 0x21F9-0x21FC now set by the front end like the CP
      std::sort(top.rbegin(), top.rend());
      std::string s2;
      for (size_t k = 0; k < top.size() && k < 24; ++k) s2 += fmt::format(" {:04X}:{}", top[k].second, top[k].first);
      std::string ops;
      for (int o = 0; o < 128; ++o)
        if (const uint64_t v = g_fe_ops[o].load()) ops += fmt::format(" {:02X}:{}", o, v);
      REXLOG_INFO("[p3fe] constant mismatches over {} post-warm-up comparisons: {} from memory loads (source now holds the bridge value in {}), {} from packets; pending snapshot addresses {}; alignment: {} snapshots dropped, {} executions with no matching snapshot; ring resets {}, no-progress resyncs {}; expired snapshots {}, fe frames {} | READ POINT over {} memory-loaded register-instances: differ read at decode {}, at next kick {} (not yet read {}), at swap {} (not yet read {}) | DEFER: front end compared second {}, bridge-side expired {}", c > 100000 ? c - 100000 : 0, g_fe_mis_memsrc, g_fe_mis_memsrc_now_bridge, g_fe_mis_packet, g_fe_snaps.size(), g_fe_align_drops, g_fe_align_none, g_fe_ring_resets.load(), g_fe_resyncs.load(), g_fe_expired.load(), g_fe_frame.load(), g_rp_regs, g_rp_dec, g_rp_k1, g_rp_k1_absent, g_rp_sw, g_rp_sw_absent, g_fe_second, g_br_expired);
      REXLOG_INFO("[p3fe] {} draws compared ({} bridge draws had no snapshot); front end decoded {} calls, skipped {} "
                  "ranges, {} kicks, saw {} draws; {} registers ever differ, worst:{} | type-3 ops:{}",
                  c, g_fe_unmatched.load(), g_fe_calls.load(), g_fe_skipped.load(), g_fe_kicks.load(), g_fe_draws.load(), regs_bad,
                  s2, ops);
  }
}

}  // namespace

void BridgeDraw(uint32_t packet_addr, const uint32_t* regs, uint32_t reg_count, uint32_t draw_initiator) {
  if (g_fe && g_fe_every && regs && reg_count >= 0x4928) {
    FeSnap* s = nullptr;
    const uint32_t a = packet_addr & 0x1FFFFFFFu;
    const uint32_t ord = g_br_ord[a]++;
    {   // [p3 rec] key diagnostic, bridge side (same sampling)
      static std::atomic<int> shown{0};
      static std::atomic<uint64_t> seen{0};
      if (seen.fetch_add(1, std::memory_order_relaxed) > 500000 && FeSampled(a) && shown.fetch_add(1) < 12)
        REXLOG_INFO("[p3key] BR  addr {:08X} ord {} br-frame {} init {:08X}", a, ord, g_bridge_frames, draw_initiator);
    }
    {
      std::lock_guard<std::mutex> lock(g_fe_mu);
      auto it = g_fe_snaps.find((uint64_t(a) << 32) | ord);
      if (it != g_fe_snaps.end()) {
        const uint32_t fe_frame = g_fe_frame.load(std::memory_order_relaxed);
        while (!it->second.empty() && it->second.front()->frame + 3 < fe_frame) {   // stale: expired, counted
          FeRelease(it->second.front());
          it->second.pop_front();
          g_fe_expired.fetch_add(1, std::memory_order_relaxed);
        }
      }
      if (it != g_fe_snaps.end() && !it->second.empty()) {
        // Align on the draw's own identity (initiator + index base, both set from the packet on each side): a
        // snapshot for an execution the bridge never makes (a conditional / viz-query draw the plugin skips, a
        // ring-resident draw keyed by an approximate address) is dropped and counted instead of shifting every
        // later comparison at this address (FE_F9 residue 0: 29 M "mismatches" that were misalignment).
        auto& q = it->second;
        while (!q.empty() && q.front()->init != draw_initiator) {
          static std::atomic<int> shown{0};
          if (shown.fetch_add(1) < 8)
            REXLOG_INFO("[p3fe] align drop at {:08X}: front end packet initiator {:08X} | bridge packet initiator {:08X}; queue {}",
                        packet_addr, q.front()->init, draw_initiator, q.size());
          FeRelease(q.front());
          q.pop_front();
          ++g_fe_align_drops;
        }
        if (!q.empty()) { s = q.front(); q.pop_front(); } else { ++g_fe_align_none; }
        if (q.empty()) g_fe_snaps.erase(it);
      }
    }
    if (!s) {
      // Deferred front end (FABLE2_P3FE_DEFER): the plugin ran this sampled draw before the front end decoded its
      // frame - keep the bridge's values for the front end to compare against when it gets there.
      if ((g_fe_defer || g_p3rec) && FeSampled(a)) {
        auto* b = FeAlloc();
        std::memcpy(b->r2, regs + 0x2000, sizeof(b->r2));
        std::memcpy(b->r4, regs + 0x4000, sizeof(b->r4));
        b->init = draw_initiator;
        b->frame = g_bridge_frames;
        b->refs = 1;
        std::lock_guard<std::mutex> lock(g_fe_mu);
        auto& q = g_br_snaps[(uint64_t(a) << 32) | ord];
        q.push_back(b);
        if (q.size() > 8) { FeRelease(q.front()); q.pop_front(); }
      } else {
        g_fe_unmatched.fetch_add(1, std::memory_order_relaxed);
      }
    } else {
      FeCompare(s, regs + 0x2000, regs + 0x4000, packet_addr, draw_initiator);
    }
  }
  if (!g_p3 || !regs || reg_count < 0x4928) return;
  const uint32_t a = packet_addr & 0x1FFFFFFFu;
  std::lock_guard<std::mutex> lock(g_p3_mu);
  if (g_p3_ranges.empty() || g_p3_bridge >= kP3BridgeMax) return;
  bool hit = false;
  for (const auto& r : g_p3_ranges) {
    if (a >= r.first && a < r.second) { hit = true; break; }
  }
  if (!hit) return;
  if (!g_p3_bridge_f) g_p3_bridge_f = std::fopen("p3_bridge.bin", "wb");
  if (!g_p3_bridge_f) return;
  std::fwrite(&packet_addr, 4, 1, g_p3_bridge_f);
  std::fwrite(regs + 0x2000, 4, 0x400, g_p3_bridge_f);
  std::fwrite(regs + 0x4000, 4, 0x928, g_p3_bridge_f);
  if (++g_p3_bridge % 64 == 0) std::fflush(g_p3_bridge_f);
  if (g_p3_bridge == kP3BridgeMax) {
    std::fclose(g_p3_bridge_f);
    g_p3_bridge_f = nullptr;
    REXLOG_INFO("[p3] wrote p3_bridge.bin: {} bridge draws inside recorded call ranges", g_p3_bridge);
  }
}

bool FrontEndDraws() { return g_p3draw; }
// [gs] The game's own graphics system (fable2_native_gs.cpp): no plugin - its register window hands every CP_RB_WPTR
// write to NativeKick on the kicking thread, and the side effects go to its executor. Design (b) as in P5.
void NativeKick(uint32_t ring_ptr, uint32_t ring_bytes, uint32_t wptr) { FeKick(ring_ptr, ring_bytes, wptr); }
bool StartNativeFrontEnd() {
  static bool started = false;
  if (started) return true;
  started = true;
  g_fe = true;
  g_fe_every = 0;          // nothing to compare against: there is no plugin
  g_p3draw = true;
  g_p3rec = true;
  g_p5exec = true;
  g_push = &::fable2::gs::PushSideEffects;
  g_fe_kick_mode = true;
  std::thread(RecorderThread).detach();
  REXLOG_INFO("[native] NATIVE FRONT END ON (own graphics system): each kick flattened on the game's thread, decoded "
              "and drawn on the recording thread, side effects run by the game's executor - rexgpu-xenos is not the "
              "graphics system");
  return true;
}
bool FrontEndRecorderMode() { return g_p3rec; }
// [p3 rec] From the plugin's draw callback: wait until the recorder has recorded as many draws as the plugin has now
// seen, so the plugin cannot pass (and fence) a draw the recorder has not recorded. Bounded: 250 ms, counted.
void WaitRecorder(uint32_t packet_addr, uint32_t draw_initiator) {
  if (!g_p3rec) return;
  g_br_draws_seen.fetch_add(1, std::memory_order_relaxed);
  static uint64_t cursor = 0;   // the plugin thread's position in the recorder's published sequence
  const uint64_t key = (uint64_t(packet_addr & 0x1FFFFFFFu) << 32) | draw_initiator;
  constexpr uint64_t kWindow = 64;
  const uint64_t t0 = GetTickCount64();
  bool waited = false;
  LARGE_INTEGER q0;
  for (;;) {
    const uint64_t head = g_rec_head.load(std::memory_order_acquire);
    if (head - cursor > kRecKeys - 1024) cursor = head - (kRecKeys - 1024);   // fell out of the ring: resync (counted below)
    // The whole published range, not a window: after a stretch of front-end-only draws (a loading screen) the cursor
    // lags far behind and a 64-entry window never finds the plugin's draw again (FR2: 30.8 M "plugin-only" draws).
    for (uint64_t c = cursor; c < head; ++c)
      if (g_rec_keys[c % kRecKeys] == key) {
        g_rec_skipped.fetch_add(c - cursor, std::memory_order_relaxed);   // front-end draws the plugin never called back
        for (uint64_t k = cursor; k < c; ++k) {   // what they are: address + initiator (prim type, source, count)
          static std::atomic<int> shown{0};
          if (shown.fetch_add(1) >= 40) break;
          const uint64_t fk = g_rec_keys[k % kRecKeys];
          const uint32_t in = uint32_t(fk);
          REXLOG_INFO("[p3rec] front-end-only draw (recorded and ISSUED, the plugin never called back): {:08X} initiator {:08X} "
                      "prim {} source {} count {} (plugin's next draw {:08X}/{:08X})",
                      uint32_t(fk >> 32), in, in & 0x3F, (in >> 6) & 3, in >> 16, packet_addr, draw_initiator);
        }
        cursor = c + 1;
        if (waited) {
          LARGE_INTEGER q1, f;
          QueryPerformanceCounter(&q1);
          QueryPerformanceFrequency(&f);
          g_rec_wait_us.fetch_add(uint64_t((q1.QuadPart - q0.QuadPart) * 1000000 / f.QuadPart), std::memory_order_relaxed);
        }
        return;
      }
    // Not published yet. A plugin-only draw is one the recorder has passed: it is well ahead (a frame's worth) and
    // still has not produced it.
    if (head >= cursor + kWindow * 256) { g_rec_plugin_extra.fetch_add(1, std::memory_order_relaxed); return; }
    if (!waited) { waited = true; g_rec_waits.fetch_add(1, std::memory_order_relaxed); QueryPerformanceCounter(&q0); }
    if (GetTickCount64() - t0 > 250) {
      const uint64_t n = g_rec_wait_timeouts.fetch_add(1, std::memory_order_relaxed);
      if (n < 8) {
        size_t qn;
        { std::lock_guard<std::mutex> lock(g_rec_mu); qn = g_rec_queue.size(); }
        REXLOG_INFO("[p3rec] WAIT TIMEOUT {}: plugin draw {:08X}/{:08X} not recorded; recorder head {}, cursor {}; {} batches queued",
                    n + 1, packet_addr, draw_initiator, head, cursor, qn);
      }
      return;
    }
    SwitchToThread();
  }
}
void RecorderSwapReport() { if (g_p3rec) RecorderReport(); }
void FrontEndCounts(uint64_t& draws, uint64_t& swaps) { draws = g_fe_draws_issued.load(); swaps = g_fe_swaps_issued.load(); }

void BridgeSwap() {
  g_br_ord.clear();
  std::lock_guard<std::mutex> lock(g_fe_mu);
  ++g_bridge_frames;
  if ((g_bridge_frames % 60) == 0)   // bridge values the front end never came for: expired, counted (bounded map)
    for (auto it = g_br_snaps.begin(); it != g_br_snaps.end();) {
      auto& q = it->second;
      while (!q.empty() && q.front()->frame + 3 < g_bridge_frames) { FeRelease(q.front()); q.pop_front(); ++g_br_expired; }
      it = q.empty() ? g_br_snaps.erase(it) : std::next(it);
    }
}   // the bridge's per-address execution ordinals restart with its frame

}  // namespace fable2::p2

extern "C" void fable2_p2_exit(int hook) { fable2::p2::Exit(hook); }
