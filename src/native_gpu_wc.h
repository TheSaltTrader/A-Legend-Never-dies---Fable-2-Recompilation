// A WRITE-ONLY view of a CPU-mapped GPU heap.
//
// A D3D12 UPLOAD heap mapped for the CPU is WRITE-COMBINED memory: stores
// stream through the WC buffers cheaply, but a LOAD is uncached, drains the
// buffers and stalls the core for hundreds of nanoseconds. On 2026-09-23 the
// two hottest instructions of the whole native GPU path (14.7 % of the render
// thread's samples, ~2.9 ms of an 18.9 ms frame - more than the entire
// clause-A gap) were two such loads: the pack line of the shared constant
// block read back blk[136] and blk[140] from the upload heap after storing
// them (leg P6, PERF_BASELINE_2026-09-22.md). The line table pointed at the
// wrong statement; the disassembly at the sampled addresses named it.
//
// A class fixed by inspection comes back (this project's gate class had seven
// members, its key collisions seven). So the mapped pointer is not a raw
// pointer any more: this wrapper hands out spans whose elements accept
// assignment and nothing else. Reading through it is a COMPILE ERROR, not a
// grep hit. Anything the CPU must read later is kept in a CPU-side shadow,
// written at the same time. The one escape hatch is named for what it costs.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ngpu {

// One element of a write-only span: assignable, never readable.
template <typename T>
class WcRef {
 public:
  explicit WcRef(T* p) : p_(p) {}
  WcRef& operator=(T v) {
    *p_ = v;
    return *this;
  }
  // No conversion to T, no operator&, no compound assignment: each of those
  // would be a read from write-combined memory.
 private:
  T* p_;
};

// A typed window onto the heap at some offset: element stores only.
template <typename T>
class WcSpan {
 public:
  WcSpan() = default;
  explicit WcSpan(T* p) : p_(p) {}
  WcRef<T> operator[](size_t i) const { return WcRef<T>(p_ + i); }
  bool valid() const { return p_ != nullptr; }
  // Bulk store from CPU memory - the cheap way to fill a block: assemble it in
  // a stack or thread-local array (readable, cached), then copy once.
  void store(size_t first, const T* src, size_t count) const { std::memcpy(p_ + first, src, count * sizeof(T)); }

 private:
  T* p_ = nullptr;
};

// The mapped heap itself.
class WcHeap {
 public:
  WcHeap() = default;
  explicit WcHeap(uint8_t* base) : base_(base) {}
  bool valid() const { return base_ != nullptr; }
  template <typename T>
  WcSpan<T> at(size_t byte_off) const { return WcSpan<T>(base_ ? reinterpret_cast<T*>(base_ + byte_off) : nullptr); }
  void store(size_t byte_off, const void* src, size_t bytes) const { std::memcpy(base_ + byte_off, src, bytes); }
  // STORE-ONLY raw pointer for the few writers that need pointer arithmetic
  // (texel rows, per-vertex conversion, the cache allocator). The name is the
  // contract: the memory behind it is write-combined and must not be read.
  // Grep it; every use should be a store.
  uint8_t* store_ptr(size_t byte_off) const { return base_ + byte_off; }
  // DELIBERATE READ-BACK FROM WRITE-COMBINED MEMORY. Every call is a stall of
  // hundreds of nanoseconds per cache line and drains the WC buffers. Only for
  // one-shot diagnostics (a dump, a once-per-run log line) - never on a
  // per-draw path. The name is the audit trail: grep it.
  const uint8_t* slow_uncached_readback() const { return base_; }

 private:
  uint8_t* base_ = nullptr;
};

}  // namespace ngpu
