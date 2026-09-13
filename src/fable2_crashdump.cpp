#include "fable2_crashdump.h"
#include "fable2_profiler.h"  // DescribeHostAddress: sub_ names for recompiled frames

#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <rex/filesystem.h>
#include <rex/logging.h>

namespace fable2 {
namespace {

std::atomic<bool> g_installed{false};
// Fixed at install time: a crash handler must not allocate or walk the
// filesystem while the heap may be the thing that broke.
wchar_t g_dump_dir[MAX_PATH] = {};

void LogStack(int skip) {
  void* frames[62];
  const USHORT n = CaptureStackBackTrace(static_cast<DWORD>(skip), 62, frames, nullptr);
  HANDLE proc = GetCurrentProcess();
  static std::atomic<bool> sym_ready{false};
  if (!sym_ready.exchange(true)) {
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(proc, nullptr, TRUE);
  }
  for (USHORT i = 0; i < n; ++i) {
    // Recompiled guest code is named sub_<guest address> through the
    // codegen's own table, so a crash in the game's code says which
    // function - the 09:03 crash on 2026-09-12 was 28 frames of
    // "fable2.exe+0x..." until this.
    REXLOG_CRITICAL("  #{:02} {}", i,
                    fable2::DescribeHostAddress(reinterpret_cast<uint64_t>(frames[i])));
  }
}

// If a C++ exception is in flight (terminate after a throw through a noexcept
// frame, or out of a thread function), say what it was.
void DescribeCurrentException() {
  const std::exception_ptr e = std::current_exception();
  if (!e) {
    REXLOG_CRITICAL("  no C++ exception in flight");
    return;
  }
  try {
    std::rethrow_exception(e);
  } catch (const std::exception& ex) {
    REXLOG_CRITICAL("  C++ exception in flight: {}", ex.what());
  } catch (...) {
    REXLOG_CRITICAL("  C++ exception in flight: (not a std::exception)");
  }
}

LONG WINAPI WriteDumpAndDie(EXCEPTION_POINTERS* exception);

// abort() - which is what std::terminate, a failed assert and a CRT invalid
// parameter all come to - ends the process with a fast-fail (0xC0000409) that
// bypasses the unhandled-exception filter: no dump, no log line, the exit code
// the only trace. The SIGABRT handler runs first, on the aborting thread, and
// is process-wide because every module here shares the one ucrtbase.
void OnAbort(int) {
  static std::atomic<bool> once{false};
  if (once.exchange(true))
    return;
  REXLOG_CRITICAL("ABORT: abort() called on thread {} - stack follows", GetCurrentThreadId());
  DescribeCurrentException();
  LogStack(1);
  WriteDumpAndDie(nullptr);
  rex::FlushLogging();
}

void OnTerminate() {
  REXLOG_CRITICAL("TERMINATE: std::terminate on thread {}", GetCurrentThreadId());
  DescribeCurrentException();
  LogStack(1);
  rex::FlushLogging();
  std::abort();
}

void OnInvalidParameter(const wchar_t* expr, const wchar_t* fn, const wchar_t* file,
                        unsigned line, uintptr_t) {
  char e[256] = "?", f[256] = "?", fl[256] = "?";
  if (expr) WideCharToMultiByte(CP_UTF8, 0, expr, -1, e, sizeof(e), nullptr, nullptr);
  if (fn) WideCharToMultiByte(CP_UTF8, 0, fn, -1, f, sizeof(f), nullptr, nullptr);
  if (file) WideCharToMultiByte(CP_UTF8, 0, file, -1, fl, sizeof(fl), nullptr, nullptr);
  REXLOG_CRITICAL("INVALID PARAMETER: '{}' in {} ({}:{}) on thread {}", e, f, fl, line,
                  GetCurrentThreadId());
  LogStack(1);
  rex::FlushLogging();
  std::abort();
}

void OnPureCall() {
  REXLOG_CRITICAL("PURE VIRTUAL CALL on thread {}", GetCurrentThreadId());
  LogStack(1);
  rex::FlushLogging();
  std::abort();
}

LONG WINAPI WriteDumpAndDie(EXCEPTION_POINTERS* exception) {
  // A debugger's leftover. cdb's data breakpoints (`ba w4`) stay armed in
  // the thread's debug registers after it detaches, `bc *` or not, and the
  // next write to that address raises a single-step exception with nobody
  // attached to take it. That killed two test sessions (2026-09-13) while
  // finding the projection builder. It is not a fault in the game: clear
  // the debug registers in the faulting context, say so once, and carry
  // on. A real single-step never reaches an unhandled-exception filter
  // with a debugger attached, so nothing legitimate is swallowed here.
  if (exception && exception->ExceptionRecord && exception->ContextRecord &&
      exception->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
    CONTEXT* ctx = exception->ContextRecord;
    ctx->Dr0 = ctx->Dr1 = ctx->Dr2 = ctx->Dr3 = 0;
    ctx->Dr6 = ctx->Dr7 = 0;
    ctx->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
    static std::atomic<int> seen{0};
    if (seen.fetch_add(1) < 4)
      REXLOG_WARN("Single-step exception at {} on thread {} with no debugger attached - a "
                  "leftover hardware breakpoint; debug registers cleared, continuing",
                  exception->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
    return EXCEPTION_CONTINUE_EXECUTION;
  }

  // One dump per process. A second fault while writing the first (possible,
  // since the heap may be corrupt) must not recurse into this.
  static std::atomic<bool> writing{false};
  if (writing.exchange(true))
    return EXCEPTION_EXECUTE_HANDLER;

  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t path[MAX_PATH];
  _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\fable2-%04d%02d%02d-%02d%02d%02d.dmp",
               g_dump_dir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

  HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = exception;
    info.ClientPointers = FALSE;
    // Enough to see every thread's stack and the memory those stacks point
    // at, without the full address space (guest memory alone is gigabytes).
    const auto type = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
        MiniDumpWithHandleData | MiniDumpWithUnloadedModules);
    const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                      exception ? &info : nullptr, nullptr, nullptr);
    CloseHandle(file);
    char narrow[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof(narrow), nullptr, nullptr);
    const DWORD code = exception && exception->ExceptionRecord
                           ? exception->ExceptionRecord->ExceptionCode
                           : 0;
    const void* at = exception && exception->ExceptionRecord
                         ? exception->ExceptionRecord->ExceptionAddress
                         : nullptr;
    if (ok) {
      REXLOG_CRITICAL("CRASH: exception {:#010x} at {} on thread {} - minidump written to {}",
                      code, at, GetCurrentThreadId(), narrow);
      // The faulting frames, named: recompiled functions as sub_<guest
      // address>, runtime frames by export. The dump has the same, but this
      // is readable without a debugger and survives a lost dump. The
      // dispatcher's own frames sit on top; the fault site is the first
      // frame that is not ntdll.
      REXLOG_CRITICAL("  fault at {}", fable2::DescribeHostAddress(reinterpret_cast<uint64_t>(at)));
      LogStack(0);
    } else {
      REXLOG_CRITICAL("CRASH: exception {:#010x} at {} on thread {} - minidump FAILED ({})",
                      code, at, GetCurrentThreadId(), GetLastError());
    }
  } else {
    REXLOG_CRITICAL("CRASH: could not create the minidump file ({})", GetLastError());
  }
  rex::FlushLogging();
  // Let the process die the normal way: Windows Error Reporting still gets
  // its record, and the return code says "crash" rather than "quit".
  return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

void InstallCrashDumps() {
  if (g_installed.exchange(true))
    return;
  const std::filesystem::path dir = rex::filesystem::GetExecutableFolder() / "crashdumps";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  wcsncpy_s(g_dump_dir, dir.wstring().c_str(), _TRUNCATE);
  SetUnhandledExceptionFilter(&WriteDumpAndDie);
  std::signal(SIGABRT, &OnAbort);
  std::set_terminate(&OnTerminate);  // this thread; other threads reach OnAbort
  _set_invalid_parameter_handler(&OnInvalidParameter);
  _set_purecall_handler(&OnPureCall);
  // No "Abort/Retry/Ignore" box from a failed assert in a WIN32 app: it would
  // hang a game nobody is watching. Report and fall through to abort instead.
  _set_error_mode(_OUT_TO_STDERR);
  _set_abort_behavior(0, _WRITE_ABORT_MSG);
  REXLOG_INFO("Crash dumps: a minidump goes to {} if the process faults or aborts",
              dir.string());
}

}  // namespace fable2
