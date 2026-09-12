#include "fable2_platform.h"

#include <algorithm>

#include <vector>

#include <windows.h>
// windows.h first, then the shell headers.
#include <shlobj.h>
#include <shobjidl.h>

#include <cstdio>
#include <cwchar>

#include <rex/logging.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace fable2 {

void SetAppUserModelId() {
  typedef HRESULT(WINAPI * SetAumidFn)(PCWSTR);
  HMODULE shell = LoadLibraryW(L"shell32.dll");
  auto fn = shell ? reinterpret_cast<SetAumidFn>(
                        GetProcAddress(shell, "SetCurrentProcessExplicitAppUserModelID"))
                  : nullptr;
  if (fn) fn(L"TheSaltTrader.Fable2Recomp");
}

void ApplyWindowIcon(void* native_window) {
  HWND hwnd = static_cast<HWND>(native_window);
  if (!hwnd) {
    REXLOG_WARN("Window icon: no native window handle");
    return;
  }
  HMODULE exe = GetModuleHandleW(nullptr);
  // Resource id 1 - see resources/fable2.rc. Two sizes: the small one is the
  // title bar and the taskbar button, the big one Alt-Tab and the switcher.
  // Not "small": <rpcndr.h> defines that word as a macro.
  HICON icon_big =
      static_cast<HICON>(LoadImageW(exe, MAKEINTRESOURCEW(1), IMAGE_ICON, 256, 256, 0));
  HICON icon_small =
      static_cast<HICON>(LoadImageW(exe, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0));
  if (!icon_big && !icon_small) {
    REXLOG_WARN("Window icon: resource 1 not found in the executable ({})", GetLastError());
    return;
  }
  if (icon_big) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon_big));
  if (icon_small)
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon_small));
  REXLOG_INFO("Window icon applied ({}{})", icon_big ? "big" : "",
              icon_small ? (icon_big ? "+small" : "small") : "");
}

bool PageCommitted(const void* p) {
  MEMORY_BASIC_INFORMATION mbi{};
  if (!p || !VirtualQuery(p, &mbi, sizeof(mbi)))
    return false;
  return mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_NOACCESS) &&
         !(mbi.Protect & PAGE_GUARD);
}

void RaiseTimerResolution() {
  // NtSetTimerResolution rather than timeBeginPeriod: it goes to 0.5 ms where
  // winmm stops at 1, and it needs no extra import library.
  typedef LONG(NTAPI * NtSetTimerResolutionFn)(ULONG, BOOLEAN, PULONG);
  typedef LONG(NTAPI * NtQueryTimerResolutionFn)(PULONG, PULONG, PULONG);
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  auto set_res = ntdll ? reinterpret_cast<NtSetTimerResolutionFn>(
                             GetProcAddress(ntdll, "NtSetTimerResolution"))
                       : nullptr;
  auto query_res = ntdll ? reinterpret_cast<NtQueryTimerResolutionFn>(
                               GetProcAddress(ntdll, "NtQueryTimerResolution"))
                         : nullptr;
  ULONG min_res = 0, max_res = 0, before = 0, actual = 0;
  if (query_res) query_res(&min_res, &max_res, &before);
  if (set_res) set_res(max_res ? max_res : 5000, TRUE, &actual);

  // Windows 11 coalesces timers for processes it does not consider foreground
  // work. A game with its window in front usually is, but say so explicitly.
  struct PowerThrottling {
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
  } throttling{1, 0x4 /* PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION */, 0};
  typedef BOOL(WINAPI * SetProcessInformationFn)(HANDLE, int, LPVOID, DWORD);
  HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
  auto set_info = k32 ? reinterpret_cast<SetProcessInformationFn>(
                            GetProcAddress(k32, "SetProcessInformation"))
                      : nullptr;
  const BOOL throttling_ok =
      set_info ? set_info(GetCurrentProcess(), 4 /* ProcessPowerThrottling */, &throttling,
                          sizeof(throttling))
               : FALSE;
  REXLOG_INFO("Timer resolution: {:.2f} ms before, {:.2f} ms granted (finest {:.2f}); "
              "timer coalescing opt-out {}",
              before / 10000.0, actual / 10000.0, max_res / 10000.0,
              throttling_ok ? "accepted" : "not available");
}

