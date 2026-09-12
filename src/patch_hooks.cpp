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

#include <cstring>

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
