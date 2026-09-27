# Xenos->native coverage census — foundation (2026-09-22, claudecode-b8)

The systematic, game-AGNOSTIC basis for the full Xenos->native conversion. Its
denominator is the FIXED Xenos hardware command surface, generated from the SDK's
own enums, so no game can enlarge it and "what is not ported is COUNTED".

## Files
- `tools/native_gpu/gen_census.py` — extracts the denominator from the SDK headers
  (xenos.h Type3Opcode + format/primitive enums, register_table.inc, format/ucode.h).
  Re-run any time; it stays in sync with the SDK.
- `src/native_gpu_census_subjects.inc` — the generated denominator (1,439 subjects).
- `src/native_gpu_census.{h,cpp}` — the census: records subjects on two axes, reports.
- Instrumentation in `native_gpu_present.cpp`: texture-format + endian See/MarkPorted;
  per-swap MaybeReport; plus an autonomous reporter thread (below).

## Run it
`FABLE2_TUNE="ngpu_census=true;ngpu_census_report_secs=6"` — no game drive needed:
a detached reporter polls on its own clock, seeds the static PORTED axis, and writes
`ngpu_census.txt` next to the exe + `[ngpu-census]` log lines every N seconds.

## The denominator (1,439 subjects) and current coverage (VERIFIED 2026-09-22)
| kind | denom | PORTED (handler exists) |
|---|---|---|
| PM4 opcode | 47 | 0 |
| register | 1164 | 0 |
| shader ALU-vector | 30 | 0 |
| shader ALU-scalar | 50 | 0 |
| shader fetch | 9 | 0 |
| shader control-flow | 16 | 0 |
| texture format | 64 | 22 |
| vertex format | 16 | 12 |
| color RT format | 12 | 0 |
| depth RT format | 2 | 0 |
| endian | 4 | 4 |
| primitive type | 20 | 0 |
(registers exclude 2,280 shader-constant-memory regs, collapsed to 5 block subjects.)

The numbers are HONEST: PORTED counts native-layer handlers that demonstrably exist
(the format switches); the 0s are surfaces the PLUGIN handles today, not the native
layer. VERIFIED=0 everywhere (no oracle check run).

## RUNTIME-VERIFIED on the PM4 surface (2026-09-22, commit b20db4b)
RingParse IS exercised, so instrumenting it produced real two-axis data (one 48 s
Bowerstone run):
- PM4 seen=15/47, ported=10/47. Exercised + ported: DRAW_INDX 454x, SET_CONSTANT
  662x, IM_LOAD 528x, LOAD_ALU_CONSTANT 301x.
- **The unported queue, NAMED + COUNTED** (seen, ported=0 - the actual work list):
  SET_BIN_MASK_LO 1063x, WAIT_REG_MEM 37x, INVALIDATE_STATE 6x, EVENT_WRITE 4x,
  INDIRECT_BUFFER 3x.
- OFF-CENSUS detection fired: ids 0x0, 0xA, 0x7F (1-2x each) - boundary GARBAGE from
  RingParse desync, NOT enum gaps. So the denominator is complete for real opcodes,
  and the instrument separates "unexpected input" from "known but unported". This is
  the point-(c)/(d) machinery proven with data.

Other surfaces still read seen=0 because their dispatch sites (GetTexture, the resolve
RT-format decode, register apply, shader translation) are either not exercised in the
hybrid config or not yet instrumented - the next work.

## Four failure modes it is built against (from claudecode-76, all real past defects)
- (a) ported != verified — two axes, never one number.
- (b) sequences are on no enum — Kind::Sequence; reported UNMEASURED until it has handlers.
- (c) the counter only sees what reaches dispatch — See() at the RAW boundary, Refused()
  counts drops, every count a FLOOR.
- (d) the list of lists is an assumption — the report prints its provenance and flags any
  OFF-CENSUS subject (seen, not in the denominator) as the incompleteness signal.
Coverage != correctness: games stop being the census but remain the correctness ORACLE.

## Boundary (point d) — stated, because a census cannot enumerate its own blind spot
Subjects come from these SDK enums: Type3Opcode; Texture/Vertex/Color+DepthRT/Endian/
Primitive formats (xenos.h); Register (register_table.inc); shader ISA Alu-vector/
Alu-scalar/Fetch/Control-flow (ucode.h). What is NOT a separate subject, and where it
IS covered: blend/alpha modes = RB_BLENDCONTROL/RB_COLORCONTROL sub-fields (register
censused; the mode is a PARAMETER of it, point a); sampler state = SQ-tex/fetch-constant
sub-fields (registers); vertex-fetch = the 0x4800 fetch-constant block (registers); index
format = a draw payload field (IndexFormat kInt16/kInt32, NOT taken as a kind); query/
fence = the VIZ_QUERY/WAIT_REG_MEM/EVENT_WRITE PM4 opcodes; memexport = a shader feature
+ fetch constant (shader ISA + registers).

## DENOMINATOR CAVEAT — the method's premise has a MEASURED exception (2026-09-22)
The denominator is "subjects the taken enums NAME", NOT "all subjects". The claim that a
game can only use subjects on the finite list is FALSIFIED by data: real register writes
to the COHERENT block **0x2010-0x202D** (each ~4-5x/run) are NOT in register_table.inc,
and are distinct from the scattered <0x2000 RingParse desync noise. Undocumented registers,
a different addressing mode, or our own decode error - UNRESOLVED, and worth its own answer.
So off-census is not all garbage: a COHERENT off-census block is a finding, a scattered one
is likely noise. The census now prints this caveat in its own output (failure mode d, real).

## Validation without a human: replay-differential (the plugin is the oracle)
Handler porting splits into WRITE (no human) and VALIDATE (the oracle, not a human at a
controller). The plugin is the correctness oracle by design. Replay-differential: feed the
SAME recorded inputs (RingParse traces / dumped textures) to the plugin's handler and the
native handler, compare byte-for-byte - automatable, catches most porting defects before any
live drive. The live drive becomes a final integration check on a BATCH, not a per-handler
gate. (Per-surface work: extracting the plugin's per-handler reference output is the piece to
build; the texture .src/.rows dumps are the start for the texture path.)

## Next work (ordered; the census names the exact remaining subjects)
1. Fill SEEN: instrument the dispatch sites that actually run — PM4 opcodes at the
   bridge/RingParse (a FLOOR: the plugin owns the complete stream), registers at the
   native register-file apply, shader ops at translation, RT/primitive at the resolve/
   draw path. Then a game run lights up which subjects are exercised.
2. Fill PORTED with real native handlers, reusing the SDK's PURE logic (the proven
   pattern): the format GAPS via texture_conversion (CTX1->ConvertTexelCTX1ToR8G8,
   DXT3A->ConvertTexelDXT3AToDXT3, the _AS_16_16_16_16 aliases via GetBaseFormat, depth
   22/23); the WHOLE shader ISA via the SDK `DxbcShaderTranslator` (pipeline/shader/
   dxbc_translator.h) — device-free, replaces the external non-deterministic
   XenosRecomp+dxc path.
3. Fill VERIFIED: check each ported subject's output against the plugin oracle
   (ngpu_truth / byte-compare), per subject.
4. Kind::Sequence: enumerate the ordering subjects (clear-before-draw, resolve order,
   marker registers) and give them handlers, so ordering stops being unmeasured.
