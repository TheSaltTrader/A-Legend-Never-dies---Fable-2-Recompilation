// Community game patch bodies. Declared in config/hooks/patches.toml, which
// carries the disassembly proving each address against our own image.
//
// Source: Xenia Canary's patch file for 4D5307F1 (Margen67, Guy, Pepper).
// Every patch is OFF by default - they are changes to how the game shipped,
// and the settings menu turns them on.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <utility>

#include <windows.h>

// Xenia's "60 FPS". The game selects a frame divider; 2 = 30 fps, 1 = 60.
REXCVAR_DEFINE_BOOL(fable2_60fps, false, "Fable II",
                    "Run at 60 fps instead of the shipped 30");

// Xenia's "1280x720 Resolution". The game renders 1120 wide and scales up.
REXCVAR_DEFINE_BOOL(fable2_720p, false, "Fable II",
                    "Render 1280 wide instead of the shipped 1120");

// Xenia's "Disable MSAA".
REXCVAR_DEFINE_BOOL(fable2_disable_msaa, false, "Fable II",
                    "Turn off multisampling in the game's render setup");

// Xenia's "Disable Texture Morphing" (Guy): the workaround for the hero's and
// the dog's black textures. Its own description warns that makeup stays
// bugged and morphs can look strange.
REXCVAR_DEFINE_BOOL(fable2_disable_texture_morph, false, "Fable II",
                    "Skip texture morphing - works around the black hero/dog "
                    "textures, but morphs can look wrong");

// Xenia's "High Tick Rate" (Guy): 15 Hz -> 30 Hz simulation tick.
REXCVAR_DEFINE_BOOL(fable2_high_tick_rate, false, "Fable II",
                    "Double the 15 Hz tick rate to 30 Hz - smoother in-game "
                    "UI and less input delay");

// Not a community patch: ours (2026-09-12). See patches.toml for the site.
REXCVAR_DEFINE_BOOL(fable2_skip_boot_logos, false, "Fable II",
                    "Start without the Microsoft and Lionhead logo videos");

// Ours (2026-09-12): the audio loader's 400 ms sleep per sound bank.
REXCVAR_DEFINE_BOOL(fable2_fast_bank_load, false, "Fable II",
                    "Shorten the 400 ms sleep the game takes after each sound bank");

// Ours (2026-09-12): the render thread's GPU progress poll yields instead of
// spinning through no-ops. See patches.toml for the profile that found it.
REXCVAR_DEFINE_BOOL(fable2_gpu_wait_yield, false, "Fable II",
                    "Yield the CPU while the render thread waits for the GPU");

// Ours (2026-09-13): vertical field of view in degrees, 60 = as shipped.
// Applied by the projection-builder hook every frame, so it is live.
REXCVAR_DEFINE_INT32(fable2_fov, 60, "Fable II",
                     "Vertical field of view in degrees (60 = the game's own)");

namespace {

// Each patch logs the first time it fires, with the value it replaced. A patch
// that silently does nothing because its site was never reached is the failure
// mode worth catching, and one line per boot is the cheapest way to see it.
void LogOnce(bool& logged, const char* what, uint32_t from, uint32_t to) {
  if (logged) return;
  logged = true;
  REXLOG_INFO("Patch: {} {} -> {}", what, from, to);
}

}  // namespace

// TU1: 0x82BA3058, after `li r11, 2` in the 3/2/1 selector (disc: 0x82B9C8E8).
void fable2PatchFrameRate(PPCRegister& r11) {
  if (!REXCVAR_GET(fable2_60fps)) return;
  static bool logged = false;
  LogOnce(logged, "frame divider", r11.u32, 1);
  r11.u32 = 1;
}

// TU1: 0x82BA3018, after `lwz r11, 0x351C(r31)` - the title update also reads
// the divider from a field, and Xenia Canary's TU1 patch replaces that load
// with `li r11, 1`. Same effect here, one comparison later.
void fable2PatchFrameRateLoad(PPCRegister& r11) {
  if (!REXCVAR_GET(fable2_60fps)) return;
  static bool logged = false;
  LogOnce(logged, "frame divider (loaded)", r11.u32, 1);
  r11.u32 = 1;
}

