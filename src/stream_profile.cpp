#include "stream_profile.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>

#include <boost/filesystem/path.hpp>
#include <nlohmann/json.hpp>

#include "config.h"
#include "logging.h"
#include "platform/common.h"
#include "platform/run_command.h"
#include "rtsp.h"

#ifdef _WIN32
  #include "platform/windows/display_device/windows_utils.h"
#endif

using json = nlohmann::json;
using namespace std::literals;

namespace stream_profile {
  namespace {
    std::mutex profile_mutex;

    std::string
    resolution_key(int width, int height) {
      if (width <= 0 || height <= 0) {
        return "default";
      }
      if (width < height) {
        std::swap(width, height);
      }
      return std::to_string(width) + "x" + std::to_string(height);
    }

    int
    configured_dpi(const json &settings, const std::string &key, int width, int height) {
      const auto mapping = settings.find("DpiByResolution");
      if (mapping != settings.end() && mapping->is_object()) {
        const auto entry = mapping->find(key);
        if (entry != mapping->end() && entry->is_number_integer()) {
          return entry->get<int>();
        }
      }

      const int fallback = settings.value("DefaultStreamDpi", 200);
      const int short_side = std::min(width, height);
      if (short_side <= 0) return fallback;
      if (short_side <= 720) return 150;
      if (short_side <= 1080) return 175;
      if (short_side <= 1640) return 200;
      return 225;
    }

    std::string
    utc_timestamp() {
      const auto now = std::chrono::system_clock::now();
      const auto time = std::chrono::system_clock::to_time_t(now);
      std::tm utc {};
#ifdef _WIN32
      gmtime_s(&utc, &time);
#else
      gmtime_r(&time, &utc);
#endif
      std::ostringstream out;
      out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
      return out.str();
    }

    bool
    read_json_file(const std::filesystem::path &path, json &value) {
      std::ifstream input(path, std::ios::binary);
      if (!input) {
        return false;
      }
      input >> value;
      return true;
    }

    bool
    replace_json_file(const std::filesystem::path &path, const json &value) {
      const auto temporary = path.string() + ".sunshine.tmp";
      {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
          return false;
        }
        output << value.dump(4) << '\n';
        if (!output) {
          return false;
        }
      }

      std::error_code ec;
      std::filesystem::copy_file(
        temporary,
        path,
        std::filesystem::copy_options::overwrite_existing,
        ec);
      const auto copy_error = ec;
      ec.clear();
      std::filesystem::remove(temporary, ec);
      if (copy_error) {
        return false;
      }
      return true;
    }

#ifdef _WIN32
    bool
    restore_layout_in_user_session(const rtsp_stream::launch_session_t &session,
                                   const std::filesystem::path &root,
                                   const std::filesystem::path &layout_path) {
      if (!std::filesystem::is_regular_file(layout_path)) {
        BOOST_LOG(info) << "Integrated stream profile has no saved layout for " << layout_path.string();
        return true;
      }

      auto helper = platf::appdata().parent_path() / "tools" / "SunshineStreamLayout.exe";
      if (!std::filesystem::is_regular_file(helper)) {
        // Development/portable fallback for an unpackaged helper beside the
        // profile. Production packages always use the fixed tools directory.
        helper = root / "SunshineStreamLayout.exe";
      }
      if (!std::filesystem::is_regular_file(helper)) {
        BOOST_LOG(warning) << "Integrated stream layout helper is missing: " << helper.string();
        return false;
      }

      auto working_directory = boost::filesystem::path(root.string());
      const auto command = '"' + helper.string() + "\" restore \"" + layout_path.string() + '"';
      std::error_code launch_error;
      auto child = platf::run_command(
        false,
        false,
        command,
        working_directory,
        session.env,
        nullptr,
        launch_error,
        nullptr);
      if (launch_error) {
        BOOST_LOG(warning) << "Failed to start integrated stream layout helper: " << launch_error.message();
        return false;
      }

      child.wait(launch_error);
      if (launch_error || child.exit_code() != 0) {
        BOOST_LOG(warning) << "Integrated stream layout helper failed: "
                           << (launch_error ? launch_error.message() : std::to_string(child.exit_code()));
        return false;
      }
      return true;
    }
#endif
  }

  bool
  apply_launch(const rtsp_stream::launch_session_t &session) {
    if (config::sunshine.stream_profile_settings_path.empty()) {
      return true;
    }

#ifndef _WIN32
    BOOST_LOG(warning) << "Integrated stream profiles are only supported on Windows"sv;
    return false;
#else
    const auto lock = std::lock_guard(profile_mutex);
    try {
      const std::filesystem::path settings_path = config::sunshine.stream_profile_settings_path;
      json settings;
      if (!read_json_file(settings_path, settings)) {
        BOOST_LOG(warning) << "Integrated stream profile settings are unavailable: " << settings_path.string();
        return false;
      }

      const auto root = settings_path.parent_path();
      const auto key = resolution_key(session.width, session.height);
      const int dpi = configured_dpi(settings, key, session.width, session.height);
      const auto virtual_device_id = settings.value("VirtualAdapterDeviceId", std::string { "Root\\ZakoVDD" });

      // Publish lifecycle state before changing the desktop. If any later
      // cosmetic step fails, the existing Undo command can still restore the
      // physical desktop instead of leaving the stream DPI behind.
      const auto state_path = root / "sunshine-display-state.json";
      json state;
      if (!read_json_file(state_path, state) || !state.is_object()) {
        state = json::object();
      }
      state["Version"] = 1;
      state["Generation"] = state.value("Generation", 0LL) + 1;
      state["Active"] = true;
      state["StreamKey"] = key;
      state["ClientWidth"] = session.width;
      state["ClientHeight"] = session.height;
      state["StartedAt"] = utc_timestamp();
      state["PendingRestoreToken"] = "";
      if (!state.contains("LastStoppedAt")) {
        state["LastStoppedAt"] = "";
      }
      if (!replace_json_file(state_path, state)) {
        BOOST_LOG(warning) << "Integrated stream profile could not update lifecycle state: " << state_path.string();
        return false;
      }

      const auto scale_result = display_device::w_utils::set_display_scale({}, virtual_device_id, dpi);
      if (!scale_result.success) {
        BOOST_LOG(warning) << "Integrated stream profile failed to apply " << dpi
                           << "% DPI for " << key << ": " << scale_result.message;
        return false;
      }

      const int settle_delay_ms = std::clamp(settings.value("DpiSettleDelayMs", 250), 0, 2000);
      if (settle_delay_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(settle_delay_ms));
      }

      const auto layout_path = root / "layouts" /
                               ("desktop_" + key + "_dpi" + std::to_string(dpi) + ".json");
      const bool layout_ok = restore_layout_in_user_session(session, root, layout_path);
      BOOST_LOG(info) << "Integrated stream profile applied: resolution=" << key
                      << ", dpi=" << dpi << ", layout=" << (layout_ok ? "ready" : "failed");
      return layout_ok;
    }
    catch (const std::exception &e) {
      BOOST_LOG(warning) << "Integrated stream profile failed: " << e.what();
      return false;
    }
#endif
  }
}
