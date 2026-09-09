#include <windows.h>
#include <commctrl.h>

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace {
  struct icon_position_t {
    std::wstring name;
    LONG x {};
    LONG y {};
  };

  std::wstring
  from_utf8(const std::string &value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) throw std::runtime_error("Invalid UTF-8 icon name");
    std::wstring converted(static_cast<std::size_t>(size), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), converted.data(), size)) {
      throw std::runtime_error("Unable to convert icon name");
    }
    return converted;
  }

  BOOL CALLBACK
  find_desktop_view(HWND window, LPARAM param) {
    auto *found = reinterpret_cast<HWND *>(param);
    const auto shell = FindWindowExW(window, nullptr, L"SHELLDLL_DefView", nullptr);
    if (shell) {
      *found = FindWindowExW(shell, nullptr, L"SysListView32", nullptr);
    }
    return *found == nullptr;
  }

  HWND
  desktop_view() {
    HWND found = nullptr;
    EnumWindows(find_desktop_view, reinterpret_cast<LPARAM>(&found));
    if (!found) throw std::runtime_error("Desktop icon view is unavailable");
    return found;
  }

  LRESULT
  message(HWND view, UINT id, WPARAM wparam, LPARAM lparam) {
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(view, id, wparam, lparam, SMTO_ABORTIFHUNG, 1500, &result)) {
      throw std::runtime_error("Desktop icon view did not respond");
    }
    return static_cast<LRESULT>(result);
  }

  class remote_buffer_t {
  public:
    explicit remote_buffer_t(HWND view) {
      DWORD pid = 0;
      GetWindowThreadProcessId(view, &pid);
      process_ = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, pid);
      if (!process_) throw std::runtime_error("Unable to open Explorer process");
      address_ = VirtualAllocEx(process_, nullptr, size_, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
      if (!address_) {
        CloseHandle(process_);
        process_ = nullptr;
        throw std::runtime_error("Unable to allocate Explorer exchange buffer");
      }
    }

    remote_buffer_t(const remote_buffer_t &) = delete;
    remote_buffer_t &operator=(const remote_buffer_t &) = delete;

    ~remote_buffer_t() {
      if (address_) VirtualFreeEx(process_, address_, 0, MEM_RELEASE);
      if (process_) CloseHandle(process_);
    }

    void *address(std::size_t offset = 0) const {
      return static_cast<std::byte *>(address_) + offset;
    }

    void write(const void *data, std::size_t bytes, std::size_t offset = 0) const {
      SIZE_T written = 0;
      if (!WriteProcessMemory(process_, address(offset), data, bytes, &written) || written != bytes) {
        throw std::runtime_error("Unable to write Explorer exchange buffer");
      }
    }

    void read(void *data, std::size_t bytes, std::size_t offset = 0) const {
      SIZE_T read = 0;
      if (!ReadProcessMemory(process_, address(offset), data, bytes, &read) || read != bytes) {
        throw std::runtime_error("Unable to read Explorer exchange buffer");
      }
    }

  private:
    static constexpr std::size_t size_ = 8192;
    HANDLE process_ {};
    void *address_ {};
  };

  std::wstring
  icon_name(HWND view, const remote_buffer_t &memory, int index) {
    constexpr std::size_t text_offset = 256;
    constexpr int text_capacity = 2048;
    LVITEMW item {};
    item.pszText = static_cast<wchar_t *>(memory.address(text_offset));
    item.cchTextMax = text_capacity;
    memory.write(&item, sizeof(item));
    const auto length = message(view, LVM_GETITEMTEXTW, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(memory.address()));
    if (length < 0 || length >= text_capacity) throw std::runtime_error("Invalid desktop icon name length");
    std::wstring name(static_cast<std::size_t>(length), L'\0');
    if (length > 0) memory.read(name.data(), static_cast<std::size_t>(length) * sizeof(wchar_t), text_offset);
    return name;
  }

  std::vector<icon_position_t>
  load_layout(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to open layout profile");
    nlohmann::json document;
    input >> document;
    if (document.value("Version", 0) != 2 || !document.contains("Icons") || !document["Icons"].is_array()) {
      throw std::runtime_error("Unsupported layout profile");
    }

    std::vector<icon_position_t> icons;
    for (const auto &item : document["Icons"]) {
      icons.push_back({
        from_utf8(item.value("Name", std::string {})),
        item.value("X", 0L),
        item.value("Y", 0L)
      });
    }
    return icons;
  }

  int
  restore_layout(const std::filesystem::path &path) {
    const auto icons = load_layout(path);
    const auto view = desktop_view();
    if ((GetWindowLongW(view, GWL_STYLE) & LVS_AUTOARRANGE) != 0) {
      throw std::runtime_error("Desktop auto-arrange is enabled");
    }

    std::map<std::wstring, std::queue<POINT>> positions;
    for (const auto &icon : icons) {
      positions[icon.name].push({ icon.x, icon.y });
    }

    const auto count = static_cast<int>(message(view, LVM_GETITEMCOUNT, 0, 0));
    if (count < 0 || count > 5000) throw std::runtime_error("Invalid desktop icon count");

    int restored = 0;
    remote_buffer_t memory(view);
    for (int index = 0; index < count; ++index) {
      const auto name = icon_name(view, memory, index);
      auto match = positions.find(name);
      if (match == positions.end() || match->second.empty()) continue;
      const auto position = match->second.front();
      match->second.pop();
      memory.write(&position, sizeof(position));
      message(view, LVM_SETITEMPOSITION32, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(memory.address()));
      ++restored;
    }
    return restored;
  }
}

int
wmain(int argc, wchar_t **argv) {
  if (argc != 3 || std::wstring_view(argv[1]) != L"restore") {
    std::wcerr << L"Usage: SunshineStreamLayout.exe restore <layout.json>\n";
    return 2;
  }

  const auto previous_context = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  try {
    const int restored = restore_layout(argv[2]);
    std::wcout << L"Restored " << restored << L" desktop icons\n";
    if (previous_context) SetThreadDpiAwarenessContext(previous_context);
    return 0;
  }
  catch (const std::exception &e) {
    std::cerr << "SunshineStreamLayout: " << e.what() << '\n';
    if (previous_context) SetThreadDpiAwarenessContext(previous_context);
    return 1;
  }
}
