// Sampling profiler for the guest threads, in-process.
//
// FABLE2_PROFILE=1 (or a comma-separated list of thread names; 1 means
// "GameThread,3D Engine") samples each named thread about a thousand times a
// second - suspend, read the context, unwind, resume - and every ten seconds
// logs where the samples landed: by module (generated guest code, this app,
// the runtime, the GPU plugin, ntdll = in a system call), the hottest leaf
// addresses, and the hottest GUEST functions (the first recompiled sub_ frame
// above the leaf), named through the codegen's own guest->host table, so no
// PDB is needed.
//
// Why in-process: the runtime raises first-chance guest access violations all
// the time, so anything that attaches as a debugger (cdb, procdump, and the
// timing-sensitive races with them) changes what it measures; and the
// recompiled functions have no symbols any external profiler could show.
#pragma once

namespace fable2 {

// Reads FABLE2_PROFILE; does nothing when it is unset.
void StartProfiler();
void StopProfiler();

}  // namespace fable2
