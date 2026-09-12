"""Name the GPU fence waits. The sampler showed the GPU Commands thread spending
30-37% of its time in one WaitForSingleObject inside CheckSubmissionFence, but
that function serves several callers: frame pacing (wait for the frame from
kQueueFrames ago), the resolve readback drain (black-texture fix), memexport
readback, the pack's cache clear, occlusion queries. Each caller now names its
reason; the two waits are timed; a line every 5 s says who waited how long.
"""
import os, sys

C = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\command_processor.cpp"
H = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\include\rex\graphics\d3d12\command_processor.h"

edits = []

edits.append((H, """  HANDLE fence_completion_event_ = nullptr;
""", """  HANDLE fence_completion_event_ = nullptr;
  // Why the next fence wait happens, for the per-reason wait statistics in
  // CheckSubmissionFence. Callers with a specific reason set it for their
  // scope; "frame pacing" is what is left - BeginSubmission waiting for the
  // frame from kQueueFrames ago.
  const char* fence_reason_ = "frame pacing";
"""))

edits.append((C, """void D3D12CommandProcessor::CheckSubmissionFence(uint64_t await_submission) {
  if (await_submission >= submission_current_) {""",
"""namespace {

// Scoped reason for the fence waits below.
struct FenceReasonScope {
  const char*& slot;
  const char* previous;
  FenceReasonScope(const char*& s, const char* reason) : slot(s), previous(s) { s = reason; }
  ~FenceReasonScope() { slot = previous; }
};

struct FenceWaitStat {
  const char* reason = nullptr;
  uint32_t count = 0;
  uint64_t us = 0;
};
FenceWaitStat g_fence_waits[8];
auto g_fence_report_at = std::chrono::steady_clock::now();

void NoteFenceWait(const char* reason, uint64_t us) {
  for (auto& s : g_fence_waits) {
    if (s.reason == reason || s.reason == nullptr) {
      s.reason = reason;
      ++s.count;
      s.us += us;
      return;
    }
  }
}

// Every five seconds: who waited on the GPU, how often, for how long in
// total. The sampler can say "a fence wait, 37%"; only this says which one.
void MaybeReportFenceWaits() {
  const auto now = std::chrono::steady_clock::now();
  const double secs = std::chrono::duration<double>(now - g_fence_report_at).count();
  if (secs < 5.0) return;
  std::string line;
  for (auto& s : g_fence_waits) {
    if (!s.reason) break;
    char b[96];
    std::snprintf(b, sizeof(b), "%s%s %u x %.1f ms", line.empty() ? "" : ", ", s.reason,
                  s.count, s.us / 1000.0);
    line += b;
    s.count = 0;
    s.us = 0;
  }
  if (!line.empty()) REXLOG_INFO("[gpu] fence waits in {:.1f} s: {}", secs, line);
  g_fence_report_at = now;
}

}  // namespace

void D3D12CommandProcessor::CheckSubmissionFence(uint64_t await_submission) {
  MaybeReportFenceWaits();
  if (await_submission >= submission_current_) {"""))

edits.append((C, """        PROFILE_CMD_BUFFER_STALL();
        WaitForSingleObject(fence_completion_event_, INFINITE);
        queue_operations_done_since_submission_signal_ = false;""",
"""        PROFILE_CMD_BUFFER_STALL();
        {
          const auto t0 = std::chrono::steady_clock::now();
          WaitForSingleObject(fence_completion_event_, INFINITE);
          NoteFenceWait(fence_reason_, uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                                                     std::chrono::steady_clock::now() - t0)
                                                     .count()));
        }
        queue_operations_done_since_submission_signal_ = false;"""))

edits.append((C, """      PROFILE_CMD_BUFFER_STALL();
      WaitForSingleObject(fence_completion_event_, INFINITE);
      submission_completed_ = submission_fence_->GetCompletedValue();""",
"""      PROFILE_CMD_BUFFER_STALL();
      {
        const auto t0 = std::chrono::steady_clock::now();
        WaitForSingleObject(fence_completion_event_, INFINITE);
        NoteFenceWait(fence_reason_, uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                                                   std::chrono::steady_clock::now() - t0)
                                                   .count()));
      }
      submission_completed_ = submission_fence_->GetCompletedValue();"""))

edits.append((C, """bool D3D12CommandProcessor::IssueDraw_MemexportReadbackFullPath(uint32_t total_size) {
""", """bool D3D12CommandProcessor::IssueDraw_MemexportReadbackFullPath(uint32_t total_size) {
  FenceReasonScope fence_reason(fence_reason_, "memexport readback");
"""))
edits.append((C, """bool D3D12CommandProcessor::IssueDraw_MemexportReadbackFastPath(uint32_t total_size) {
""", """bool D3D12CommandProcessor::IssueDraw_MemexportReadbackFastPath(uint32_t total_size) {
  FenceReasonScope fence_reason(fence_reason_, "memexport fast readback");
"""))
edits.append((C, """bool D3D12CommandProcessor::IssueCopy_ReadbackResolvePath() {
""", """bool D3D12CommandProcessor::IssueCopy_ReadbackResolvePath() {
  FenceReasonScope fence_reason(fence_reason_, "resolve readback");
"""))
edits.append((C, """    if (cache_clear_requested_ && AwaitAllQueueOperationsCompletion()) {""",
"""    if (cache_clear_requested_ &&
        (FenceReasonScope(fence_reason_, "cache clear"), AwaitAllQueueOperationsCompletion())) {"""))
edits.append((C, """  CheckSubmissionFence(query_submission);
""", """  {
    FenceReasonScope fence_reason(fence_reason_, "query result");
    CheckSubmissionFence(query_submission);
  }
"""))

for path, old, new in edits:
    s = open(path, encoding="utf-8").read()
    n = s.count(old)
    if n != 1:
        print("FAIL: %d matches in %s for: %s" % (n, os.path.basename(path), old[:60].strip()))
        sys.exit(1)
    open(path, "w", encoding="utf-8", newline="").write(s.replace(old, new))
    print("ok  %s: %s" % (os.path.basename(path), old.strip().splitlines()[0][:60]))
print("all edits applied")