namespace {

std::wstring Widen(const std::string& s) {
  if (s.empty())
    return {};
  const int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                       static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(need), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                      out.data(), need);
  return out;
}

// COM is already initialised on this thread by SDL, and it picked the
// apartment. Asking for a different model returns RPC_E_CHANGED_MODE, which is
// not a failure - it means someone else got there first, and we must not
// balance it with CoUninitialize.
class ComScope {
 public:
  ComScope() {
    const HRESULT hr = CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    owns_ = SUCCEEDED(hr);
  }
  ~ComScope() {
    if (owns_)
      CoUninitialize();
  }

 private:
  bool owns_ = false;
};

std::optional<std::filesystem::path> RunDialog(
    const std::string& title, bool pick_folder,
    const std::vector<FileFilter>& filters,
    const std::filesystem::path& start_in) {
  ComScope com;

  IFileOpenDialog* dialog = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog));
  if (FAILED(hr) || dialog == nullptr) {
    REXLOG_WARN("File dialog: CoCreateInstance failed (0x{:08X})",
                static_cast<uint32_t>(hr));
    return std::nullopt;
  }

  const std::wstring wide_title = Widen(title);
  dialog->SetTitle(wide_title.c_str());

  DWORD options = 0;
  dialog->GetOptions(&options);
  options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
  if (pick_folder)
    options |= FOS_PICKFOLDERS;
  dialog->SetOptions(options);

  // The filter spec array points at these strings, so they have to outlive the
  // Show() call - build them all before taking any pointer.
  std::vector<std::wstring> filter_storage;
  std::vector<COMDLG_FILTERSPEC> specs;
  filter_storage.reserve(filters.size() * 2);
  for (const auto& f : filters) {
    filter_storage.push_back(Widen(f.name));
    filter_storage.push_back(Widen(f.pattern));
  }
  for (size_t i = 0; i < filters.size(); ++i) {
    specs.push_back({filter_storage[i * 2].c_str(), filter_storage[i * 2 + 1].c_str()});
  }
  if (!specs.empty())
    dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());

  std::error_code ec;
  if (!start_in.empty() && std::filesystem::exists(start_in, ec)) {
    const auto folder =
        std::filesystem::is_directory(start_in, ec) ? start_in : start_in.parent_path();
    IShellItem* item = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(folder.wstring().c_str(), nullptr,
                                              IID_PPV_ARGS(&item)))) {
      dialog->SetFolder(item);
      item->Release();
    }
  }

  // Parent to the game window so the dialog is modal to it rather than
  // appearing behind. GetActiveWindow is correct here: this only ever runs on
  // the UI thread, from inside the dialog's own draw.
  hr = dialog->Show(GetActiveWindow());
  if (FAILED(hr)) {
    dialog->Release();
    return std::nullopt;  // HRESULT_FROM_WIN32(ERROR_CANCELLED) is the usual one
  }

  IShellItem* result = nullptr;
  std::optional<std::filesystem::path> picked;
  if (SUCCEEDED(dialog->GetResult(&result)) && result != nullptr) {
    PWSTR path = nullptr;
    if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr) {
      picked = std::filesystem::path(path);
      CoTaskMemFree(path);
    }
    result->Release();
  }
  dialog->Release();
  return picked;
}

}  // namespace

std::optional<std::filesystem::path> PickFile(
    const std::string& title, const std::vector<FileFilter>& filters,
    const std::filesystem::path& start_in) {
  return RunDialog(title, /*pick_folder=*/false, filters, start_in);
}

std::optional<std::filesystem::path> PickFolder(
    const std::string& title, const std::filesystem::path& start_in) {
  return RunDialog(title, /*pick_folder=*/true, {}, start_in);
}

bool ShiftHeld() { return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0; }

