#include "fable2_camera.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>

#include <windows.h>

#include <rex/logging.h>
#include <rex/system/kernel_state.h>

namespace fable2 {

namespace {
constexpr uint32_t kFovGuestAddr = 0x82101034;  // vertical FOV, radians, be32

std::atomic<int> g_fov_degrees{kFovDefaultDegrees};
std::atomic<bool> g_keeper_started{false};

// The constant sits in a demand-paged region: at startup its page is not
// committed, and the game itself faults reading nearby addresses before it
// touches them (seen in a log, 2026-09-13). So every write checks the host
// page is committed and writable first - a write to an uncommitted page would
// fault THIS process. Returns true if the value is now in place.
bool WriteFovLocked(int degrees) {
  auto* memory = REX_KERNEL_MEMORY();
  if (!memory) return false;
  auto* slot = memory->TranslateVirtual<uint8_t*>(kFovGuestAddr);
  if (!slot) return false;

  MEMORY_BASIC_INFORMATION mbi{};
  if (!VirtualQuery(slot, &mbi, sizeof(mbi))) return false;
  if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) ||
      (mbi.Protect & PAGE_NOACCESS))
    return false;

  const float radians = static_cast<float>(degrees) * 3.14159265358979f / 180.0f;
  uint32_t bits;
  std::memcpy(&bits, &radians, sizeof(bits));
  const uint8_t be[4] = {
      static_cast<uint8_t>(bits >> 24), static_cast<uint8_t>(bits >> 16),
      static_cast<uint8_t>(bits >> 8), static_cast<uint8_t>(bits)};
  if (std::memcmp(slot, be, 4) == 0) return true;  // already set

  // The constant sits in a read-only page. Flip it writable for the store and
  // put the original protection back, the way the value was first found by
  // poking the running game. Guest code only ever reads it, so nothing races
  // the four bytes.
  const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE |
                         PAGE_EXECUTE_WRITECOPY;
  DWORD old = 0;
  const bool need_flip = !(mbi.Protect & writable);
  // PAGE_READWRITE, not EXECUTE_READWRITE: the constant sits in a data page
  // (PAGE_READONLY), and asking for execute rights is refused here with
  // ERROR_INVALID_PARAMETER, likely a dynamic-code mitigation. We only store
  // four bytes, so plain read-write is both enough and allowed.
  const DWORD want = (mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE))
                         ? PAGE_EXECUTE_READWRITE
                         : PAGE_READWRITE;
  if (need_flip && !VirtualProtect(slot, 4, want, &old)) return false;
  std::memcpy(slot, be, 4);
  if (need_flip) VirtualProtect(slot, 4, old, &old);
  return true;
}

// Keeps the chosen field of view applied. The constant is only reachable once
// the game is rendering, and the game re-initialises it across some loads, so
// one write at startup is not enough - this re-applies every couple of seconds
// (a no-op when the bytes already match, and silent while the page is absent).
void FovKeeper() {
  int announced = 0;  // the degrees last logged, 0 = nothing yet
  for (;;) {
    const int deg = g_fov_degrees.load(std::memory_order_relaxed);
    if (deg != kFovDefaultDegrees && WriteFovLocked(deg) && announced != deg) {
      announced = deg;
      REXLOG_INFO("Field of view applied: {} deg", deg);
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
}
}  // namespace

int ApplyFieldOfView(int degrees) {
  degrees = std::clamp(degrees, kFovMinDegrees, kFovMaxDegrees);
  g_fov_degrees.store(degrees, std::memory_order_relaxed);
  WriteFovLocked(degrees);  // immediate when the page is present (the slider path)
  if (!g_keeper_started.exchange(true)) {
    std::thread(FovKeeper).detach();
  }
  return degrees;
}

}  // namespace fable2