// 0x8238DF58, after `li r11, 0x460` (1120).
void fable2PatchRenderWidth(PPCRegister& r11) {
  if (!REXCVAR_GET(fable2_720p)) return;
  static bool logged = false;
  LogOnce(logged, "render width", r11.u32, 1280);
  r11.u32 = 1280;
}

// 0x8238DF3C, after `li r9, 2` (sample count).
void fable2PatchMsaa(PPCRegister& r9) {
  if (!REXCVAR_GET(fable2_disable_msaa)) return;
  static bool logged = false;
  LogOnce(logged, "MSAA samples", r9.u32, 1);
  r9.u32 = 1;
}

// 0x8220EF0C, BEFORE `cmplwi cr6, r18, 0`.
//
// Xenia rewrites the following `beq` into an unconditional branch. A midasm
// hook cannot change control flow, so this forces the value the compare is
// about to test: r18 = 0 sets cr6.eq, and the game's own `beq` then takes the
// skip. The branch stays exactly as the game wrote it.
void fable2PatchTextureMorph(PPCRegister& r18) {
  if (!REXCVAR_GET(fable2_disable_texture_morph)) return;
  if (r18.u32 == 0) return;  // already taking the skip
  static bool logged = false;
  LogOnce(logged, "texture morph guard", r18.u32, 0);
  r18.u32 = 0;
}

// 0x8233AEB4, after `stfd f0, -0x6af0(r8)`.
//
// The instruction just wrote the tick period as a double. Xenia NOPs the store
// and presets the constant to 30.0; we let the store happen and then overwrite
// it, which reaches the same state and re-applies if the store runs again.
void fable2PatchTickRate(PPCRegister& r8) {
  if (!REXCVAR_GET(fable2_high_tick_rate)) return;
  auto* memory = REX_KERNEL_MEMORY();
  if (!memory) return;

  // The store's own effective address, computed the same way the instruction
  // did - not a hard-coded .data address. The displacement is the store's:
  // disc `stfd f0, -0x6AF0(r8)` at 0x8233AEB4 (double at 0x83319510); title
  // update 1 `stfd f0, -0x69A0(r8)` at 0x8231091C (double at 0x83319660) -
  // see config/hooks/patches.toml for how it was found.
  const uint32_t va = r8.u32 - 0x69A0;
  auto* slot = memory->TranslateVirtual<uint8_t*>(va);
  if (!slot) return;

  // Guest doubles are big-endian.
  const double target = 30.0;
  uint64_t bits;
  std::memcpy(&bits, &target, sizeof(bits));
  uint8_t be[8];
  for (int i = 0; i < 8; ++i) be[i] = static_cast<uint8_t>(bits >> (56 - i * 8));

  if (std::memcmp(slot, be, 8) == 0) return;  // already 30.0

  uint64_t was_bits = 0;
  for (int i = 0; i < 8; ++i) was_bits = (was_bits << 8) | slot[i];
  double was;
  std::memcpy(&was, &was_bits, sizeof(was));

  std::memcpy(slot, be, 8);

  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("Patch: tick rate {} Hz -> {} Hz (guest 0x{:08X})", was, target,
                va);
  }
}

// Boot logos. r3 is sub_8229B1B8's answer for the FIRST entry of the boot
// movie list: non-zero = "not the end marker, play it". Zero sends the game
// down its own empty-list path, so the two logo videos are never opened.
void fable2PatchSkipBootLogos(PPCRegister& r3) {
  if (!REXCVAR_GET(fable2_skip_boot_logos)) return;
  static bool logged = false;
  LogOnce(logged, "boot logo list", r3.u32, 0);
  r3.u32 = 0;
}

