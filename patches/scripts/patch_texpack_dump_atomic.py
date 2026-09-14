"""s65 - the texture dump is a consistent snapshot: the bytes are copied to a
private buffer, the buffer is hashed, and the live memory is hashed again;
if the two differ the game was still streaming that memory and nothing is
written (the content will be dumped when it is next loaded, settled). Until
now the hash was taken from the live memory and the file written from the
live memory a moment later - on a streaming slot the file carried the NEXT
occupant's bytes under the previous occupant's hash, and every pack picture
made from it was another texture's art (744 such hashes in the 2026-09-14
audit: the ornament in the Oakfield sky, the villager outfit on the barrels).
APPLY ONCE (requires patch_texpack_gpuwritten.py)."""
P = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src\src\graphics\d3d12\texture_cache.cpp"
s = open(P, encoding="utf-8").read()

def rep(old, new):
    global s
    assert s.count(old) == 1, ("expected 1, found %d: %s" % (s.count(old), old[:90]))
    s = s.replace(old, new)

rep('''      const uint32_t hash = TexturePackContentHash(src, tsize);
      const std::string dir = rex::cvar::Query<std::string>("texture_dump_path");
      bool fresh = false;
''', '''      const uint32_t hash = TexturePackContentHash(src, tsize);
      const std::string dir = rex::cvar::Query<std::string>("texture_dump_path");
      bool fresh = false;
      // Only a snapshot whose bytes hash to the name is written (below): the
      // live memory may be mid-stream, and a file named by one moment's hash
      // holding another moment's bytes poisons the pack for that hash.
      std::vector<uint8_t> snap;
''')

rep('''      if (fresh) {
        char path[512];
        std::snprintf(path, sizeof(path), "%s/tex_%016llX-%08X.bin",
                      dir.c_str(), (unsigned long long)id, hash);
        if (FILE* f = std::fopen(path, "wb")) {
          std::fwrite(src, 1, tsize, f);
          std::fclose(f);
''', '''      if (fresh) {
        snap.assign(src, src + tsize);
        if (TexturePackContentHash(snap.data(), tsize) != hash ||
            TexturePackContentHash(src, tsize) != hash) {
          // Torn: the memory changed under the copy. Forget it so the settled
          // content is dumped on a later load.
          std::lock_guard<std::mutex> lock(dump_mutex);
          dumped.erase({TexturePackShape(id), hash});
          --dumped_this_session;
          static std::atomic<uint32_t> torn{0};
          const uint32_t n = ++torn;
          if (n <= 10 || n % 100 == 0)
            REXLOG_INFO("[texpack] dump skipped: memory under {:016X} changed while it was "
                        "copied ({} so far)", id, n);
          fresh = false;
        }
      }
      if (fresh) {
        char path[512];
        std::snprintf(path, sizeof(path), "%s/tex_%016llX-%08X.bin",
                      dir.c_str(), (unsigned long long)id, hash);
        if (FILE* f = std::fopen(path, "wb")) {
          std::fwrite(snap.data(), 1, tsize, f);
          std::fclose(f);
''')

open(P, "w", encoding="utf-8", newline="").write(s)
print("patched: atomic texture dump")