namespace {

BOOL CALLBACK CollectMonitor(HMONITOR handle, HDC, LPRECT, LPARAM param) {
  auto* out = reinterpret_cast<std::vector<MonitorInfo>*>(param);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!::GetMonitorInfoW(handle, &info))
    return TRUE;  // skip this one, keep enumerating
  MonitorInfo m;
  // rcWork, not rcMonitor: the taskbar is not somewhere a window can put its
  // footer, and the footer is exactly what went missing.
  m.full_width = int(info.rcMonitor.right - info.rcMonitor.left);
  m.full_height = int(info.rcMonitor.bottom - info.rcMonitor.top);
  m.width = int(info.rcWork.right - info.rcWork.left);
  m.height = int(info.rcWork.bottom - info.rcWork.top);
  m.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;

  // Per-monitor DPI, which really does differ per display: 100%, 125% and 150%
  // across the three on the machine this was written against.
  // Resolved at runtime rather than linked: GetDpiForMonitor lives in
  // shcore.dll, which is not in the import libraries this links against, and
  // adding a library for one call is a worse trade than one GetProcAddress.
  // Falling back to 96 DPI when it is unavailable is correct - that is what a
  // machine without per-monitor scaling has.
  using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
  static const auto get_dpi = [] {
    HMODULE shcore = ::LoadLibraryW(L"shcore.dll");
    return shcore ? reinterpret_cast<GetDpiForMonitorFn>(
                        ::GetProcAddress(shcore, "GetDpiForMonitor"))
                  : nullptr;
  }();
  UINT dpi_x = 96, dpi_y = 96;
  if (get_dpi != nullptr &&
      SUCCEEDED(get_dpi(handle, 0 /* MDT_EFFECTIVE_DPI */, &dpi_x, &dpi_y)) &&
      dpi_x > 0) {
    m.scale = float(dpi_x) / 96.0f;
  }
  out->push_back(m);
  return TRUE;
}

}  // namespace

std::vector<MonitorInfo> Monitors() {
  std::vector<MonitorInfo> found;
  ::EnumDisplayMonitors(nullptr, nullptr, CollectMonitor,
                        reinterpret_cast<LPARAM>(&found));
  // PRIMARY first, then the rest in enumeration order - SDL's convention, and
  // established by testing rather than assumed. On the three-display machine
  // this was written against, Windows numbers them 4K=1, 3440=2, primary=3,
  // while the runtime's own indices were 1=primary, 2=4K, 3=3440. Neither
  // Windows' numbering nor left-to-right position matches; this does.
  std::stable_sort(found.begin(), found.end(),
                   [](const MonitorInfo& a, const MonitorInfo& b) {
                     return a.primary && !b.primary;
                   });
  for (size_t i = 0; i < found.size(); ++i)
    found[i].index = int(i);
  return found;
}

float SystemScale() {
  for (const auto& m : Monitors())
    if (m.primary)
      return m.scale;
  return 1.0f;
}

bool MonitorWorkArea(int index, int& width, int& height, float& scale) {
  const auto all = Monitors();
  if (all.empty())
    return false;
  size_t at = (index >= 0 && size_t(index) < all.size()) ? size_t(index) : 0;
  if (index < 0 || size_t(index) >= all.size()) {
    for (size_t i = 0; i < all.size(); ++i)
      if (all[i].primary) at = i;
  }
  width = all[at].width;
  height = all[at].height;
  scale = all[at].scale;
  return width > 0 && height > 0;
}

bool ThisProcessIsForeground() {
  HWND foreground = ::GetForegroundWindow();
  if (foreground == nullptr)
    return false;
  DWORD pid = 0;
  ::GetWindowThreadProcessId(foreground, &pid);
  return pid == ::GetCurrentProcessId();
}

std::string FormatBytes(uint64_t bytes) {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double value = static_cast<double>(bytes);
  size_t unit = 0;
  while (value >= 1024.0 && unit + 1 < std::size(units)) {
    value /= 1024.0;
    ++unit;
  }
  char buf[64];
  std::snprintf(buf, sizeof(buf), unit == 0 ? "%.0f %s" : "%.1f %s", value, units[unit]);
  return buf;
}

}  // namespace fable2
