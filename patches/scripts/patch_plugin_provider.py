"""Plugin: on-demand resolve readback through the runtime's data providers.

readback "some": a resolve whose copy is deferred now sets its guest pages
no-access (EnablePhysicalMemoryAccessCallbacks with data providers). If the
game's CPU touches that memory before the copy has landed, the runtime calls
the plugin's provider on the faulting thread: it asks the GPU worker thread
(a thread-safe call queue drained between passes) to submit the pending
work, wait for exactly that submission, land the copy into guest memory and
release the pages, and waits for it - the global lock released meanwhile.
A copy that lands on its own (next frame's opening submission) releases the
pages too. So the CPU waits only for renders it actually reads, instead of
"full" draining the GPU on every resolve (33 fps at the lake).
"""
import os, sys
ROOT = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"

def edit(rel, pairs):
    p = os.path.join(ROOT, rel)
    d = open(p, "rb").read(); crlf = b"\r\n" in d
    t = d.decode("utf-8").replace("\r\n", "\n")
    for old, new in pairs:
        if t.count(old) == 0 and t.count(new) == 1:
            print("  already applied:", old[:50].strip()); continue
        if t.count(old) != 1:
            sys.exit("%s: %d matches for %r" % (rel, t.count(old), old[:70]))
        t = t.replace(old, new)
    open(p, "wb").write((t.replace("\n", "\r\n") if crlf else t).encode("utf-8"))
    print("patched", rel)

# ---- generic command processor: a thread-safe call queue -------------------------
edit("include/rex/graphics/command_processor.h", [
    ("  void CallInThread(std::function<void()> fn);\n",
     "  void CallInThread(std::function<void()> fn);\n"
     "  // From ANY thread: runs fn on the worker thread between passes (or at\n"
     "  // once when already on it). Wakes the worker if it is idling.\n"
     "  void CallInThreadSafe(std::function<void()> fn);\n"),
    ("  std::queue<std::function<void()>> pending_fns_;\n",
     "  std::queue<std::function<void()>> pending_fns_;\n"
     "  std::mutex safe_fns_lock_;\n"
     "  std::queue<std::function<void()>> safe_fns_;\n"
     "  std::atomic<bool> safe_fns_pending_{false};\n"
     "  void RunSafeFns();\n"),
])

edit("src/graphics/command_processor.cpp", [
    ("void CommandProcessor::CallInThread(std::function<void()> fn) {\n",
     "void CommandProcessor::CallInThreadSafe(std::function<void()> fn) {\n"
     "  if (system::XThread::IsInThread(worker_thread_.get())) {\n"
     "    fn();\n"
     "    return;\n"
     "  }\n"
     "  {\n"
     "    std::lock_guard<std::mutex> lock(safe_fns_lock_);\n"
     "    safe_fns_.push(std::move(fn));\n"
     "  }\n"
     "  safe_fns_pending_.store(true, std::memory_order_release);\n"
     "  write_ptr_index_event_->Set();\n"
     "}\n"
     "\n"
     "void CommandProcessor::RunSafeFns() {\n"
     "  if (!safe_fns_pending_.load(std::memory_order_acquire)) return;\n"
     "  while (true) {\n"
     "    std::function<void()> fn;\n"
     "    {\n"
     "      std::lock_guard<std::mutex> lock(safe_fns_lock_);\n"
     "      if (safe_fns_.empty()) {\n"
     "        safe_fns_pending_.store(false, std::memory_order_release);\n"
     "        return;\n"
     "      }\n"
     "      fn = std::move(safe_fns_.front());\n"
     "      safe_fns_.pop();\n"
     "    }\n"
     "    fn();\n"
     "  }\n"
     "}\n"
     "\n"
     "void CommandProcessor::CallInThread(std::function<void()> fn) {\n"),
    ("  while (worker_running_) {\n"
     "    while (!pending_fns_.empty()) {\n"
     "      auto fn = std::move(pending_fns_.front());\n"
     "      pending_fns_.pop();\n"
     "      fn();\n"
     "    }\n",
     "  while (worker_running_) {\n"
     "    while (!pending_fns_.empty()) {\n"
     "      auto fn = std::move(pending_fns_.front());\n"
     "      pending_fns_.pop();\n"
     "      fn();\n"
     "    }\n"
     "    RunSafeFns();\n"),
    ("        rex::thread::MaybeYield();\n"
     "        loop_count++;\n"
     "        write_ptr_index = write_ptr_index_.load();\n"
     "      } while (worker_running_ && pending_fns_.empty() &&\n"
     "               (write_ptr_index == 0xBAADF00D || read_ptr_index_ == write_ptr_index));\n"
     "      ReturnFromWait();\n"
     "      if (!worker_running_ || !pending_fns_.empty()) {\n"
     "        continue;\n"
     "      }\n",
     "        rex::thread::MaybeYield();\n"
     "        loop_count++;\n"
     "        write_ptr_index = write_ptr_index_.load();\n"
     "      } while (worker_running_ && pending_fns_.empty() &&\n"
     "               !safe_fns_pending_.load(std::memory_order_acquire) &&\n"
     "               (write_ptr_index == 0xBAADF00D || read_ptr_index_ == write_ptr_index));\n"
     "      ReturnFromWait();\n"
     "      if (!worker_running_ || !pending_fns_.empty() ||\n"
     "          safe_fns_pending_.load(std::memory_order_acquire)) {\n"
     "        continue;\n"
     "      }\n"),
])

