/**
 * @file src/stream_profile.h
 * @brief Integrated per-resolution Windows DPI and desktop-layout lifecycle.
 */
#pragma once

namespace rtsp_stream {
  struct launch_session_t;
}

namespace stream_profile {
  /**
   * Apply the configured DPI and desktop layout for a newly launched stream.
   * Returns true when disabled or successfully applied. Failures are logged and
   * returned to the caller so it can deliberately fail open.
   */
  bool
  apply_launch(const rtsp_stream::launch_session_t &session);
}
