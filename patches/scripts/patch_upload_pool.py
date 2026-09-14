"""s77 - shared_memory_upload_threads (int, 3; 0 = inline as before): the
copies of guest memory into the upload buffer are spread over a small pool
of helper threads plus the calling thread, joined before the function
returns. The market walk uploads ~18 MB a frame on the plugin's command
thread (RequestRanges 13% of its profile, 2026-09-14); the copies are
independent of each other and of the command-list recording, which only
needs them finished before the submission executes - and UploadRanges
returns before that. Chunks under 256 KB stay inline (the hand-off costs
more than the copy). APPLY ONCE."""
P = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\shared_memory.cpp"
s = open(P, encoding="utf-8").read()
assert "shared_memory_upload_threads" not in s, "already applied"

def rep(old, new):
    global s
    assert s.count(old) == 1, ("expected 1, found %d: %s" % (s.count(old), old[:90]))
    s = s.replace(old, new)

rep('''namespace rex::graphics::d3d12 {
''', '''REXCVAR_DEFINE_INT32(shared_memory_upload_threads, 3, "GPU/D3D12",
                     "Helper threads for the guest-memory to upload-buffer copies (0 = copy inline on the "
                     "command thread; the market uploads ~18 MB a frame)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace rex::graphics::d3d12 {

namespace {
// [perf] A pool for the upload copies: jobs are added while the command
// list is recorded, then run on the helpers AND the caller, and joined.
class UploadCopyPool {
 public:
  explicit UploadCopyPool(int threads) {
    for (int i = 0; i < threads; ++i) workers_.emplace_back([this] { Run(); });
  }
  ~UploadCopyPool() {
    {
      std::lock_guard<std::mutex> lock(m_);
      quit_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) w.join();
  }
  void Add(void* dst, const void* src, size_t size) { jobs_.push_back({dst, src, size}); }
  bool Empty() const { return jobs_.empty(); }
  void RunAll() {
    if (jobs_.empty()) return;
    {
      std::lock_guard<std::mutex> lock(m_);
      next_ = 0;
      pending_ = jobs_.size();
      ++generation_;
    }
    cv_.notify_all();
    Work();
    std::unique_lock<std::mutex> lock(m_);
    done_cv_.wait(lock, [this] { return pending_ == 0; });
    jobs_.clear();
  }

 private:
  struct Job {
    void* dst;
    const void* src;
    size_t size;
  };
  void Work() {
    for (;;) {
      size_t i;
      {
        std::lock_guard<std::mutex> lock(m_);
        if (next_ >= jobs_.size()) return;
        i = next_++;
      }
      std::memcpy(jobs_[i].dst, jobs_[i].src, jobs_[i].size);
      std::lock_guard<std::mutex> lock(m_);
      if (--pending_ == 0) done_cv_.notify_all();
    }
  }
  void Run() {
    uint64_t seen = 0;
    for (;;) {
      std::unique_lock<std::mutex> lock(m_);
      cv_.wait(lock, [&] { return quit_ || generation_ != seen; });
      if (quit_) return;
      seen = generation_;
      lock.unlock();
      Work();
    }
  }
  std::vector<Job> jobs_;
  std::vector<std::thread> workers_;
  std::mutex m_;
  std::condition_variable cv_, done_cv_;
  size_t next_ = 0, pending_ = 0;
  uint64_t generation_ = 0;
  bool quit_ = false;
};
UploadCopyPool* g_upload_pool = nullptr;
}  // namespace
''')

rep('''      MakeRangeValid(upload_range_start << page_size_log2(), uint32_t(upload_buffer_size), false);
      command_processor_.NoteSharedMemoryUpload(uint64_t(upload_buffer_size));  // [hitch]
      std::memcpy(upload_buffer_mapping,
                  memory().TranslatePhysical(upload_range_start << page_size_log2()),
                  upload_buffer_size);
''', '''      MakeRangeValid(upload_range_start << page_size_log2(), uint32_t(upload_buffer_size), false);
      command_processor_.NoteSharedMemoryUpload(uint64_t(upload_buffer_size));  // [hitch]
      {
        const int threads = REXCVAR_GET(shared_memory_upload_threads);
        if (threads > 0 && !g_upload_pool) g_upload_pool = new UploadCopyPool(std::min(threads, 8));
        const void* src = memory().TranslatePhysical(upload_range_start << page_size_log2());
        if (threads > 0 && upload_buffer_size >= (256u << 10)) {
          // Big chunks are split in 256 KB pieces so every helper gets a share.
          for (size_t off = 0; off < upload_buffer_size; off += (256u << 10)) {
            const size_t piece = std::min(size_t(256u << 10), upload_buffer_size - off);
            g_upload_pool->Add(upload_buffer_mapping + off, static_cast<const uint8_t*>(src) + off,
                               piece);
          }
        } else {
          std::memcpy(upload_buffer_mapping, src, upload_buffer_size);
        }
      }
''')

# join before returning (both the success path and the failure path after any job was added)
rep('''      if (upload_buffer_mapping == nullptr) {
        REXGPU_ERROR("Shared memory: Failed to get an upload buffer");
        return false;
      }
''', '''      if (upload_buffer_mapping == nullptr) {
        REXGPU_ERROR("Shared memory: Failed to get an upload buffer");
        if (g_upload_pool) g_upload_pool->RunAll();
        return false;
      }
''')
rep('''      uint32_t upload_buffer_pages = uint32_t(upload_buffer_size >> page_size_log2());
      upload_range_start += upload_buffer_pages;
      upload_range_length -= upload_buffer_pages;
    }
  }
  return true;
}
''', '''      uint32_t upload_buffer_pages = uint32_t(upload_buffer_size >> page_size_log2());
      upload_range_start += upload_buffer_pages;
      upload_range_length -= upload_buffer_pages;
    }
  }
  if (g_upload_pool) g_upload_pool->RunAll();
  return true;
}
''')
for inc in ("<condition_variable>", "<mutex>", "<thread>", "<vector>", "<algorithm>"):
    if "#include " + inc not in s:
        s = s.replace("#include <cstring>", "#include <cstring>\n#include " + inc, 1)
open(P, "w", encoding="utf-8", newline="").write(s)
print("patched: upload copy pool")