# ---- the D3D12 command processor -------------------------------------------------
edit("include/rex/graphics/d3d12/command_processor.h", [
    ("  std::vector<std::pair<uint64_t, ID3D12Resource*>> readback_buffers_to_release_;\n"
     "  void DropPendingResolveReadbacks(uint64_t key, uint32_t index);\n",
     "  std::vector<std::pair<uint64_t, ID3D12Resource*>> readback_buffers_to_release_;\n"
     "  void DropPendingResolveReadbacks(uint64_t key, uint32_t index);\n"
     "  // [readback] On-demand: the runtime calls this on a guest thread that\n"
     "  // touched memory of a resolve whose copy is still pending; the copy is\n"
     "  // landed by the worker thread and waited for here.\n"
     "  static void ResolveDataProviderThunk(void* context,\n"
     "                                       std::unique_lock<std::recursive_mutex>& global_lock,\n"
     "                                       uint32_t physical_address, uint32_t length, bool is_write);\n"
     "  void ResolveDataProvider(std::unique_lock<std::recursive_mutex>& global_lock,\n"
     "                           uint32_t physical_address, uint32_t length, bool is_write);\n"
     "  // Worker thread: submits, waits for and lands every pending copy that\n"
     "  // overlaps the range, and releases the range's pages.\n"
     "  void LandPendingResolveReadbacks(uint32_t address, uint32_t length);\n"
     "  void* resolve_data_provider_handle_ = nullptr;\n"),
])

