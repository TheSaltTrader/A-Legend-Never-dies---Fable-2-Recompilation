"""Runtime: physical-memory DATA PROVIDERS (read watches), the TODO in xmemory.

A range can be watched for ANY access (pages set no-access in the three guest
views). On the first access from a guest view the registered providers are
called on the faulting thread - they may release and re-take the global lock
while they wait for another thread to produce the data - then the pages get
their access back (read/write, or read-only if still write-watched) and the
faulting instruction re-executes. A range can also be released without an
access (DisablePhysicalMemoryDataProviders). Writes to such a page then go on
through the invalidation path as before. The plugin uses it for resolves whose
CPU copy is still in flight: the game's CPU only ever waits when it actually
touches such memory, instead of every resolve draining the GPU.
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

edit("include/rex/system/xmemory.h", [
    ("  struct SystemPageFlagsBlock {\n"
     "    // Whether writing to each page should result trigger invalidation\n"
     "    // callbacks.\n"
     "    uint64_t notify_on_invalidation;\n"
     "  };\n",
     "  struct SystemPageFlagsBlock {\n"
     "    // Whether writing to each page should result trigger invalidation\n"
     "    // callbacks.\n"
     "    uint64_t notify_on_invalidation = 0;\n"
     "    // Whether ANY access to each page should call the data providers first\n"
     "    // (the page is no-access until then).\n"
     "    uint64_t provide_on_access = 0;\n"
     "  };\n"),
    ("  void EnableAccessCallbacks(uint32_t physical_address, uint32_t length,\n",
     "  // Releases the data-provider watch on the range without an access: the\n"
     "  // pages get their access back (read-only if still write-watched).\n"
     "  void DisableDataProviders(uint32_t physical_address, uint32_t length);\n"
     "  void EnableAccessCallbacks(uint32_t physical_address, uint32_t length,\n"),
    ("  // Enables physical memory access callbacks for the specified memory range,\n"
     "  // snapped to system page boundaries.\n"
     "  void EnablePhysicalMemoryAccessCallbacks(uint32_t physical_address, uint32_t length,\n"
     "                                           bool enable_invalidation_notifications,\n"
     "                                           bool enable_data_providers);\n",
     "  // Data providers: called on the faulting thread on the first access (read\n"
     "  // or write) to a page enabled with enable_data_providers, with the global\n"
     "  // critical region locked once (the provider may unlock and re-lock it\n"
     "  // while it waits for the data to be produced - by another thread, into\n"
     "  // the host physical heap directly). After the providers return, the pages\n"
     "  // get their access back and the instruction re-executes.\n"
     "  typedef void (*PhysicalMemoryDataProviderCallback)(\n"
     "      void* context_ptr, std::unique_lock<std::recursive_mutex>& global_lock_locked_once,\n"
     "      uint32_t physical_address_start, uint32_t length, bool is_write);\n"
     "  void* RegisterPhysicalMemoryDataProvider(PhysicalMemoryDataProviderCallback callback,\n"
     "                                           void* callback_context);\n"
     "  void UnregisterPhysicalMemoryDataProvider(void* callback_handle);\n"
     "  // Releases a data-provider watch without an access (all guest views).\n"
     "  void DisablePhysicalMemoryDataProviders(uint32_t physical_address, uint32_t length);\n"
     "\n"
     "  // Enables physical memory access callbacks for the specified memory range,\n"
     "  // snapped to system page boundaries.\n"
     "  void EnablePhysicalMemoryAccessCallbacks(uint32_t physical_address, uint32_t length,\n"
     "                                           bool enable_invalidation_notifications,\n"
     "                                           bool enable_data_providers);\n"),
    ("  std::vector<std::pair<PhysicalMemoryInvalidationCallback, void*>*>\n"
     "      physical_memory_invalidation_callbacks_;\n"
     "};\n",
     "  std::vector<std::pair<PhysicalMemoryInvalidationCallback, void*>*>\n"
     "      physical_memory_invalidation_callbacks_;\n"
     "  std::vector<std::pair<PhysicalMemoryDataProviderCallback, void*>*>\n"
     "      physical_memory_data_providers_;\n"
     "};\n"),
])

edit("src/system/xmemory.cpp", [
    # ---- Memory: register / unregister / disable ---------------------------------
    ("void Memory::EnablePhysicalMemoryAccessCallbacks(uint32_t physical_address, uint32_t length,\n",
     "void* Memory::RegisterPhysicalMemoryDataProvider(PhysicalMemoryDataProviderCallback callback,\n"
     "                                                 void* callback_context) {\n"
     "  auto entry = new std::pair<PhysicalMemoryDataProviderCallback, void*>(callback, callback_context);\n"
     "  auto lock = global_critical_region_.Acquire();\n"
     "  physical_memory_data_providers_.push_back(entry);\n"
     "  return entry;\n"
     "}\n"
     "\n"
     "void Memory::UnregisterPhysicalMemoryDataProvider(void* callback_handle) {\n"
     "  auto entry = reinterpret_cast<std::pair<PhysicalMemoryDataProviderCallback, void*>*>(callback_handle);\n"
     "  {\n"
     "    auto lock = global_critical_region_.Acquire();\n"
     "    auto it = std::find(physical_memory_data_providers_.begin(),\n"
     "                        physical_memory_data_providers_.end(), entry);\n"
     "    if (it != physical_memory_data_providers_.end()) {\n"
     "      physical_memory_data_providers_.erase(it);\n"
     "    }\n"
     "  }\n"
     "  delete entry;\n"
     "}\n"
     "\n"
     "void Memory::DisablePhysicalMemoryDataProviders(uint32_t physical_address, uint32_t length) {\n"
     "  heaps_.vA0000000.DisableDataProviders(physical_address, length);\n"
     "  heaps_.vC0000000.DisableDataProviders(physical_address, length);\n"
     "  heaps_.vE0000000.DisableDataProviders(physical_address, length);\n"
     "}\n"
     "\n"
     "void Memory::EnablePhysicalMemoryAccessCallbacks(uint32_t physical_address, uint32_t length,\n"),
    # ---- PhysicalHeap::EnableAccessCallbacks: data providers ----------------------
    ("  // TODO(Triang3l): Implement data providers.\n"
     "  assert_false(enable_data_providers);\n"
     "  if (!enable_invalidation_notifications && !enable_data_providers) {\n",
     "  if (!enable_invalidation_notifications && !enable_data_providers) {\n"),
    ("    if (current_page_access != rex::memory::PageAccess::kNoAccess) {\n"
     "      // TODO(Triang3l): Enable data providers.\n"
     "      if (enable_invalidation_notifications) {\n"
     "        if (current_page_access != rex::memory::PageAccess::kReadOnly &&\n"
     "            (page_flags_block.notify_on_invalidation & page_flags_bit) == 0) {\n"
     "          // TODO(Triang3l): Check if data providers are already enabled.\n"
     "          // If data providers are already enabled for the page, it has even\n"
     "          // stricter protection.\n"
     "          protect_system_page = true;\n"
     "          page_flags_block.notify_on_invalidation |= page_flags_bit;\n"
     "        }\n"
     "      }\n"
     "    }\n",
     "    if (current_page_access != rex::memory::PageAccess::kNoAccess) {\n"
     "      const bool providing = (page_flags_block.provide_on_access & page_flags_bit) != 0;\n"
     "      if (enable_data_providers && !providing) {\n"
     "        // Any access must reach the providers first: no access until then.\n"
     "        protect_system_page = true;\n"
     "        page_flags_block.provide_on_access |= page_flags_bit;\n"
     "      }\n"
     "      if (enable_invalidation_notifications) {\n"
     "        if (current_page_access != rex::memory::PageAccess::kReadOnly &&\n"
     "            (page_flags_block.notify_on_invalidation & page_flags_bit) == 0) {\n"
     "          // A page with data providers already has stricter protection:\n"
     "          // only the flag, the no-access stays.\n"
     "          if (!providing && !enable_data_providers) protect_system_page = true;\n"
     "          page_flags_block.notify_on_invalidation |= page_flags_bit;\n"
     "        }\n"
     "      }\n"
     "    }\n"),
    # ---- PhysicalHeap::DisableDataProviders + the shared restore helper ----------
    ("bool PhysicalHeap::TriggerCallbacks(std::unique_lock<std::recursive_mutex> global_lock_locked_once,\n"
     "                                    uint32_t virtual_address, uint32_t length, bool is_write,\n"
     "                                    bool unwatch_exact_range, bool unprotect) {\n"
     "  // TODO(Triang3l): Support read watches.\n"
     "  assert_true(is_write);\n"
     "  if (!is_write) {\n"
     "    return false;\n"
     "  }\n",
     "// Clears the data-provider flag of the system pages [first, last] and gives\n"
     "// them their access back: the guest's own protection, read-only if the page\n"
     "// is still write-watched. Caller holds the global critical region.\n"
     "void PhysicalHeap::RestoreProviderPages(uint32_t system_page_first, uint32_t system_page_last) {\n"
     "  uint8_t* protect_base = membase_ + heap_base_;\n"
     "  uint32_t run_first = UINT32_MAX;\n"
     "  rex::memory::PageAccess run_access = rex::memory::PageAccess::kNoAccess;\n"
     "  auto flush = [&](uint32_t end_exclusive) {\n"
     "    if (run_first != UINT32_MAX) {\n"
     "      rex::memory::Protect(protect_base + run_first * system_page_size_,\n"
     "                           (end_exclusive - run_first) * system_page_size_, run_access);\n"
     "      run_first = UINT32_MAX;\n"
     "    }\n"
     "  };\n"
     "  for (uint32_t i = system_page_first; i <= system_page_last; ++i) {\n"
     "    SystemPageFlagsBlock& block = system_page_flags_[i >> 6];\n"
     "    const uint64_t bit = uint64_t(1) << (i & 63);\n"
     "    if ((block.provide_on_access & bit) == 0) {\n"
     "      flush(i);\n"
     "      continue;\n"
     "    }\n"
     "    block.provide_on_access &= ~bit;\n"
     "    uint32_t guest_page_number =\n"
     "        rex::sat_sub(i * system_page_size_, host_address_offset()) >> page_size_shift_;\n"
     "    rex::memory::PageAccess access = rex::memory::PageAccess::kReadWrite;\n"
     "    if (guest_page_number < page_table_.size()) {\n"
     "      access = ToPageAccess(page_table_[guest_page_number].current_protect);\n"
     "    }\n"
     "    if (access == rex::memory::PageAccess::kNoAccess) access = rex::memory::PageAccess::kReadWrite;\n"
     "    if ((block.notify_on_invalidation & bit) && access == rex::memory::PageAccess::kReadWrite) {\n"
     "      access = rex::memory::PageAccess::kReadOnly;  // still write-watched\n"
     "    }\n"
     "    if (run_first != UINT32_MAX && access != run_access) flush(i);\n"
     "    if (run_first == UINT32_MAX) {\n"
     "      run_first = i;\n"
     "      run_access = access;\n"
     "    }\n"
     "  }\n"
     "  flush(system_page_last + 1);\n"
     "}\n"
     "\n"
     "void PhysicalHeap::DisableDataProviders(uint32_t physical_address, uint32_t length) {\n"
     "  uint32_t physical_address_offset = GetPhysicalAddress(heap_base_);\n"
     "  if (physical_address < physical_address_offset) {\n"
     "    if (physical_address_offset - physical_address >= length) return;\n"
     "    length -= physical_address_offset - physical_address;\n"
     "    physical_address = physical_address_offset;\n"
     "  }\n"
     "  uint32_t heap_relative_address = physical_address - physical_address_offset;\n"
     "  if (heap_relative_address >= heap_size_) return;\n"
     "  length = std::min(length, heap_size_ - heap_relative_address);\n"
     "  if (!length) return;\n"
     "  uint32_t system_page_first = (heap_relative_address + host_address_offset()) / system_page_size_;\n"
     "  uint32_t system_page_last =\n"
     "      (heap_relative_address + length - 1 + host_address_offset()) / system_page_size_;\n"
     "  system_page_last = std::min(system_page_last, system_page_count_ - 1);\n"
     "  auto global_lock = global_critical_region_.Acquire();\n"
     "  RestoreProviderPages(system_page_first, system_page_last);\n"
     "}\n"
     "\n"
     "bool PhysicalHeap::TriggerCallbacks(std::unique_lock<std::recursive_mutex> global_lock_locked_once,\n"
     "                                    uint32_t virtual_address, uint32_t length, bool is_write,\n"
     "                                    bool unwatch_exact_range, bool unprotect) {\n"),
    # ---- TriggerCallbacks: providers first, for any access -----------------------
    ("  uint32_t block_index_first = system_page_first >> 6;\n"
     "  uint32_t block_index_last = system_page_last >> 6;\n"
     "\n"
     "  // Check if watching any page, whether need to call the callback at all.\n"
     "  bool any_watched = false;\n",
     "  uint32_t block_index_first = system_page_first >> 6;\n"
     "  uint32_t block_index_last = system_page_last >> 6;\n"
     "\n"
     "  // Data providers first, for reads and writes alike: the pages are\n"
     "  // no-access until the providers have supplied the data.\n"
     "  bool provided = false;\n"
     "  {\n"
     "    bool any_provide = false;\n"
     "    for (uint32_t i = block_index_first; i <= block_index_last; ++i) {\n"
     "      uint64_t block = system_page_flags_[i].provide_on_access;\n"
     "      if (i == block_index_first) block &= ~((uint64_t(1) << (system_page_first & 63)) - 1);\n"
     "      if (i == block_index_last && (system_page_last & 63) != 63)\n"
     "        block &= (uint64_t(1) << ((system_page_last & 63) + 1)) - 1;\n"
     "      if (block) {\n"
     "        any_provide = true;\n"
     "        break;\n"
     "      }\n"
     "    }\n"
     "    if (any_provide) {\n"
     "      const uint32_t offset = GetPhysicalAddress(heap_base_);\n"
     "      const uint32_t start =\n"
     "          rex::sat_sub(system_page_first * system_page_size_, host_address_offset()) + offset;\n"
     "      const uint32_t len = std::min(\n"
     "          rex::sat_sub(system_page_last * system_page_size_ + system_page_size_,\n"
     "                       host_address_offset()) +\n"
     "              offset - start,\n"
     "          heap_size_ - (start - offset));\n"
     "      // Copy the list: a provider may unlock the critical region.\n"
     "      auto providers = memory_->physical_memory_data_providers_;\n"
     "      for (auto provider : providers) {\n"
     "        provider->first(provider->second, global_lock_locked_once, start, len, is_write);\n"
     "      }\n"
     "      RestoreProviderPages(system_page_first, system_page_last);\n"
     "      provided = true;\n"
     "    }\n"
     "  }\n"
     "  if (!is_write) {\n"
     "    return provided;\n"
     "  }\n"
     "  // Check if watching any page, whether need to call the callback at all.\n"
     "  bool any_watched = false;\n"),
    ("  if (!any_watched) {\n"
     "    return false;\n"
     "  }\n"
     "\n"
     "  // Trigger callbacks.\n",
     "  if (!any_watched) {\n"
     "    return provided;\n"
     "  }\n"
     "\n"
     "  // Trigger callbacks.\n"),
])

# The private helper's declaration.
edit("include/rex/system/xmemory.h", [
    ("  void DisableDataProviders(uint32_t physical_address, uint32_t length);\n"
     "  void EnableAccessCallbacks(uint32_t physical_address, uint32_t length,\n",
     "  void DisableDataProviders(uint32_t physical_address, uint32_t length);\n"
     "  void RestoreProviderPages(uint32_t system_page_first, uint32_t system_page_last);\n"
     "  void EnableAccessCallbacks(uint32_t physical_address, uint32_t length,\n"),
])
print("done")
