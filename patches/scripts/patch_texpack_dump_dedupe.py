import os, sys
ROOT = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"

def edit(rel, pairs):
    p = os.path.join(ROOT, rel)
    d = open(p, "rb").read(); crlf = b"\r\n" in d
    t = d.decode("utf-8").replace("\r\n", "\n")
    for old, new in pairs:
        if t.count(old) != 1:
            sys.exit("%s: %d matches for %r" % (rel, t.count(old), old[:70]))
        t = t.replace(old, new)
    open(p, "wb").write((t.replace("\n", "\r\n") if crlf else t).encode("utf-8"))
    print("patched", rel)

# The dump wrote every (address id, content hash) pair it met, remembered only
# within the session (capped at 20,000), and never looked at the folder: each
# session re-dumped what it loaded, and a content met at a new address became a
# new file. An hour of play with dumping on at 400% draw distance wrote 101,493
# raw files, 67 GB, and filled the disk (2026-09-13). Now the key is the pack's
# own (shape, content hash), the set is seeded from the files already on disk,
# and only the per-session count is capped.
edit("src/graphics/d3d12/texture_cache.cpp", [
    ('    const uint64_t id = TexturePackId(tk);\n'
     '    static std::set<TexturePackKey> dumped;\n'
     '    static std::mutex dump_mutex;\n',
     '    const uint64_t id = TexturePackId(tk);\n'
     '    // (shape, content hash) of every raw dump on disk plus this session\'s -\n'
     '    // the pack\'s own identity, address-free. Seeded from the folder the\n'
     '    // first time a folder is dumped to. Before this the key carried the\n'
     '    // address and the set was per session: every session re-dumped what it\n'
     '    // loaded, and a texture met at a new address became a new file - an\n'
     '    // hour of play at 400% draw distance wrote 101,493 files, 67 GB, and\n'
     '    // filled the disk (2026-09-13).\n'
     '    static std::set<std::pair<uint64_t, uint32_t>> dumped;\n'
     '    static std::string dumped_dir;\n'
     '    static uint32_t dumped_this_session = 0;\n'
     '    static std::mutex dump_mutex;\n'),
    ('      const uint32_t hash = TexturePackContentHash(src, tsize);\n'
     '      bool fresh = false;\n'
     '      {\n'
     '        std::lock_guard<std::mutex> lock(dump_mutex);\n'
     '        fresh = dumped.insert(TexturePackKey{id, hash}).second && dumped.size() <= 20000;\n'
     '      }\n'
     '      if (fresh) {\n'
     '        const std::string dir = rex::cvar::Query<std::string>("texture_dump_path");\n',
     '      const uint32_t hash = TexturePackContentHash(src, tsize);\n'
     '      const std::string dir = rex::cvar::Query<std::string>("texture_dump_path");\n'
     '      bool fresh = false;\n'
     '      {\n'
     '        std::lock_guard<std::mutex> lock(dump_mutex);\n'
     '        if (dir != dumped_dir) {\n'
     '          dumped.clear();\n'
     '          dumped_dir = dir;\n'
     '          dumped_this_session = 0;\n'
     '          std::error_code ec;\n'
     '          for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {\n'
     '            const std::string n = e.path().filename().string();\n'
     '            unsigned long long fid = 0;\n'
     '            unsigned fh = 0;\n'
     '            if (n.size() == 33 && std::sscanf(n.c_str(), "tex_%16llx-%8x.bin", &fid, &fh) == 2)\n'
     '              dumped.insert({TexturePackShape(uint64_t(fid)), uint32_t(fh)});\n'
     '          }\n'
     '          REXLOG_INFO("[texpack] dump folder \'{}\' already holds {} distinct contents; "\n'
     '                      "those are not written again",\n'
     '                      dir, dumped.size());\n'
     '        }\n'
     '        fresh = dumped_this_session < 20000 &&\n'
     '                dumped.insert({TexturePackShape(id), hash}).second;\n'
     '        if (fresh) ++dumped_this_session;\n'
     '      }\n'
     '      if (fresh) {\n'),
])
print("done")