edit("src/graphics/d3d12/command_processor.cpp", [
    ("#include <rex/memory/utils.h>\n",
     "#include <rex/memory/utils.h>\n"
     "#include <rex/system/xthread.h>\n"
     "#include <condition_variable>\n"
     "#include <memory>\n"),
    # the counters, next to the fence-wait stats
    ("FenceWaitStat g_fence_waits[8];\n",
     "FenceWaitStat g_fence_waits[8];\n"
     "// [readback] On-demand waits by guest threads (any thread: atomics).\n"
     "std::atomic<uint32_t> g_provider_waits{0};\n"
     "std::atomic<uint64_t> g_provider_wait_us{0};\n"
     "std::atomic<uint32_t> g_provider_calls{0};\n"),
    ("  if (!line.empty()) REXLOG_INFO(\"[gpu] fence waits in {:.1f} s: {}\", secs, line);\n",
     "  {\n"
     "    const uint32_t calls = g_provider_calls.exchange(0);\n"
     "    const uint32_t waits = g_provider_waits.exchange(0);\n"
     "    const uint64_t us = g_provider_wait_us.exchange(0);\n"
     "    if (calls) {\n"
     "      char b[96];\n"
     "      std::snprintf(b, sizeof(b), \"%son-demand readback %u x %.1f ms (%u touches)\",\n"
     "                    line.empty() ? \"\" : \", \", waits, us / 1000.0, calls);\n"
     "      line += b;\n"
     "    }\n"
     "  }\n"
     "  if (!line.empty()) REXLOG_INFO(\"[gpu] fence waits in {:.1f} s: {}\", secs, line);\n"),
    # registration
    ("  shared_memory_ = std::make_unique<D3D12SharedMemory>(*this, *memory_);\n"
     "  if (!shared_memory_->Initialize()) {\n"
     "    REXGPU_ERROR(\"Failed to initialize shared memory\");\n"
     "    return false;\n"
     "  }\n",
     "  shared_memory_ = std::make_unique<D3D12SharedMemory>(*this, *memory_);\n"
     "  if (!shared_memory_->Initialize()) {\n"
     "    REXGPU_ERROR(\"Failed to initialize shared memory\");\n"
     "    return false;\n"
     "  }\n"
     "  // [readback] On-demand copies of resolves the CPU touches early.\n"
     "  if (!resolve_data_provider_handle_) {\n"
     "    resolve_data_provider_handle_ =\n"
     "        memory_->RegisterPhysicalMemoryDataProvider(ResolveDataProviderThunk, this);\n"
     "  }\n"),
    ("void D3D12CommandProcessor::ShutdownContext() {\n"
     "  AwaitAllQueueOperationsCompletion();\n",
     "void D3D12CommandProcessor::ShutdownContext() {\n"
     "  AwaitAllQueueOperationsCompletion();\n"
     "  // [readback] Land what is pending, release every watched page, unhook.\n"
     "  for (const PendingResolveReadback& p : pending_resolve_readbacks_) {\n"
     "    memory_->DisablePhysicalMemoryDataProviders(p.address, p.length);\n"
     "  }\n"
     "  pending_resolve_readbacks_.clear();\n"
     "  if (resolve_data_provider_handle_) {\n"
     "    memory_->UnregisterPhysicalMemoryDataProvider(resolve_data_provider_handle_);\n"
     "    resolve_data_provider_handle_ = nullptr;\n"
     "  }\n"),
    # deferral: watch the pages ("some")
    ("      pending_resolve_readbacks_.push_back(\n"
     "          {resolve_key, GetCurrentSubmission(), write_index, written_address, written_length});\n"
     "      shared_memory_->ProtectGpuRange(written_address, written_length);\n"
     "    }\n"
     "    rb.current_index = 1 - rb.current_index;\n"
     "    return true;\n"
     "  }\n",
     "      pending_resolve_readbacks_.push_back(\n"
     "          {resolve_key, GetCurrentSubmission(), write_index, written_address, written_length});\n"
     "      shared_memory_->ProtectGpuRange(written_address, written_length);\n"
     "      // The game's CPU may only touch this memory once the copy has landed:\n"
     "      // no access until then, the provider lands it on demand.\n"
     "      memory_->EnablePhysicalMemoryAccessCallbacks(written_address, written_length, false, true);\n"
     "    }\n"
     "    rb.current_index = 1 - rb.current_index;\n"
     "    return true;\n"
     "  }\n"),
    # deferral: the over-budget path (fast / full)
    ("      pending_resolve_readbacks_.push_back(\n"
     "          {resolve_key, GetCurrentSubmission(), write_index, written_address, written_length});\n"
     "      shared_memory_->ProtectGpuRange(written_address, written_length);\n"
     "      rb.current_index = 1 - rb.current_index;\n"
     "      return true;\n",
     "      pending_resolve_readbacks_.push_back(\n"
     "          {resolve_key, GetCurrentSubmission(), write_index, written_address, written_length});\n"
     "      shared_memory_->ProtectGpuRange(written_address, written_length);\n"
     "      memory_->EnablePhysicalMemoryAccessCallbacks(written_address, written_length, false, true);\n"
     "      rb.current_index = 1 - rb.current_index;\n"
     "      return true;\n"),
    # landing at the frame's opening submission: release the pages
    ("      shared_memory_->UnprotectGpuRange(p.address, p.length);  // the copy lands now\n"
     "      auto it = readback_buffers_.find(p.key);\n",
     "      shared_memory_->UnprotectGpuRange(p.address, p.length);  // the copy lands now\n"
     "      memory_->DisablePhysicalMemoryDataProviders(p.address, p.length);\n"
     "      auto it = readback_buffers_.find(p.key);\n"),
    # drop: release the pages (the data stays as the CPU had it)
    ("    } else if (shared_memory_) {\n"
     "      shared_memory_->UnprotectGpuRange(p.address, p.length);\n"
     "    }\n",
     "    } else if (shared_memory_) {\n"
     "      shared_memory_->UnprotectGpuRange(p.address, p.length);\n"
     "      memory_->DisablePhysicalMemoryDataProviders(p.address, p.length);\n"
     "    }\n"),
    # the provider + the landing routine
    ("void D3D12CommandProcessor::DropPendingResolveReadbacks(uint64_t key, uint32_t index) {\n",
     "void D3D12CommandProcessor::ResolveDataProviderThunk(\n"
     "    void* context, std::unique_lock<std::recursive_mutex>& global_lock, uint32_t physical_address,\n"
     "    uint32_t length, bool is_write) {\n"
     "  reinterpret_cast<D3D12CommandProcessor*>(context)->ResolveDataProvider(\n"
     "      global_lock, physical_address, length, is_write);\n"
     "}\n"
     "\n"
     "void D3D12CommandProcessor::ResolveDataProvider(std::unique_lock<std::recursive_mutex>& global_lock,\n"
     "                                                uint32_t physical_address, uint32_t length,\n"
     "                                                bool is_write) {\n"
     "  g_provider_calls.fetch_add(1, std::memory_order_relaxed);\n"
     "  if (system::XThread::IsInThread(worker_thread_.get())) {\n"
     "    LandPendingResolveReadbacks(physical_address, length);\n"
     "    return;\n"
     "  }\n"
     "  // Another thread touched the memory: have the worker land the copies,\n"
     "  // and wait for it with the global lock released (the worker needs it).\n"
     "  struct Request {\n"
     "    std::mutex m;\n"
     "    std::condition_variable cv;\n"
     "    bool done = false;\n"
     "  };\n"
     "  auto request = std::make_shared<Request>();\n"
     "  const auto t0 = std::chrono::steady_clock::now();\n"
     "  CallInThreadSafe([this, request, physical_address, length]() {\n"
     "    LandPendingResolveReadbacks(physical_address, length);\n"
     "    {\n"
     "      std::lock_guard<std::mutex> lock(request->m);\n"
     "      request->done = true;\n"
     "    }\n"
     "    request->cv.notify_all();\n"
     "  });\n"
     "  const bool was_locked = global_lock.owns_lock();\n"
     "  if (was_locked) global_lock.unlock();\n"
     "  bool timed_out = false;\n"
     "  {\n"
     "    std::unique_lock<std::mutex> lock(request->m);\n"
     "    timed_out = !request->cv.wait_for(lock, std::chrono::seconds(3), [&] { return request->done; });\n"
     "  }\n"
     "  if (was_locked) global_lock.lock();\n"
     "  const uint64_t us = uint64_t(\n"
     "      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)\n"
     "          .count());\n"
     "  g_provider_waits.fetch_add(1, std::memory_order_relaxed);\n"
     "  g_provider_wait_us.fetch_add(us, std::memory_order_relaxed);\n"
     "  if (timed_out) {\n"
     "    static std::atomic<int> logged{0};\n"
     "    if (logged.fetch_add(1) < 5)\n"
     "      REXGPU_WARN(\"On-demand readback: the GPU worker did not land 0x{:08X}+{} within 3 s\",\n"
     "                  physical_address, length);\n"
     "  }\n"
     "}\n"
     "\n"
     "void D3D12CommandProcessor::LandPendingResolveReadbacks(uint32_t address, uint32_t length) {\n"
     "  FenceReasonScope fence_reason(fence_reason_, \"on-demand readback\");\n"
     "  const uint64_t end = uint64_t(address) + length;\n"
     "  uint64_t await = 0;\n"
     "  bool need_submit = false;\n"
     "  for (const PendingResolveReadback& p : pending_resolve_readbacks_) {\n"
     "    if (p.address >= end || uint64_t(p.address) + p.length <= address) continue;\n"
     "    await = std::max(await, p.submission);\n"
     "    if (p.submission >= GetCurrentSubmission()) need_submit = true;\n"
     "  }\n"
     "  if (await) {\n"
     "    if (need_submit && submission_open_) {\n"
     "      EndSubmission(false);  // the copy is queued in the open submission\n"
     "    }\n"
     "    await = std::min(await, submission_current_ - 1);\n"
     "    CheckSubmissionFence(await);\n"
     "    const uint64_t completed = GetCompletedSubmission();\n"
     "    size_t kept = 0;\n"
     "    for (size_t i = 0; i < pending_resolve_readbacks_.size(); ++i) {\n"
     "      const PendingResolveReadback p = pending_resolve_readbacks_[i];\n"
     "      const bool overlaps = !(p.address >= end || uint64_t(p.address) + p.length <= address);\n"
     "      if (!overlaps || p.submission > completed) {\n"
     "        pending_resolve_readbacks_[kept++] = p;\n"
     "        continue;\n"
     "      }\n"
     "      shared_memory_->UnprotectGpuRange(p.address, p.length);\n"
     "      auto it = readback_buffers_.find(p.key);\n"
     "      if (it != readback_buffers_.end()) {\n"
     "        ReadbackBuffer& rb = it->second;\n"
     "        if (rb.buffers[p.index] && rb.mapped_data[p.index] && p.length <= rb.sizes[p.index]) {\n"
     "          if (uint8_t* destination = memory_->TranslatePhysical(p.address)) {\n"
     "            std::memcpy(destination, static_cast<const uint8_t*>(rb.mapped_data[p.index]),\n"
     "                        p.length);\n"
     "          }\n"
     "        }\n"
     "      }\n"
     "      memory_->DisablePhysicalMemoryDataProviders(p.address, p.length);\n"
     "    }\n"
     "    pending_resolve_readbacks_.resize(kept);\n"
     "  }\n"
     "  // Whatever is left watched in the range (a dropped copy, a race) is\n"
     "  // released: the faulting thread must be able to proceed.\n"
     "  memory_->DisablePhysicalMemoryDataProviders(address, length);\n"
     "}\n"
     "\n"
     "void D3D12CommandProcessor::DropPendingResolveReadbacks(uint64_t key, uint32_t index) {\n"),
])
print("done")
