#pragma once
#include <cstdint>

// =============================================================================
// Game-agnostic Xenos -> native coverage census.
//
// The DENOMINATOR is the finite Xenos hardware command surface, GENERATED from
// the SDK's own enums into native_gpu_census_subjects.inc (by
// tools/native_gpu/gen_census.py) - never hand-maintained, so it stays complete
// as the SDK evolves. A game is a sequence of these fixed subjects; when the
// surface is fully PORTED, any Xbox 360 title runs by construction (for
// COVERAGE - correctness is a separate axis, see below).
//
// This design answers four ways a coverage census reports 100% and is still
// wrong, each a defect already paid for in this project:
//
//   (a) PORTED IS NOT VERIFIED. The defect space is subject x PARAMETERS (a
//       register's value space, a shader op's modifiers/swizzle/predication, a
//       format's tiling/endian/mip/array). So every subject carries TWO axes -
//       `ported` (a native handler exists) and `verified` (exercised AND its
//       output checked against the plugin oracle, naming WHAT checked it). They
//       are never conflated into one coverage number.
//
//   (b) SEQUENCES ARE ON NO ENUM. The costliest bugs here live in ORDERING
//       (EDRAM aliasing, resolve-after-clear, a marker carrying its own regs).
//       No enum enumerates ordering, so Kind::Sequence is a first-class subject
//       kind; until it has handlers, Report() states IN WRITING that sequence
//       coverage is unmeasured. An unmeasured axis named is fine; implied is not.
//
//   (c) THE COUNTER ONLY SEES WHAT REACHES DISPATCH. A subject dropped by an
//       upstream decode/mask never arrives to be counted, so the count reads
//       clean and the gap is invisible (how `& 15` hid 55,860 draws). See() is
//       therefore called at the RAW boundary, value-independently, before any
//       decode; Refused() counts what the decoder threw away; every printed
//       count is a FLOOR.
//
//   (d) THE LIST OF LISTS IS ITSELF AN ASSUMPTION. A census cannot enumerate its
//       own blind spot. Report() prints its provenance (which enums were taken
//       and why that set is believed complete), and any subject See()n that is
//       NOT in the generated denominator is flagged off-census - the signal that
//       the enum set was incomplete.
//
// COVERAGE vs CORRECTNESS: games stop being the census (they reveal no new
// subjects), but they remain the correctness ORACLE (whether a handler is right,
// whether sequences work). "Fully ported => any game runs" is a COVERAGE claim
// and says nothing about correctness. Report() keeps the two numbers apart and
// every completion claim must say which it is about.
// =============================================================================

