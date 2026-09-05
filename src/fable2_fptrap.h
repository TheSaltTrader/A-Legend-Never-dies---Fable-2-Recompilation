// Catching the invalid floating-point operation that produces the NaN.
//
// WHAT THIS IS FOR
//
// The draw census established that the vertex shader constants reaching the GPU
// contain NaN, that the NaN grows over time, and - decisively - that only THREE
// canonical quiet-NaN bit patterns appear across millions of values
// (0x7FC00000, 0xFFC00000, 0x7FE00000). Canonical QNaNs are what an INVALID
// OPERATION produces: 0/0, Inf-Inf, sqrt of a negative, 0*Inf. Not a fill
// pattern, not corruption - arithmetic.
//
// Bone matrices for skinned meshes travel through those constants, which is why
// characters never draw while static geometry is perfect.
//
// A computed NaN can be caught where it happens. x86 raises #I on an invalid
// operation whenever MXCSR's IM bit is clear, and the SDK seeds MXCSR for every
// guest thread through FPSCRPlatform::InitHostExceptions. Setting
// rex::platform::g_trap_fp_invalid leaves INVALID unmasked, so the offending
// instruction faults instead of quietly handing a NaN to something three
// subsystems away.
//
// WHY IT REPORTS AND CONTINUES
//
// Guest code legitimately performs invalid operations sometimes and discards the
// result (fsel after a divide is a common shape). So a single fault proves
// nothing on its own. This records the first N DISTINCT faulting addresses with
// how often each fires, masks the exception again for that thread, and lets the
// game run. The address that fires in step with the NaN count is the one worth
// disassembling.
//
// Enabled with FABLE2_TRAP_FP=1 rather than a cvar, because it has to be set
// before any guest thread seeds its MXCSR.

#pragma once

#ifdef _WIN32

#include <windows.h>

#include <dbghelp.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <thread>

#include <rex/logging.h>
#include <rex/platform/fpscr.h>

