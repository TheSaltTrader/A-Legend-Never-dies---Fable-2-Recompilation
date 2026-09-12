#include "fable2_autoskip.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <chrono>
#include <cstring>

#include <rex/logging.h>

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace fable2 {

// The SDK keeps these in rex:: and rex::input::; pulled in here so the driver
// below reads like the SDK's own drivers do.
using rex::X_STATUS;
using rex::X_RESULT;
using rex::input::X_INPUT_STATE;
using rex::input::X_INPUT_CAPABILITIES;
using rex::input::X_INPUT_VIBRATION;
using rex::input::X_INPUT_KEYSTROKE;

namespace {

// How long a chapter load keeps the skip armed. Long enough to cover an opening
// cinematic, short enough that it cannot still be running once the player has
// control - which in this game would mean holding attack.
constexpr double kArmedSeconds = 25.0;

// A press every ~12 frames at 60Hz. Fast enough to get through a prompt that
// wants several presses, slow enough to look like presses rather than a stuck
// button - some menus ignore a button that never releases.
constexpr double kPressPeriod = 0.20;
constexpr double kPressHold = 0.08;

constexpr uint16_t kButtonA = 0x1000;
constexpr uint16_t kButtonStart = 0x0010;

std::atomic<bool> g_enabled{false};
std::atomic<double> g_armed_until{-1.0};

double Now() {
  using clock = std::chrono::steady_clock;
  static const auto t0 = clock::now();
  return std::chrono::duration<double>(clock::now() - t0).count();
}

// Is the player actually touching a controller?
//
// Polled through XInput directly rather than through the input system, and that
// is the point: the synthetic pad is NOT an XInput device, so this sees only
// real hardware and cannot feed back on itself. Loaded dynamically because the
// version present differs across Windows installs and a missing DLL should cost
// the feature, not the process.
bool RealPadActive() {
  struct XInputGamepad {
    uint16_t buttons; uint8_t lt, rt; int16_t lx, ly, rx, ry;
  };
  struct XInputState { uint32_t packet; XInputGamepad pad; };
  using GetStateFn = uint32_t(WINAPI*)(uint32_t, XInputState*);

  static GetStateFn get_state = [] () -> GetStateFn {
    for (const wchar_t* dll : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
      if (HMODULE m = LoadLibraryW(dll))
        return reinterpret_cast<GetStateFn>(GetProcAddress(m, "XInputGetState"));
    }
    return nullptr;
  }();
  if (!get_state)
    return false;

  constexpr int16_t kStickDeadzone = 12000;   // well past drift
  for (uint32_t user = 0; user < 4; ++user) {
    XInputState st{};
    if (get_state(user, &st) != ERROR_SUCCESS)
      continue;
    const XInputGamepad& g = st.pad;
    if (g.buttons || g.lt > 40 || g.rt > 40 ||
        std::abs(int(g.lx)) > kStickDeadzone || std::abs(int(g.ly)) > kStickDeadzone ||
        std::abs(int(g.rx)) > kStickDeadzone || std::abs(int(g.ry)) > kStickDeadzone) {
      return true;
    }
  }
  return false;
}

// A single device, so its handle is a constant. Distinct from the SDK's own
// driver ids.
constexpr rex::input::DeviceId kSkipDevice =
    static_cast<rex::input::DeviceId>(0x4E473253);  // 'NG2S'

// FABLE2_PAD_SCRIPT="autoskip:20,40:b,41:down,42:a": presses on the synthetic
// pad at the given seconds after boot, 200 ms each, for scripted runs. Keys
// sent through the window depend on focus and on what the guest is polling
// at that instant; this is read by the guest exactly like a controller. An
// "autoskip:N" entry keeps the intro skipper's A/Start hammering until N s
// and nothing after - it would otherwise press A on the main menu too.
struct PadScript {
  std::vector<std::pair<double, uint16_t>> presses;
  double autoskip_until = 0.0;  // 0 = the skipper is left alone
  bool present = false;
  std::vector<bool> logged;
};

uint16_t ButtonMask(const std::string& name) {
  if (name == "a") return 0x1000;
  if (name == "b") return 0x2000;
  if (name == "x") return 0x4000;
  if (name == "y") return 0x8000;
  if (name == "start") return 0x0010;
  if (name == "back") return 0x0020;
  if (name == "up") return 0x0001;
  if (name == "down") return 0x0002;
  if (name == "left") return 0x0004;
  if (name == "right") return 0x0008;
  if (name == "lb") return 0x0100;
  if (name == "rb") return 0x0200;
  return 0;
}

PadScript ReadPadScript() {
  PadScript ps;
  const char* env = std::getenv("FABLE2_PAD_SCRIPT");
  if (!env || !*env) return ps;
  ps.present = true;
  std::string all = env;
  size_t start = 0;
  while (start < all.size()) {
    size_t comma = all.find(',', start);
    if (comma == std::string::npos) comma = all.size();
    std::string item = all.substr(start, comma - start);
    start = comma + 1;
    const size_t colon = item.find(':');
    if (colon == std::string::npos) continue;
    const std::string k = item.substr(0, colon), v = item.substr(colon + 1);
    if (k == "autoskip") {
      ps.autoskip_until = std::atof(v.c_str());
    } else {
      const uint16_t mask = ButtonMask(v);
      if (mask) ps.presses.emplace_back(std::atof(k.c_str()), mask);
    }
  }
  ps.logged.assign(ps.presses.size(), false);
  REXLOG_INFO("[padscript] {} scripted press(es); intro skipper until {:.0f} s", ps.presses.size(),
              ps.autoskip_until);
  return ps;
}

class AutoSkipDriver final : public rex::input::InputDriver {
 public:
  AutoSkipDriver() : InputDriver(nullptr, 0), script_(ReadPadScript()) {}
  ~AutoSkipDriver() override = default;

  X_STATUS Setup() override { return X_STATUS_SUCCESS; }

  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    rex::input::DeviceInfo info;
    info.id = kSkipDevice;
    info.name = "Auto-skip";
    // Synthetic, so the assignment logic knows it is not a physical pad and
    // does not count it as "a controller is plugged in".
    info.synthetic = true;
    out.push_back(info);
  }

  X_RESULT GetDeviceState(rex::input::DeviceId id, X_INPUT_STATE* out_state) override {
    if (id != kSkipDevice)
      return X_ERROR_DEVICE_NOT_CONNECTED;
    if (!out_state)
      return X_ERROR_BAD_ARGUMENTS;
    std::memset(out_state, 0, sizeof(*out_state));
    // The packet number has to move or the guest may treat the state as stale.
    out_state->packet_number = ++packet_;
    // Checked here, at the guest's own polling rate, so a real press stops the
    // synthetic ones within a frame rather than within a sampling interval.
    if (RealPadActive())
      NoteRealInput();
    if (script_.present) {
      const double now = Now();
      uint16_t buttons = 0;
      if (now < script_.autoskip_until && std::fmod(now, kPressPeriod) < kPressHold)
        buttons |= kButtonA | kButtonStart;
      for (size_t i = 0; i < script_.presses.size(); ++i) {
        const auto& [at, mask] = script_.presses[i];
        if (now >= at && now < at + 0.2) {
          buttons |= mask;
          if (!script_.logged[i]) {
            script_.logged[i] = true;
            REXLOG_INFO("[padscript] {:.1f} s: buttons {:#06x}", now, mask);
          }
        }
      }
      out_state->gamepad.buttons = buttons;
      return X_ERROR_SUCCESS;
    }
    if (!AutoSkipActive())
      return X_ERROR_SUCCESS;
    const double phase = std::fmod(Now(), kPressPeriod);
    if (phase < kPressHold)
      out_state->gamepad.buttons = kButtonA | kButtonStart;
    return X_ERROR_SUCCESS;
  }

  X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t,
                                 X_INPUT_CAPABILITIES* out_caps) override {
    if (id != kSkipDevice)
      return X_ERROR_DEVICE_NOT_CONNECTED;
    if (out_caps) {
      std::memset(out_caps, 0, sizeof(*out_caps));
      out_caps->type = 0x01;
      out_caps->sub_type = 0x01;
      out_caps->gamepad.buttons = script_.present ? 0xFFFF : (kButtonA | kButtonStart);
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT SetDeviceVibration(rex::input::DeviceId, X_INPUT_VIBRATION*) override {
    return X_ERROR_SUCCESS;  // nothing to rumble
  }

  X_RESULT GetDeviceKeystroke(rex::input::DeviceId, uint32_t,
                              X_INPUT_KEYSTROKE*) override {
    return X_ERROR_EMPTY;
  }

 private:
  uint32_t packet_ = 0;
  PadScript script_;
};

}  // namespace

void SetAutoSkipEnabled(bool enabled) {
  g_enabled.store(enabled, std::memory_order_release);
  if (!enabled)
    g_armed_until.store(-1.0, std::memory_order_release);
}

void ArmAutoSkip() {
  if (!g_enabled.load(std::memory_order_acquire))
    return;
  g_armed_until.store(Now() + kArmedSeconds, std::memory_order_release);
  REXLOG_INFO("Auto-skip: armed for {:.0f}s (chapter loading)", kArmedSeconds);
}

void NoteRealInput() {
  // Disarm rather than pause. Someone who reached for the pad during a
  // cinematic wants to watch it, and the next chapter will arm it again.
  if (g_armed_until.load(std::memory_order_acquire) > 0.0) {
    g_armed_until.store(-1.0, std::memory_order_release);
    REXLOG_INFO("Auto-skip: disarmed - the player pressed something");
  }
}

bool AutoSkipActive() {
  if (!g_enabled.load(std::memory_order_acquire))
    return false;
  const double until = g_armed_until.load(std::memory_order_acquire);
  return until > 0.0 && Now() < until;
}

std::unique_ptr<rex::input::InputDriver> MakeAutoSkipDriver() {
  return std::make_unique<AutoSkipDriver>();
}

}  // namespace fable2
