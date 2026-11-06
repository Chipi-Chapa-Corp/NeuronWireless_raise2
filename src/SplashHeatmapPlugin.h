/* Splash/Heatmap LED plugin for the exact Raise2_v1.5.0 API.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once
#include "Kaleidoscope-LEDControl.h"
#include "SplashHeatmap.h"

class SplashHeatmapPlugin : public kaleidoscope::Plugin {
 public:
  kaleidoscope::EventHandlerResult onSetup();
  kaleidoscope::EventHandlerResult onLEDModeChange();
  kaleidoscope::EventHandlerResult onFocusEvent(const char *command);
  kaleidoscope::EventHandlerResult onLayerChange();
  kaleidoscope::EventHandlerResult beforeReportingState();

  void start();
  void render();

 private:
  static bool allow_packet(const Communications_protocol::Packet &packet);
  bool transmit(const Communications_protocol::Packet &packet);
  void observe_keys(const Communications_protocol::Packet &packet);
  void restore();
  void reset_sides();
  void reset_side(uint8_t side);
  void stream_frame(uint8_t side, uint8_t count, uint32_t now);
  static int8_t side_index(Communications_protocol::Devices device);
  Communications_protocol::Packet mode_packet(Communications_protocol::Devices device, bool black) const;
  Communications_protocol::Packet configuration_packet(uint8_t side, uint8_t stage, const uint8_t *indices);
  SplashHeatmap heatmap_;
  bool running_ = false;
  bool restoring_ = false;
  bool transmitting_ = false;
  bool leds_enabled_ = true;
  bool host_connected_ = false;
  uint8_t frame_stage_[2] = {};
  bool frame_changed_[2] = {};
  bool palette_known_[2] = {};
  bool map_known_[2] = {};
  SplashHeatmap::Frame frames_[2] = {};
  uint8_t sent_palette_[2][16][4] = {};
  bool restore_complete_[2] = {};
  uint32_t last_frame_[2] = {};
  uint32_t frame_started_[2] = {};
  Communications_protocol::Packet stock_mode_{};
  uint8_t previous_rows_[2][5] = {};
  uint8_t setup_stage_[2] = {};
  Communications_protocol::Devices devices_[2] = {};
  uint8_t sent_indices_[2][SplashHeatmap::max_leds] = {};
};

extern SplashHeatmapPlugin SplashHeatmapLED;