namespace fable2 {

class FPTrap {
 public:
  // Must run before any guest thread starts - MXCSR is seeded per thread.
  static void InstallIfRequested() {
    const char* on = std::getenv("FABLE2_TRAP_FP");
    if (!on || !*on || *on == '0') {
      return;
    }
    // The SDK reads this itself, in the DLL, when each guest thread seeds
    // MXCSR - so set it in the environment rather than through a global that
    // would not be the same object over there.
    _putenv_s("REX_TRAP_FP_INVALID", "1");
    // The SDK seeds guest MXCSR with INVALID unmasked; DIVIDE-BY-ZERO is armed
    // from inside the handler instead (see the mask flip there), because
    // InitHostExceptions lives in the SDK and this needs no rebuild of it.
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    AddVectoredExceptionHandler(1, &Handler);
    enabled_ = true;
    // A reporter on a timer, because the interesting signal is a RATE: which
    // site starts firing as NaN begins appearing in the vertex constants. A
    // one-shot line per site cannot show that, and a run that is killed never
    // reaches OnShutdown at all.
    std::thread([] {
      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(20));
        Report("periodic");
      }
    }).detach();
    REXLOG_INFO(
        "FP TRAP: invalid floating-point operations will fault and be reported. "
        "Guest code does this legitimately sometimes, so read the counts, not "
        "the first hit.");
  }

  // Called on the way out so the findings survive a run that ends badly.
  static void Report(const char* label = "final") {
    if (!enabled_) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (sites_.empty()) {
      REXLOG_INFO("FP TRAP ({}): no invalid floating-point operation trapped yet.", label);
      return;
    }
    std::vector<std::pair<uintptr_t, uint64_t>> ordered(sites_.begin(), sites_.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    size_t creators = 0, div_zeros = 0;
    for (const auto& kv : verdicts_) {
      creators += kv.second == Verdict::kCreator ? 1 : 0;
    }
    for (const auto& kv : div_zero_) {
      div_zeros += kv.second ? 1 : 0;
    }
    REXLOG_INFO("FP TRAP ({}): {} sites | {} CREATED a NaN from non-NaN inputs | {} DIVIDED BY "
                "ZERO (the source of the Inf those NaNs are made from).",
                label, ordered.size(), creators, div_zeros);
    for (size_t i = 0; i < ordered.size() && i < 24; ++i) {
      // The delta is the point: a site that fires steadily is background noise,
      // one that starts firing now is a lead.
      uint64_t total = ordered[i].second;
      uint64_t& seen = last_seen_[ordered[i].first];
      uint64_t delta = total - seen;
      seen = total;
      auto op = opcodes_.find(ordered[i].first);
      auto vd = verdicts_.find(ordered[i].first);
      auto dt = details_.find(ordered[i].first);
      const Verdict v = vd == verdicts_.end() ? Verdict::kUnknown : vd->second;
      auto dz = div_zero_.find(ordered[i].first);
      const bool was_div_zero = dz != div_zero_.end() && dz->second;
      REXLOG_INFO("   {:>10} x (+{:<9}) {:9} [{}] {}  {}", total, delta,
                  was_div_zero              ? "DIV0>INF"
                  : v == Verdict::kCreator  ? "CREATOR"
                  : v == Verdict::kConsumer ? "consumer"
                                            : "?",
                  op == opcodes_.end() ? "??" : op->second.c_str(),
                  Describe(ordered[i].first),
                  dt == details_.end() ? "" : dt->second.c_str());
    }
  }

 private:
  // Symbol name for a host address. The recompiled guest functions are emitted
  // as sub_<guest address>, so the name IS the guest address - which is the
  // whole point of symbolising rather than logging a raw pointer.
  // How a faulting site is classified once its operands have been read.
  enum class Verdict { kUnknown, kConsumer, kCreator };

  struct Operands {
    Verdict verdict = Verdict::kUnknown;
    // Human-readable rendering of the inputs, for the report.
    std::string detail;
  };

  static bool LanesHaveNaN(const M128A& v, bool scalar_only) {
    float f[4];
    std::memcpy(f, &v, sizeof(f));
    const int lanes = scalar_only ? 1 : 4;
    for (int i = 0; i < lanes; ++i) {
      if (std::isnan(f[i])) {
        return true;
      }
    }
    return false;
  }

  static std::string RenderLanes(const M128A& v, bool scalar_only) {
    float f[4];
    std::memcpy(f, &v, sizeof(f));
    std::string out = "(";
    const int lanes = scalar_only ? 1 : 4;
    for (int i = 0; i < lanes; ++i) {
      uint32_t bits;
      std::memcpy(&bits, &f[i], sizeof(bits));
      out += fmt::format("{}{:g}/0x{:08X}", i ? " " : "", f[i], bits);
    }
    return out + ")";
  }

  // Minimal decode of the two-operand SSE forms this trap actually sees:
  //   [REX] [66|F2|F3] 0F <op> <modrm> [disp]
  // Enough to name the two inputs and read them. Anything with a SIB byte is
  // reported as unknown rather than guessed at - a wrong operand would be worse
  // than no operand.
  static Operands ClassifyOperands(EXCEPTION_POINTERS* info, uintptr_t at) {
    Operands result;
    unsigned char b[16] = {};
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(at), b, sizeof(b),
                           &got) ||
        got < 4) {
      return result;
    }

    size_t i = 0;
    unsigned char rex = 0;
    bool scalar_only = false;
    // Prefixes. Order matters: the mandatory 66/F2/F3 prefix precedes REX.
    while (i < got) {
      if (b[i] == 0x66) {
        ++i;
      } else if (b[i] == 0xF3 || b[i] == 0xF2) {
        scalar_only = true;  // ss / sd forms touch lane 0 only
        ++i;
      } else if (b[i] >= 0x40 && b[i] <= 0x4F) {
        rex = b[i];
        ++i;
      } else {
        break;
      }
    }
    if (i + 2 >= got || b[i] != 0x0F) {
      return result;
    }
    const unsigned char op = b[i + 1];
    const unsigned char modrm = b[i + 2];

    // Conversions are host emulation artefacts, not guest arithmetic: simde
    // implements the per-lane vector shift vslw through floats, and cvttps2dq
    // of 2^31 raises a spurious #I while still producing the right answer.
    if (op == 0x5B || op == 0x2C || op == 0x2D) {
      result.detail = "host emulation (cvt*), not guest arithmetic";
      result.verdict = Verdict::kConsumer;
      return result;
    }
    // Only these can manufacture a NaN out of non-NaN inputs.
    // sqrtps, addps, mulps, subps, divps. dpps is deliberately absent: it is a
    // three-byte opcode (0F 3A 40) this decoder does not reach, and claiming it
    // here would just be a dead branch with a misleading name.
    const bool can_create =
        (op == 0x51 || op == 0x58 || op == 0x59 || op == 0x5C || op == 0x5E);
    const bool is_compare_or_minmax = (op == 0xC2 || op == 0x5D || op == 0x5F);
    if (!can_create && !is_compare_or_minmax) {
      return result;
    }

    const int mod = (modrm >> 6) & 3;
    const int reg = (((rex >> 2) & 1) << 3) | ((modrm >> 3) & 7);
    const int rm = ((rex & 1) << 3) | (modrm & 7);
    const M128A* xmm = info->ContextRecord->FltSave.XmmRegisters;

    // src1 is always the reg operand.
    const bool a_nan = LanesHaveNaN(xmm[reg], scalar_only);
    std::string a = fmt::format("xmm{}{}", reg, RenderLanes(xmm[reg], scalar_only));

    bool b_known = false;
    bool b_nan = false;
    std::string b_txt;
    if (mod == 3) {
      b_known = true;
      b_nan = LanesHaveNaN(xmm[rm], scalar_only);
      b_txt = fmt::format("xmm{}{}", rm, RenderLanes(xmm[rm], scalar_only));
    } else if ((modrm & 7) != 4 && (modrm & 7) != 5) {
      // [reg], [reg+disp8], [reg+disp32] with no SIB. Read it through the same
      // guarded path as the code bytes - the address comes from a register and
      // may not be mapped.
      static const int kGprIndex[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
      (void)kGprIndex;
      const DWORD64* gpr = &info->ContextRecord->Rax;
      // CONTEXT lays the integer registers out as Rax,Rcx,Rdx,Rbx,Rsp,Rbp,Rsi,Rdi,R8..R15
      static const int kOrder[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
      const int base_index = kOrder[rm];
      DWORD64 base = gpr[base_index];
      int32_t disp = 0;
      size_t disp_at = i + 3;
      if (mod == 1 && disp_at < got) {
        disp = int8_t(b[disp_at]);
      } else if (mod == 2 && disp_at + 3 < got) {
        std::memcpy(&disp, &b[disp_at], 4);
      }
      M128A mem = {};
      SIZE_T mgot = 0;
      if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(base + disp), &mem,
                            sizeof(mem), &mgot) &&
          mgot == sizeof(mem)) {
        b_known = true;
        b_nan = LanesHaveNaN(mem, scalar_only);
        b_txt = "mem" + RenderLanes(mem, scalar_only);
      }
    }

    if (!b_known) {
      result.detail = a + " + <operand not decoded>";
      return result;
    }
    result.detail = a + "  " + b_txt;
    if (a_nan || b_nan) {
      result.verdict = Verdict::kConsumer;
    } else if (can_create) {
      result.verdict = Verdict::kCreator;
    } else {
      // A compare/min/max that faulted with no NaN input means a SIGNALLING
      // NaN was involved, which is still information worth seeing.
      result.verdict = Verdict::kCreator;
    }
    return result;
  }

  // The guest call stack, unwound from the FAULTING context.
  //
  // RtlCaptureStackBackTrace walks the caller's own stack, which inside a
  // vectored handler is the exception dispatch stack - it reaches the faulting
  // function and stops. Unwinding the context the exception carries is what
  // actually crosses into the guest callers.
  static std::string Backtrace(EXCEPTION_POINTERS* info) {
    CONTEXT ctx = *info->ContextRecord;
    std::string out;
    for (int depth = 0; depth < 14; ++depth) {
      const DWORD64 pc = ctx.Rip;
      if (!pc) {
        break;
      }
      out += fmt::format("#{} {}", depth, Describe(uintptr_t(pc)));
      out += "|";

      DWORD64 image_base = 0;
      PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(pc, &image_base, nullptr);
      if (!fn) {
        break;  // a leaf or an unknown module - the chain ends here
      }
      PVOID handler_data = nullptr;
      DWORD64 establisher = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, pc, fn, &ctx, &handler_data, &establisher,
                       nullptr);
      if (!ctx.Rip) {
        break;
      }
    }
    return out;
  }

  // The log takes one line at a time, so the packed trace is split for output.
  static std::vector<std::string> SplitFrames(const std::string& packed) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < packed.size()) {
      const size_t end = packed.find('|', start);
      if (end == std::string::npos) {
        break;
      }
      if (end > start) {
        out.emplace_back(packed.substr(start, end - start));
      }
      start = end + 1;
    }
    return out;
  }

  // First bytes at the faulting instruction, so the opcode can be identified.
  // Read defensively: this runs inside an exception handler.
  static std::string ReadOpcode(uintptr_t address) {
    unsigned char bytes[10] = {};
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), bytes,
                           sizeof(bytes), &got) ||
        got == 0) {
      return "??";
    }
    std::string out;
    for (SIZE_T i = 0; i < got; ++i) {
      out += fmt::format("{:02X} ", bytes[i]);
    }
    return out;
  }

  static std::string Describe(uintptr_t address) {
    char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    DWORD64 displacement = 0;
    HMODULE module = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(address), &module);
    // The RVA is what a disassembler needs, and unlike the symbol it cannot be
    // wrong: DbgHelp will happily return the nearest preceding export (an
    // "__imp__" thunk) for an address inside a function it has no symbol for.
    uintptr_t rva = module ? address - uintptr_t(module) : 0;
    if (SymFromAddr(GetCurrentProcess(), DWORD64(address), &displacement, symbol)) {
      return fmt::format("{}+0x{:X}  (rva 0x{:X})", symbol->Name, displacement, rva);
    }
    return fmt::format("rva 0x{:X} (no symbol, host 0x{:016X})", rva, address);
  }

  static LONG CALLBACK Handler(EXCEPTION_POINTERS* info) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    const bool div_zero = code == STATUS_FLOAT_DIVIDE_BY_ZERO;
    if (code != STATUS_FLOAT_INVALID_OPERATION && !div_zero) {
      return EXCEPTION_CONTINUE_SEARCH;
    }
    uintptr_t at = uintptr_t(info->ExceptionRecord->ExceptionAddress);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = sites_.find(at);
      if (it != sites_.end()) {
        ++it->second;
      } else if (sites_.size() < kMaxSites) {
        sites_.emplace(at, 1);
        // The opcode matters more than the address. The busiest site in the
        // first run was cvttps2dq - simde emulating a per-lane vector shift
        // through floats, which raises a SPURIOUS #I and still computes the
        // right answer. Without the bytes there is no way to tell that host
        // emulation artefact apart from guest arithmetic genuinely going bad.
        opcodes_.emplace(at, ReadOpcode(at));
        // Classify while the registers still hold the inputs - after the
        // handler returns, the instruction runs and the evidence is gone.
        Operands ops = ClassifyOperands(info, at);
        verdicts_.emplace(at, ops.verdict);
        details_.emplace(at, ops.detail);
        div_zero_.emplace(at, div_zero);
        REXLOG_WARN("FP TRAP: {} at {} | {}",
                    div_zero                            ? "DIVIDED BY ZERO (makes an Inf)"
                    : ops.verdict == Verdict::kCreator  ? "CREATED a NaN"
                    : ops.verdict == Verdict::kConsumer ? "touched an existing NaN"
                                                        : "invalid operation",
                    Describe(at), ops.detail);
        // Only the sites that MAKE the poison are worth a stack walk. Consumers
        // are downstream by definition and there are far more of them.
        if (div_zero || ops.verdict == Verdict::kCreator) {
          for (const auto& frame : SplitFrames(Backtrace(info))) {
            REXLOG_WARN("   {}", frame);
          }
        }
      }
    }
    // Mask whichever exception just fired so the instruction can retry and the
    // game carries on - without this the same instruction faults forever.
    // Then ARM THE OTHER ONE. Both cannot be live at once (the first would loop
    // before the second ever fired), so alternating samples INVALID (where a
    // NaN is made) and DIVIDE-BY-ZERO (where the Inf that feeds it is made)
    // across the run. Bit 7 is IM, bit 9 is ZM; bits 0 and 2 are their sticky
    // flags, which must be cleared or the retry faults on the stale flag.
    if (div_zero) {
      info->ContextRecord->MxCsr |= (1u << 9);
      info->ContextRecord->MxCsr &= ~(1u << 7);
      info->ContextRecord->MxCsr &= ~(1u << 2);
    } else {
      info->ContextRecord->MxCsr |= (1u << 7);
      info->ContextRecord->MxCsr &= ~(1u << 9);
      info->ContextRecord->MxCsr &= ~(1u << 0);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
  }

  static constexpr size_t kMaxSites = 256;
  static inline bool enabled_ = false;
  static inline std::mutex mutex_;
  static inline std::unordered_map<uintptr_t, uint64_t> sites_;
  static inline std::unordered_map<uintptr_t, uint64_t> last_seen_;
  static inline std::unordered_map<uintptr_t, std::string> opcodes_;
  static inline std::unordered_map<uintptr_t, Verdict> verdicts_;
  static inline std::unordered_map<uintptr_t, std::string> details_;
  static inline std::unordered_map<uintptr_t, bool> div_zero_;
};

}  // namespace fable2

#else   // !_WIN32
namespace fable2 {
class FPTrap {
 public:
  static void InstallIfRequested() {}
  static void Report() {}
};
}  // namespace fable2
#endif  // _WIN32
