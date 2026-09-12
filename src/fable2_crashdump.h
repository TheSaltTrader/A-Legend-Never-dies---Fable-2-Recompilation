// A minidump for every host crash, so the next one leaves evidence.
//
// On 2026-09-11 the game died in play with nothing in the log (its last
// second is lost to the flush interval), no Windows error-reporting record and
// no dump, and the only evidence was the shape of the last two log lines. The
// runtime's own exception handler deals with GUEST memory faults and declines
// everything else; this is the top-level filter behind it, and it writes a
// minidump under crashdumps\ beside the executable before the process goes.
//
// `cdb -z <dump> -c "!analyze -v; q"` then names the faulting module, the
// thread and the stack - which is the whole difference between "it crashed
// when I pressed F9" and knowing why.

#pragma once

namespace fable2 {

// Installs the top-level unhandled-exception filter. Call once, as early as
// possible in startup; safe to call again (the second call is ignored).
void InstallCrashDumps();

}  // namespace fable2
