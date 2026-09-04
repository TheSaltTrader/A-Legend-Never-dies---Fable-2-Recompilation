#include "fable2_platform.h"

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
