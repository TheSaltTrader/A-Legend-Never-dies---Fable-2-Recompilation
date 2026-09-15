# s94: cost accounting on the GPU command thread's fence line, no behaviour change.
# (1) CopyToGuestMemory: how long the readback landings take in total and how much of
#     that is waiting for the global critical region (the allocation check) -
#     "landings N x T ms (lock L ms)". The lake profile showed 12% of the thread
#     blocked under CopyToGuestMemory / LandCompletedResolveReadback.
# (2) EndSubmission: count and total time - "submissions N x T ms" (29% of the
#     thread at the lake with a boundary after every resolve).
import io

CP = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
s = io.open(CP, "r", encoding="utf-8", newline="").read()


def rep(old, new):
    global s
    assert s.count(old) == 1, old[:70]
    s = s.replace(old, new)


rep("static std::atomic<uint32_t> g_readbacks_valid_no_wait{0};\n",
    "static std::atomic<uint32_t> g_readbacks_valid_no_wait{0};\n"
    "// [cost] Readback landings (CopyToGuestMemory) and submissions per window.\n"
    "static std::atomic<uint32_t> g_landing_count{0};\n"
    "static std::atomic<uint64_t> g_landing_us{0};\n"
    "static std::atomic<uint64_t> g_landing_lock_us{0};\n"
    "static std::atomic<uint32_t> g_submission_count{0};\n"
    "static std::atomic<uint64_t> g_submission_us{0};\n")

rep('''bool D3D12CommandProcessor::CopyToGuestMemory(uint32_t address, const void* source,
                                              uint32_t length) {
  if (!length || !source) return false;
  uint8_t* destination = memory_->TranslatePhysical(address);
  if (!destination) return false;
''', '''bool D3D12CommandProcessor::CopyToGuestMemory(uint32_t address, const void* source,
                                              uint32_t length) {
  if (!length || !source) return false;
  uint8_t* destination = memory_->TranslatePhysical(address);
  if (!destination) return false;
  const auto landing_t0 = std::chrono::steady_clock::now();
  struct LandingTimer {
    std::chrono::steady_clock::time_point t0;
    ~LandingTimer() {
      g_landing_count.fetch_add(1, std::memory_order_relaxed);
      g_landing_us.fetch_add(
          uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - t0)
                       .count()),
          std::memory_order_relaxed);
    }
  } landing_timer{landing_t0};
''')
rep('''  {
    auto global_lock = rex::thread::global_critical_region::AcquireDirect();
    memory::BaseHeap* heap = memory_->physical_heap();
    const uint64_t end = uint64_t(address) + length;
''', '''  {
    auto global_lock = rex::thread::global_critical_region::AcquireDirect();
    g_landing_lock_us.fetch_add(
        uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::steady_clock::now() - landing_t0)
                     .count()),
        std::memory_order_relaxed);
    memory::BaseHeap* heap = memory_->physical_heap();
    const uint64_t end = uint64_t(address) + length;
''')

rep('''bool D3D12CommandProcessor::EndSubmission(bool is_swap) {
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
''', '''bool D3D12CommandProcessor::EndSubmission(bool is_swap) {
  const ui::d3d12::D3D12Provider& provider = GetD3D12Provider();
  struct SubmissionTimer {
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    ~SubmissionTimer() {
      g_submission_count.fetch_add(1, std::memory_order_relaxed);
      g_submission_us.fetch_add(
          uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - t0)
                       .count()),
          std::memory_order_relaxed);
    }
  } submission_timer;
''')

rep('''      const uint32_t valid = g_readbacks_valid_no_wait.exchange(0);
      if (awaited || open || valid) {
''', '''      const uint32_t valid = g_readbacks_valid_no_wait.exchange(0);
      {
        const uint32_t ln = g_landing_count.exchange(0);
        const uint64_t lus = g_landing_us.exchange(0);
        const uint64_t llk = g_landing_lock_us.exchange(0);
        const uint32_t sn = g_submission_count.exchange(0);
        const uint64_t sus = g_submission_us.exchange(0);
        if (ln || sn) {
          char b[160];
          std::snprintf(b, sizeof(b), "%slandings %u x %.1f ms (lock %.1f ms), submissions %u x %.1f ms",
                        line.empty() ? "" : ", ", ln, lus / 1000.0, llk / 1000.0, sn, sus / 1000.0);
          line += b;
        }
      }
      if (awaited || open || valid) {
''')
io.open(CP, "w", encoding="utf-8", newline="").write(s)
print("patched OK")
