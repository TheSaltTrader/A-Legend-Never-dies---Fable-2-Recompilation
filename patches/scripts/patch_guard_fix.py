"""Two plugin edits.

1. The upload-fit guard added today was too strict: GetCopyableFootprints
   sizes the upload buffer as Offset + (rows-1)*RowPitch + row_bytes, i.e. the
   LAST row is not padded to the pitch. The guard demanded rows*RowPitch and
   rejected every replacement whose width is not a multiple of 64 (432, 368,
   288 wide - seen five times in the player's first session on 0.0.13). Bound
   it the way the read actually writes.

2. Diagnostic for the re-upload churn: which ids are uploaded again and again
   (9300 uploads of <=338 files in one session, ~200/s, ~5 per frame at 39
   fps). Every 5 s, the top three ids by upload count, with size.
"""
import os, sys

C = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\texture_cache.cpp"

edits = []

edits.append((C, """    if (dest_desc.Width != replacement->width || dest_desc.Height != replacement->height ||
        footprint.Footprint.RowPitch < uint64_t(replacement->width) * 4 ||
        footprint.Offset + uint64_t(row_count) * footprint.Footprint.RowPitch > upload_size) {
""", """    // The last row is NOT padded to the pitch in the footprint's size, so the
    // bound is (rows - 1) pitches plus one row of pixels - exactly what the
    // read below writes. Demanding rows * pitch rejected every width that is
    // not a multiple of 64 (432, 368, 288 wide) on the first play of 0.0.13.
    if (dest_desc.Width != replacement->width || dest_desc.Height != replacement->height ||
        footprint.Footprint.RowPitch < uint64_t(replacement->width) * 4 || row_count == 0 ||
        footprint.Offset + uint64_t(row_count - 1) * footprint.Footprint.RowPitch +
                uint64_t(replacement->width) * 4 >
            upload_size) {
"""))

edits.append((C, """    static std::atomic<uint32_t> replaced{0};
    static std::atomic<uint64_t> total_us{0};
    static std::atomic<uint64_t> total_read_us{0};
    const uint32_t n = ++replaced;
""", """    // Which textures are uploaded AGAIN AND AGAIN. One session showed 9300
    // uploads of a pack with 338 files - about 5 per frame at 39 fps, each
    // 1.3 ms of render-thread time - and nothing named the textures. Every
    // five seconds: the three ids uploaded most often in that window.
    {
      static std::unordered_map<uint64_t, uint32_t> uploads_by_id;
      static std::unordered_map<uint64_t, uint32_t> size_by_id;
      static auto window_start = std::chrono::steady_clock::now();
      const uint64_t id = TexturePackId(texture.key());
      ++uploads_by_id[id];
      size_by_id[id] = (replacement->width << 16) | replacement->height;
      const auto now_t = std::chrono::steady_clock::now();
      if (std::chrono::duration<double>(now_t - window_start).count() >= 5.0) {
        uint64_t top_id[3] = {0, 0, 0};
        uint32_t top_n[3] = {0, 0, 0};
        uint32_t total = 0;
        for (const auto& kv : uploads_by_id) {
          total += kv.second;
          for (int i = 0; i < 3; ++i) {
            if (kv.second > top_n[i]) {
              for (int j = 2; j > i; --j) { top_n[j] = top_n[j - 1]; top_id[j] = top_id[j - 1]; }
              top_n[i] = kv.second;
              top_id[i] = kv.first;
              break;
            }
          }
        }
        REXLOG_INFO("[texpack] re-uploads in {:.1f} s: {} uploads of {} ids; top {:016X} {}x{} x{}, "
                    "{:016X} {}x{} x{}, {:016X} {}x{} x{}",
                    std::chrono::duration<double>(now_t - window_start).count(), total,
                    uploads_by_id.size(), top_id[0], size_by_id[top_id[0]] >> 16,
                    size_by_id[top_id[0]] & 0xFFFF, top_n[0], top_id[1],
                    size_by_id[top_id[1]] >> 16, size_by_id[top_id[1]] & 0xFFFF, top_n[1],
                    top_id[2], size_by_id[top_id[2]] >> 16, size_by_id[top_id[2]] & 0xFFFF,
                    top_n[2]);
        uploads_by_id.clear();
        size_by_id.clear();
        window_start = now_t;
      }
    }

    static std::atomic<uint32_t> replaced{0};
    static std::atomic<uint64_t> total_us{0};
    static std::atomic<uint64_t> total_read_us{0};
    const uint32_t n = ++replaced;
"""))

for path, old, new in edits:
    s = open(path, encoding="utf-8").read()
    n = s.count(old)
    if n != 1:
        print("FAIL: %d matches for: %s" % (n, old[:60].strip()))
        sys.exit(1)
    open(path, "w", encoding="utf-8", newline="").write(s.replace(old, new))
    print("ok:", old.strip().splitlines()[0][:60])
print("all edits applied")
