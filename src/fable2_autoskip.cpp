#include "fable2_autoskip.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <chrono>
#include <cctype>
#include <cstring>

#include <rex/logging.h>

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>

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

// ---------------------------------------------------------------------------
// The pad FILE: a live command queue an external tool writes beside the
// executable. FABLE2_PAD_SCRIPT (above) is fixed at launch and fine for a
// scripted run; a tool that looks at the screen and decides what to press
// next needs a channel that is open while the game runs. A file is the
// simplest thing both sides can do without a driver, a pipe or a focus
// fight: the tool writes lines, the game reads and deletes them.
//
// The game announces the channel with pad_script.accepts (created at start,
// removed at exit) so a tool never writes into a folder that will not read.
struct PadCommand {
  uint16_t buttons = 0;
  float lx = 0, ly = 0, rx = 0, ry = 0;   // -1..1
  uint8_t lt = 0, rt = 0;                  // 0..255
  double seconds = 0.2;                    // how long the state is held
  bool wait_only = false;                  // "wait:N": nothing pressed
  bool release = false;                    // "release": clear and stop
  int dump_frames = 0;                     // "dump:N": the plugin's per-draw dump
  std::string text;                        // for the log
};

// "dump:N" - ask the GPU plugin for one line per draw over the next N guest
// frames (gpu_draw_dump_frames), into draw_dump_<serial>.txt beside the exe.
// A diagnostic for finding which draws make up a transition (the ultrawide
// pause-menu dissolve): put it on the line before the press.
void StartDrawDump(int frames) {
  static int serial = 0;
  ++serial;
  const auto path = rex::filesystem::GetExecutableFolder() /
                    ("draw_dump_" + std::to_string(serial) + ".txt");
  rex::cvar::SetFlagByName("gpu_draw_dump_file", path.string());
  rex::cvar::SetFlagByName("gpu_draw_dump_frames", std::to_string(frames));
  REXLOG_INFO("[padfile] draw dump {} frames -> {}", frames, path.string());
}

std::filesystem::path PadFilePath() {
  if (const char* env = std::getenv("FABLE2_PAD_FILE"); env && *env)
    return std::filesystem::path(env);
  return rex::filesystem::GetExecutableFolder() / "pad_script.txt";
}

std::filesystem::path PadAcceptsPath() {
  return PadFilePath().parent_path() / "pad_script.accepts";
}

bool ParsePadCommand(const std::string& raw, PadCommand& out) {
  std::string line = raw;
  const size_t hash = line.find('#');
  if (hash != std::string::npos) line.erase(hash);
  while (!line.empty() && (line.back() == ' ' || line.back() == '\r' || line.back() == '\t'))
    line.pop_back();
  size_t start = 0;
  while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) ++start;
  line.erase(0, start);
  if (line.empty()) return false;
  out = PadCommand{};
  out.text = line;
  for (char& c : line) c = char(std::tolower(static_cast<unsigned char>(c)));
  std::vector<std::string> parts;
  {
    size_t p = 0;
    while (true) {
      const size_t colon = line.find(':', p);
      parts.push_back(line.substr(p, colon == std::string::npos ? std::string::npos : colon - p));
      if (colon == std::string::npos) break;
      p = colon + 1;
    }
  }
  const std::string& head = parts[0];
  auto secs_at = [&](size_t i, double dflt) {
    return (parts.size() > i && !parts[i].empty()) ? std::atof(parts[i].c_str()) : dflt;
  };
  if (head == "release") { out.release = true; out.seconds = 0; return true; }
  if (head == "dump") {
    out.wait_only = true;
    out.seconds = 0;
    out.dump_frames = int(secs_at(1, 40));
    return true;
  }
  if (head == "wait") { out.wait_only = true; out.seconds = secs_at(1, 0.5); return true; }
  if (head == "l" || head == "r") {
    if (parts.size() < 2) return false;
    const size_t comma = parts[1].find(',');
    if (comma == std::string::npos) return false;
    const float x = float(std::atof(parts[1].substr(0, comma).c_str()));
    const float y = float(std::atof(parts[1].substr(comma + 1).c_str()));
    auto clamp = [](float v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); };
    if (head == "l") { out.lx = clamp(x); out.ly = clamp(y); } else { out.rx = clamp(x); out.ry = clamp(y); }
    out.seconds = secs_at(2, 0.5);
    return true;
  }
  if (head == "lt" || head == "rt") {
    if (head == "lt") out.lt = 255; else out.rt = 255;
    out.seconds = secs_at(1, 0.2);
    return true;
  }
  const uint16_t mask = ButtonMask(head);
  if (!mask) return false;
  out.buttons = mask;
  out.seconds = secs_at(1, 0.2);
  return true;
}

