"""Fix: a texture's pack decision is taken ONCE, at creation, and stored on the
texture. The upload and the view use the stored decision instead of a fresh
lookup, so a pack switch between creation and upload can no longer describe an
image of another size than the resource (heap overrun in the upload read)."""
import os, sys

ROOT = r"C:\users\renoi\claudecode\Fable 2 Recompile Xbox\rexglue-src"
H = os.path.join(ROOT, "include", "rex", "graphics", "d3d12", "texture_cache.h")
C = os.path.join(ROOT, "src", "graphics", "d3d12", "texture_cache.cpp")

edits = []  # (path, old, new)

# --- header ---------------------------------------------------------------
edits.append((H, "#include <memory>\n#include <unordered_map>\n",
              "#include <memory>\n#include <string>\n#include <unordered_map>\n"))
edits.append((H, """    void AddSRVDescriptorIndex(SRVDescriptorKey descriptor_key, uint32_t descriptor_index) {
      srv_descriptors_.emplace(descriptor_key, descriptor_index);
    }

   private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    D3D12_RESOURCE_STATES resource_state_;
    std::unique_ptr<D3D12Texture> texture_3d_as_2d_;
""", """    void AddSRVDescriptorIndex(SRVDescriptorKey descriptor_key, uint32_t descriptor_index) {
      srv_descriptors_.emplace(descriptor_key, descriptor_index);
    }

    // [texpack] The replacement this resource was CREATED for, if any: its
    // size and path, copied at creation. The upload and the view use THIS and
    // never a fresh lookup. The pack can be switched between creation and
    // upload (F9, or the settings screen, while a scene streams in), and a
    // fresh lookup then describes an image of another size than the resource
    // it is poured into. Measured on Fable II, 2026-09-11: an access violation
    // in memcpy under std::istream::read on the GPU thread, one frame after
    // the switch - the pack's rows written past the end of an upload buffer
    // sized for the guest texture.
    bool texpack_replaced() const { return texpack_replaced_; }
    uint32_t texpack_width() const { return texpack_width_; }
    uint32_t texpack_height() const { return texpack_height_; }
    const std::string& texpack_path() const { return texpack_path_; }
    void SetTexpackReplacement(uint32_t width, uint32_t height, std::string path) {
      texpack_replaced_ = true;
      texpack_width_ = width;
      texpack_height_ = height;
      texpack_path_ = std::move(path);
    }

   private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    D3D12_RESOURCE_STATES resource_state_;
    std::unique_ptr<D3D12Texture> texture_3d_as_2d_;
    bool texpack_replaced_ = false;
    uint32_t texpack_width_ = 0;
    uint32_t texpack_height_ = 0;
    std::string texpack_path_;
"""))

# --- creation -------------------------------------------------------------
edits.append((C, """  const uint8_t* texpack_guest = shared_memory().memory().TranslatePhysical<const uint8_t*>(
      uint32_t(key.base_page) << 12);
  if (const TexturePackFile* replacement = TexturePackLookup(
          key, texpack_guest, key.GetGuestLayout().base.level_data_extent_bytes, true)) {
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Width = replacement->width;
    desc.Height = replacement->height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
  }
""", """  const uint8_t* texpack_guest = shared_memory().memory().TranslatePhysical<const uint8_t*>(
      uint32_t(key.base_page) << 12);
  // Copied, not pointed at: the entry lives in a map the next pack switch
  // clears, and the decision has to outlive that on the texture (see below).
  TexturePackFile replacement;
  bool replaced = false;
  if (const TexturePackFile* found = TexturePackLookup(
          key, texpack_guest, key.GetGuestLayout().base.level_data_extent_bytes, true)) {
    replacement = *found;
    replaced = true;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Width = replacement.width;
    desc.Height = replacement.height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
  }
"""))
edits.append((C, """  return std::unique_ptr<Texture>(new D3D12Texture(*this, key, resource.Get(), resource_state));
}
""", """  auto* texture = new D3D12Texture(*this, key, resource.Get(), resource_state);
  // [texpack] The decision is taken here, once, and travels with the resource
  // it sized. The load and the view read it from the texture; a lookup of
  // their own could answer differently after a pack switch, for a resource
  // that cannot change with it.
  if (replaced) {
    texture->SetTexpackReplacement(replacement.width, replacement.height, replacement.path);
  }
  return std::unique_ptr<Texture>(texture);
}
"""))