// GPU progress poll: one scheduler slice per delay-loop iteration instead
// of eight no-ops. SwitchToThread returns at once when nothing else is
// ready, so an idle machine loses no latency; a busy one stops seeing a core
// pinned by a wait. r11 is the loop counter and is left alone.
void fable2PatchGpuWaitYield(PPCRegister& r11) {
  (void)r11;
  if (!REXCVAR_GET(fable2_gpu_wait_yield)) return;
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("Patch: GPU progress poll yields instead of spinning");
  }
  SwitchToThread();
}

namespace {

// The host thread's description, read once per thread: the runtime names
// guest threads "<name> (F8xxxxxx)" and the audio loader is
// "Front end audio loading". Cached thread-locally so a sleep costs no
// syscall after the first.
const std::string& CurrentThreadName() {
  thread_local std::string name;
  thread_local bool read = false;
  if (!read) {
    read = true;
    typedef HRESULT(WINAPI * GetDescFn)(HANDLE, PWSTR*);
    static GetDescFn get_desc = reinterpret_cast<GetDescFn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));
    PWSTR desc = nullptr;
    if (get_desc && SUCCEEDED(get_desc(GetCurrentThread(), &desc)) && desc) {
      char narrow[256] = {};
      WideCharToMultiByte(CP_UTF8, 0, desc, -1, narrow, sizeof(narrow), nullptr, nullptr);
      LocalFree(desc);
      name = narrow;
    }
  }
  return name;
}

}  // namespace

// Sleep(ms) at its entry. 400 ms on an audio-loading thread becomes 1 ms:
// the read it follows took a millisecond, and nothing waits on the other
// side of the sleep but the next bank. Everything else 100 ms or longer is
// logged once per (thread, duration) and left alone.
void fable2PatchBankLoadSleep(PPCRegister& r3) {
  if (!REXCVAR_GET(fable2_fast_bank_load)) return;
  const uint32_t ms = r3.u32;
  if (ms == 0xFFFFFFFFu || ms < 100) return;
  const std::string& thread = CurrentThreadName();
  const bool audio_loader = thread.find("audio loading") != std::string::npos;
  if (audio_loader && ms == 400) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      REXLOG_INFO("Patch: sound-bank load sleep 400 ms -> 1 ms on '{}'", thread);
    }
    r3.u32 = 1;
    return;
  }
  static std::mutex mutex;
  static std::set<std::pair<std::string, uint32_t>> seen;
  std::lock_guard<std::mutex> lock(mutex);
  if (seen.size() < 40 && seen.insert({thread, ms}).second) {
    REXLOG_INFO("Sleep {} ms on '{}' (first time; left alone)", ms, thread);
  }
}

// Field of view. sub_821B4B48 has just blended this frame's horizontal (f8)
// and vertical (f30) angles, radians, and is about to halve them and take the
// tan of each for the perspective matrix (see patches.toml). Scale the
// vertical angle by setting/60 and re-derive the horizontal one from the same
// tan ratio, so the aspect ratio the game chose is untouched. Every camera
// goes through here (gameplay, cutscenes, the zoomed dialogue shots), and
// scaling rather than replacing keeps their relative framing.
void fable2PatchFieldOfView(PPCRegister& f8, PPCRegister& f30) {
  const int degrees = std::clamp(REXCVAR_GET(fable2_fov), 40, 120);
  if (degrees == 60) return;
  const double fx = f8.f64, fy = f30.f64;
  // A nonsense angle (uninitialised camera, the odd frame during a load) is
  // left alone rather than turned into a bigger nonsense.
  if (!(fx > 0.01 && fx < 3.0 && fy > 0.01 && fy < 3.0)) return;
  const double ratio = std::tan(fx * 0.5) / std::tan(fy * 0.5);  // aspect
  const double ny = std::clamp(fy * (degrees / 60.0), 0.05, 3.0);
  const double nx = 2.0 * std::atan(ratio * std::tan(ny * 0.5));
  f8.f64 = nx;
  f30.f64 = ny;
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("Patch: field of view {} deg: vertical {:.4f} -> {:.4f} rad, horizontal {:.4f} -> {:.4f} rad",
                degrees, fy, ny, fx, nx);
  }
}
