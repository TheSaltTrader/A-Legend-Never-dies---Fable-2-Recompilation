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
    REXLOG_INFO("FP TRAP ({}): {} distinct invalid-operation sites, most frequent first:",
                label, ordered.size());
    for (size_t i = 0; i < ordered.size() && i < 24; ++i) {
      // The delta is the point: a site that fires steadily is background noise,
      // one that starts firing now is a lead.
      uint64_t total = ordered[i].second;
      uint64_t& seen = last_seen_[ordered[i].first];
      uint64_t delta = total - seen;
      seen = total;
      auto op = opcodes_.find(ordered[i].first);
      REXLOG_INFO("   {:>10} x (+{:<9}) [{}] {}", total, delta,
                  op == opcodes_.end() ? "??" : op->second.c_str(),
                  Describe(ordered[i].first));
    }
  }

 private:
  // Symbol name for a host address. The recompiled guest functions are emitted
  // as sub_<guest address>, so the name IS the guest address - which is the
  // whole point of symbolising rather than logging a raw pointer.
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
    if (info->ExceptionRecord->ExceptionCode != STATUS_FLOAT_INVALID_OPERATION) {
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
        REXLOG_WARN("FP TRAP: invalid operation at {}", Describe(at));
      }
    }
    // Re-mask INVALID for this thread and clear the sticky flag, so the game
    // carries on and later faults are still catchable elsewhere. Without this
    // the same instruction faults forever and nothing progresses.
    info->ContextRecord->MxCsr |= (1u << 7);
    info->ContextRecord->MxCsr &= ~(1u << 0);
    return EXCEPTION_CONTINUE_EXECUTION;
  }

  static constexpr size_t kMaxSites = 256;
  static inline bool enabled_ = false;
  static inline std::mutex mutex_;
  static inline std::unordered_map<uintptr_t, uint64_t> sites_;
  static inline std::unordered_map<uintptr_t, uint64_t> last_seen_;
  static inline std::unordered_map<uintptr_t, std::string> opcodes_;
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