# --- upload ---------------------------------------------------------------
edits.append((C, """  const uint8_t* texpack_guest = shared_memory().memory().TranslatePhysical<const uint8_t*>(
      uint32_t(texture_key.base_page) << 12);
  if (const TexturePackFile* replacement =
          TexturePackLookup(texture_key, texpack_guest, texture.GetGuestBaseSize(), false)) {
    if (!load_base) {
      return true;  // mips: the replacement has only a base level
    }
""", """  // The decision the resource was created with, NOT a fresh lookup - see
  // D3D12Texture::SetTexpackReplacement. If the pack was switched since, this
  // texture still uploads the file it was sized for; the switch drops every
  // texture at the end of the frame and the recreated one decides afresh.
  const TexturePackFile texpack_replacement{d3d12_texture.texpack_width(),
                                            d3d12_texture.texpack_height(),
                                            d3d12_texture.texpack_path()};
  if (const TexturePackFile* replacement =
          d3d12_texture.texpack_replaced() ? &texpack_replacement : nullptr) {
    if (!load_base) {
      return true;  // mips: the replacement has only a base level
    }
"""))
edits.append((C, """    device->GetCopyableFootprints(&dest_desc, 0, 1, 0, &footprint, &row_count, &row_bytes,
                                  &upload_size);

    D3D12_RESOURCE_DESC upload_desc = {};
""", """    device->GetCopyableFootprints(&dest_desc, 0, 1, 0, &footprint, &row_count, &row_bytes,
                                  &upload_size);
    // The read below trusts these to agree. They do by construction now (the
    // resource was created for this very replacement), and the day something
    // breaks that, this is the difference between a logged skip and a heap
    // overrun on the GPU thread.
    if (dest_desc.Width != replacement->width || dest_desc.Height != replacement->height ||
        footprint.Footprint.RowPitch < uint64_t(replacement->width) * 4 ||
        footprint.Offset + uint64_t(row_count) * footprint.Footprint.RowPitch > upload_size) {
      static std::atomic<int> logged{0};
      if (logged++ < 5) {
        REXGPU_ERROR("[texpack] {}: resource {}x{} does not fit the replacement {}x{} "
                     "(row pitch {}, upload {} bytes) - skipped",
                     replacement->path, dest_desc.Width, dest_desc.Height, replacement->width,
                     replacement->height, footprint.Footprint.RowPitch, upload_size);
      }
      return false;
    }

    D3D12_RESOURCE_DESC upload_desc = {};
"""))

# --- view -----------------------------------------------------------------
edits.append((C, """  const TexturePackFile* srv_replacement = TexturePackLookup(
      texture_key,
      shared_memory().memory().TranslatePhysical<const uint8_t*>(uint32_t(texture_key.base_page)
                                                                  << 12),
      texture.GetGuestBaseSize(), false);
  if (srv_replacement) {
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  }
""", """  // The resource's own decision, for the same reason as the upload: a view in
  // the guest format on an RGBA resource created for the pack is invalid, and
  // that is exactly what a fresh lookup answers once the pack is switched off.
  const bool srv_replacement = texture.texpack_replaced();
  if (srv_replacement) {
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  }
"""))

for path, old, new in edits:
    s = open(path, encoding="utf-8").read()
    n = s.count(old)
    if n != 1:
        print("FAIL: %d matches in %s for: %s" % (n, os.path.basename(path), old[:70].strip()))
        sys.exit(1)
    open(path, "w", encoding="utf-8", newline="").write(s.replace(old, new))
    print("ok  %s: %s" % (os.path.basename(path), old.strip().splitlines()[0][:70]))
print("all edits applied")
