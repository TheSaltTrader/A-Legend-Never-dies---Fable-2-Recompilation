"""Plugin: a pack texture is uploaded ONCE per resource.

The base cache re-loads a texture whenever the game writes its guest memory
(the memory watch trips). For a pack replacement that reload re-read the .tex
file and re-uploaded it - 1.3 ms of render-thread time - although the resource
already holds exactly those pixels and the guest bytes play no part in them.
Measured in play: ~5 such re-uploads per frame, 9300 in one session against a
338-file pack. Now: the first upload sets a flag on the texture; later loads
of a flagged replacement return at once. Behaviour is unchanged in every other
respect - the decision was already fixed at creation, so a later load never
looked the pack up afresh anyway.
"""
import os, sys

SDK = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"
H = os.path.join(SDK, "include", "rex", "graphics", "d3d12", "texture_cache.h")
C = os.path.join(SDK, "src", "graphics", "d3d12", "texture_cache.cpp")

edits = []

edits.append((H, """    void SetTexpackReplacement(uint32_t width, uint32_t height, std::string path) {
      texpack_replaced_ = true;
      texpack_width_ = width;
      texpack_height_ = height;
      texpack_path_ = std::move(path);
    }
""", """    void SetTexpackReplacement(uint32_t width, uint32_t height, std::string path) {
      texpack_replaced_ = true;
      texpack_width_ = width;
      texpack_height_ = height;
      texpack_path_ = std::move(path);
    }
    // The replacement's pixels are in the resource. A later load - the base
    // cache re-loads whenever the game writes the guest memory - has nothing
    // to do: the guest bytes are not what is shown. Measured at ~5 re-uploads
    // a frame, 1.3 ms each, before this was remembered.
    bool texpack_uploaded() const { return texpack_uploaded_; }
    void SetTexpackUploaded() { texpack_uploaded_ = true; }
"""))
edits.append((H, """    bool texpack_replaced_ = false;
    uint32_t texpack_width_ = 0;
""", """    bool texpack_replaced_ = false;
    bool texpack_uploaded_ = false;
    uint32_t texpack_width_ = 0;
"""))

edits.append((C, """    if (!load_base) {
      return true;  // mips: the replacement has only a base level
    }
    // Retire upload buffers the GPU has finished with.""", """    if (!load_base) {
      return true;  // mips: the replacement has only a base level
    }
    if (d3d12_texture.texpack_uploaded()) {
      // Already in the resource. The game wrote the guest copy (that is why
      // the cache asked again); the replacement does not come from there.
      static std::atomic<uint32_t> skipped{0};
      const uint32_t s = ++skipped;
      if (s == 1 || s % 1000 == 0) {
        REXLOG_INFO("[texpack] {} re-loads of pack textures skipped (resource already holds them)",
                    s);
      }
      return true;
    }
    // Retire upload buffers the GPU has finished with."""))

edits.append((C, """    g_texpack_uploads.emplace_back(command_processor_.GetCurrentSubmission(), std::move(upload));
""", """    g_texpack_uploads.emplace_back(command_processor_.GetCurrentSubmission(), std::move(upload));
    d3d12_texture.SetTexpackUploaded();
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