namespace fable2 {
namespace ngpu {
namespace census {

// Distinct kinds so Report() is explicit about which slice of the surface each
// number describes (point d - explicitness is the defense against a blind spot).
enum class Kind : uint8_t {
  Pm4Opcode = 0,      // Type3Opcode                         (xenos.h)
  Register,           // Register                            (registers.h)
  ShaderAluVector,    // AluVectorOpcode                     (ucode.h)
  ShaderAluScalar,    // AluScalarOpcode                     (ucode.h)
  ShaderFetch,        // FetchOpcode (vertex + texture)      (ucode.h)
  ShaderControlFlow,  // ControlFlowOpcode                   (ucode.h)
  TextureFormat,      // TextureFormat                       (xenos.h)
  VertexFormat,       // VertexFormat                        (xenos.h)
  ColorRtFormat,      // ColorRenderTargetFormat             (xenos.h)
  DepthRtFormat,      // DepthRenderTargetFormat             (xenos.h)
  Endian,             // Endian                              (xenos.h)
  PrimitiveType,      // PrimitiveType                       (xenos.h)
  Sequence,           // ordering/state-transition subjects  (NOT an enum; point b)
  XdkDeviceState,     // XDK-private device-shadow fields the port needs that NO register enumerates
                      // (polygon-offset pairs, target sizes, pointers): id = the shadow slot label
                      // 0x2010-0x2012 / 0x2211-0x2262. Denominator = the 85 non-register dwords of
                      // the 144-dword shadow block, registered programmatically (not from an SDK enum).
  kCount
};

const char* KindName(Kind k);

// ---- Denominator: populated once from native_gpu_census_subjects.inc --------
// Idempotent; safe to call from static init or first-use.
void EnsureLoaded();

// ---- Recording (call from the RAW boundary, before decode/mask - point c) ---

// Subject (kind,id) was ENCOUNTERED. `name` is optional context for an id not in
// the denominator (an off-census hit, point d). Counts arrivals, not decodes.
void See(Kind kind, uint32_t id, const char* name = nullptr);

// A raw payload the decoder REFUSED / dropped before dispatch. Counted so an
// upstream drop cannot read as clean coverage. `raw` is the undecoded value.
void Refused(Kind kind, uint32_t raw);

// ---- Tentative recording (speculative parser walks) --------------------------
// A parser that re-walks the same bytes speculatively (RingAdvance's resync
// probe tries up to 512 start offsets over the same 16 KB, and a walk that
// starts mid-packet decodes garbage headers) must not count every attempt:
// only the walk that STICKS is a reading of the stream. Between Begin and
// Commit/Discard, See()/Refused() are buffered on the calling thread; Commit
// counts them as real arrivals, Discard drops them and counts the drop per kind
// (reported as "discarded(probe)" so the instrument's own rejections are visible
// and a conservation check can be done: arrivals that vanish from off-census
// after this fix must reappear under discarded, not disappear).
void BeginTentative();
void CommitTentative();
void DiscardTentative();

// ---- Coverage axis 1: a native handler EXISTS for (kind,id). ----------------
void MarkPorted(Kind kind, uint32_t id);

// ---- Replacement ledger: WHAT emulated code the handler replaces, and whether
// that code is now DEAD on the native path. The user's method is in-place
// replacement of rexglue's Xenos-layer emulation; the rejected method ran a
// native renderer ON TOP of emulation that kept executing. The difference is
// not whether the new code is native but whether the OLD code still runs. So a
// subject whose emulated implementation still executes is DUPLICATED, not
// ported, and Report() counts it under its own heading - never as ported.
//   `replaces`      the rexglue file:function this handler stands in for.
//   `emulated_dead` true only when that code no longer executes on the native
//                   path (on the hybrid build, where the plugin still renders
//                   the main window, this is false for EVERY subject).
void MarkReplaces(Kind kind, uint32_t id, const char* replaces, bool emulated_dead);
// BACKEND TRANSPLANT (2026-09-26): the in-app backend (the plugin's own D3D12 backend, vendored) is live. From the
// next report every in-denominator subject of the kinds that backend handles - registers (state apply), texture /
// vertex / colour-RT / depth-RT formats, endians, primitive types - is PORTED, and emulated_dead when the plugin's
// copy no longer runs (gpu_offload_to_native: its IssueDraw/IssueCopy/IssueSwap and register hooks return early).
// Without offload the plugin still renders, so the same subjects are DUPLICATED. PM4 opcodes and shader ISA are
// not included: the plugin still parses PM4, and the shader census keeps its own ledger.
void NoteBackendTransplant(bool plugin_backend_skipped);

// ---- Coverage axis 2: the handler was exercised AND its output checked -------
// against the oracle. `oracle` names what checked it ("plugin frame diff",
// "byte cmp vs plugin untile", ...). Ported without verified is coverage only.
void MarkVerified(Kind kind, uint32_t id, const char* oracle);
// Every subject of `kind` that has a handler (ported) passed the same check.
// An oracle string beginning "PARTIAL:" is counted under PARTIAL, not VERIFIED:
// one STAGE of the handler was checked (say which), not the whole handler.
void MarkVerifiedAllPorted(Kind kind, const char* oracle);

// ---- Verdicts: a file per-frame spam cannot reach ---------------------------
// Twice tonight a log flood rotated the run's self-test verdicts out of
// existence (a per-draw warning, then two per-frame summary lines). Throttling
// each is a per-instance fix; the structural one is a separate result file
// that nothing hot writes to: ngpu_verdicts.txt next to the exe, appended with
// a timestamp, one line per verdict, and per-kind census lines only when they
// changed since the last append. A flood then costs the trace, never the result.
void Verdict(const char* line);

// ---- Reporting --------------------------------------------------------------
// Per kind: denominator size, seen, ported, verified; then the lists of
// seen-but-unported, off-census (seen, not in denominator), and refused. Leads
// with the provenance line and the explicit unmeasured-sequence note. All counts
// are FLOORS. Writes to the log and to ngpu_census.txt next to the exe.
void Report();

// Cvar-gated periodic report; call from the swap/frame boundary (fires whether
// or not anything drew - a report that needs a draw to have happened is blind in
// exactly the failing case).
void MaybeReport();

}  // namespace census
}  // namespace ngpu
}  // namespace fable2