class PadFile {
 public:
  PadFile() : path_(PadFilePath()), accepts_(PadAcceptsPath()) {
    std::error_code ec;
    // A file left by an earlier session must not fire now.
    std::filesystem::remove(path_, ec);
    std::ofstream a(accepts_, std::ios::trunc);
    // The PID is the liveness check: the port leaves by a hard exit, so this
    // file outlives the process, and a tool must not write to a dead folder.
    a << "fable2recomp pad script v1 pid=" << GetCurrentProcessId() << "\n"
         "commands, one per line, in pad_script.txt beside this file; consumed when read\n"
         "a b x y start back up down left right lb rb [:hold_s] | l:x,y[:s] r:x,y[:s] | lt[:s] rt[:s] | wait:s | release\n";
    if (a)
      REXLOG_INFO("[padfile] listening: {}", path_.string());
  }
  ~PadFile() {
    std::error_code ec;
    std::filesystem::remove(accepts_, ec);
  }

  // Called from GetDeviceState: at most ten stats a second.
  void Poll(double now) {
    if (now - last_poll_ < 0.1) return;
    last_poll_ = now;
    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) return;
    std::vector<std::string> lines;
    {
      std::ifstream in(path_);
      std::string line;
      while (std::getline(in, line)) lines.push_back(line);
    }
    std::filesystem::remove(path_, ec);
    int accepted = 0, rejected = 0;
    for (const std::string& l : lines) {
      PadCommand c;
      if (!ParsePadCommand(l, c)) {
        if (!l.empty() && l[0] != '#') ++rejected;
        continue;
      }
      if (c.release) { queue_.clear(); current_ = PadCommand{}; current_until_ = 0; }
      queue_.push_back(c);
      ++accepted;
    }
    if (accepted || rejected)
      REXLOG_INFO("[padfile] {} command(s) queued{}", accepted,
                  rejected ? (", " + std::to_string(rejected) + " not understood") : std::string());
  }

  // The state to report this poll. Advances the queue on its own clock.
  void Apply(double now, X_INPUT_STATE* st) {
    if (now >= current_until_) {
      current_ = PadCommand{};
      if (!queue_.empty() && now >= gap_until_) {
        current_ = queue_.front();
        queue_.pop_front();
        current_until_ = now + current_.seconds;
        gap_until_ = current_until_ + 0.1;
        if (current_.dump_frames > 0) StartDrawDump(current_.dump_frames);
        if (!current_.wait_only && !current_.release)
          REXLOG_INFO("[padfile] {} for {:.2f} s", current_.text, current_.seconds);
      }
    }
    if (current_.wait_only || current_.release) return;
    // The SDK's fields are big-endian wrappers: read, combine, assign.
    const uint16_t cur_buttons = st->gamepad.buttons;
    st->gamepad.buttons = uint16_t(cur_buttons | current_.buttons);
    const uint8_t cur_lt = st->gamepad.left_trigger, cur_rt = st->gamepad.right_trigger;
    st->gamepad.left_trigger = uint8_t(std::max(cur_lt, current_.lt));
    st->gamepad.right_trigger = uint8_t(std::max(cur_rt, current_.rt));
    auto axis = [](float v) { return int16_t(v * 32767.0f); };
    if (current_.lx != 0 || current_.ly != 0) { st->gamepad.thumb_lx = axis(current_.lx); st->gamepad.thumb_ly = axis(current_.ly); }
    if (current_.rx != 0 || current_.ry != 0) { st->gamepad.thumb_rx = axis(current_.rx); st->gamepad.thumb_ry = axis(current_.ry); }
  }

  // A person on the real pad takes over: nothing queued survives.
  void ClearForRealInput() {
    if (queue_.empty() && current_until_ == 0) return;
    queue_.clear();
    current_ = PadCommand{};
    current_until_ = 0;
    REXLOG_INFO("[padfile] cleared - the player pressed something");
  }

  bool Busy() const { return !queue_.empty() || current_until_ > last_poll_; }

 private:
  std::filesystem::path path_, accepts_;
  std::deque<PadCommand> queue_;
  PadCommand current_;
  double current_until_ = 0, gap_until_ = 0, last_poll_ = -1.0;
};

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
    const double file_now = Now();
    pad_file_.Poll(file_now);
    if (RealPadActive()) {
      NoteRealInput();
      pad_file_.ClearForRealInput();
    }
    pad_file_.Apply(file_now, out_state);
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
      const uint16_t have = out_state->gamepad.buttons;
      out_state->gamepad.buttons = uint16_t(have | buttons);
      return X_ERROR_SUCCESS;
    }
    if (!AutoSkipActive())
      return X_ERROR_SUCCESS;
    const double phase = std::fmod(Now(), kPressPeriod);
    if (phase < kPressHold) {
      const uint16_t have = out_state->gamepad.buttons;
      out_state->gamepad.buttons = uint16_t(have | kButtonA | kButtonStart);
    }
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
      out_caps->gamepad.buttons = 0xFFFF;
      out_caps->gamepad.left_trigger = 0xFF;
      out_caps->gamepad.right_trigger = 0xFF;
      out_caps->gamepad.thumb_lx = out_caps->gamepad.thumb_ly = int16_t(0x7FFF);
      out_caps->gamepad.thumb_rx = out_caps->gamepad.thumb_ry = int16_t(0x7FFF);
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
  PadFile pad_file_;
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
