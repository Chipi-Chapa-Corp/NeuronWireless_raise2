/* SPDX-License-Identifier: GPL-3.0-only */
#include "SplashHeatmapPlugin.h"
#include "Communications.h"
#include "Colormap-Defy.h"
#include "LED-Palette-Theme-Defy.h"
#include "Kaleidoscope-FocusSerial.h"
#include "LedModeSerializable-Layer.h"
#include "LedModeSerializable-SolidColor.h"
#include "common.h"

using namespace Communications_protocol;
using kaleidoscope::EventHandlerResult;
using StockKeyData = kaleidoscope::device::dygma::dygma_keyboards::key_data;
static_assert(sizeof(((StockKeyData *)nullptr)->rows) == 5,
              "Splash build must retain the v1.5.0 five-byte key packet decoder");

SplashHeatmapPlugin SplashHeatmapLED;

int8_t SplashHeatmapPlugin::side_index(Devices device) {
  switch (device) {
    case KEYSCANNER_DEFY_LEFT: case RF_DEFY_LEFT: case BLE_DEFY_LEFT: return 0;
    case KEYSCANNER_DEFY_RIGHT: case RF_DEFY_RIGHT: case BLE_DEFY_RIGHT: return 1;
    default: return -1;
  }
}

bool SplashHeatmapPlugin::allow_packet(const Packet &p) {
  auto &self = SplashHeatmapLED;
  if ((!self.running_ && !self.restoring_) || self.transmitting_) return true;
  switch (p.header.command) {
    case MODE_LED:
      self.stock_mode_ = p;
      return false;
    case PALETTE_COLORS: case LAYER_KEYMAP_COLORS: case LAYER_UNDERGLOW_COLORS: case LED:
      return false;
    default:
      return true;
  }
}

bool SplashHeatmapPlugin::transmit(const Packet &p) {
  transmitting_ = true;
  bool accepted = Communications.sendPacket(p);
  transmitting_ = false;
  return accepted;
}

void SplashHeatmapPlugin::reset_side(uint8_t side) {
  setup_stage_[side] = 0;
  frame_stage_[side] = 0;
  frame_changed_[side] = false;
  palette_known_[side] = map_known_[side] = false;
  restore_complete_[side] = false;
}

void SplashHeatmapPlugin::reset_sides() {
  for (uint8_t side = 0; side < 2; ++side) reset_side(side);
}

EventHandlerResult SplashHeatmapPlugin::onSetup() {
  Communications.setPacketFilter(allow_packet);
  Communications.callbacks.bind(HAS_KEYS, [this](const Packet &p) { observe_keys(p); });
  auto reconnect = [this](const Packet &p) {
    if (!running_ && !restoring_) return;
    int8_t side = side_index(p.header.device);
    if (side < 0) return;
    // Repeated requests must not restart an in-flight configuration forever.
    if (p.header.command == CONNECTED || setup_stage_[side] >= 25) {
      reset_side(side);
    }
  };
  Communications.callbacks.bind(CONNECTED, reconnect);
  Communications.callbacks.bind(RETRY_LAYERS, reconnect);
  // A half requests MODE_LED when its host/power state resumes. The stock
  // response is intentionally filtered, so the overlay must answer itself.
  Communications.callbacks.bind(MODE_LED, reconnect);
  Communications.callbacks.bind(DISCONNECTED, [this](const Packet &p) {
    int8_t side = side_index(p.header.device);
    if (side < 0) return;
    memset(previous_rows_[side], 0, sizeof(previous_rows_[side]));
    reset_side(side);
  });
  return EventHandlerResult::OK;
}

void SplashHeatmapPlugin::observe_keys(const Packet &p) {
  if (p.header.size < 5) return;
  uint8_t side;
  switch (p.header.device) {
    case KEYSCANNER_DEFY_LEFT: case RF_DEFY_LEFT: case BLE_DEFY_LEFT: side = 0; break;
    case KEYSCANNER_DEFY_RIGHT: case RF_DEFY_RIGHT: case BLE_DEFY_RIGHT: side = 1; break;
    default: return;
  }
  for (uint8_t row = 0; row < 5; ++row) {
    uint8_t pressed = p.data[row] & ~previous_rows_[side][row];
    previous_rows_[side][row] = p.data[row];
    if (!running_) continue;
    for (uint8_t col = 0; col < 8; ++col)
      if (pressed & (1 << col)) heatmap_.press(row, side ? 15 - col : col, millis());
  }
}

void SplashHeatmapPlugin::start() {
  if (!running_) heatmap_.reset(millis());
  running_ = true;
  restoring_ = false;
  leds_enabled_ = ::LEDControl.isEnabled();
  host_connected_ = Communications.is_host_connected();
  reset_sides();
  // Capture the selected stock mode after the transmit filter owns LEDs.
  ::LEDControl.refreshAll();
}

EventHandlerResult SplashHeatmapPlugin::onLEDModeChange() {
  // Competing modes are blocked before routing and kept for later restoration.
  return EventHandlerResult::OK;
}

EventHandlerResult SplashHeatmapPlugin::onLayerChange() {
  // Keyboard layers never select our side display layer or reset accumulated heat.
  return EventHandlerResult::OK;
}

EventHandlerResult SplashHeatmapPlugin::beforeReportingState() {
  render();
  return EventHandlerResult::OK;
}

EventHandlerResult SplashHeatmapPlugin::onFocusEvent(const char *command) {
  if (strcmp(command, "splash.protocol") == 0) {
    ::Focus.send(2);
    return EventHandlerResult::EVENT_CONSUMED;
  }
  if (::Focus.handleHelp(command, "splash2\nsplash.protocol")) return EventHandlerResult::OK;
  if (strcmp(command, "splash2") != 0) return EventHandlerResult::OK;
  if (::Focus.isEOL()) {
    ::Focus.send(running_ ? 1 : 0);
  } else {
    uint8_t enabled = 0;
    ::Focus.read(enabled);
    if (enabled) start();
    else if (running_) restore();
  }
  return EventHandlerResult::EVENT_CONSUMED;
}

Packet SplashHeatmapPlugin::mode_packet(Devices device, bool black) const {
  Packet p{};
  p.header.device = device;
  p.header.command = MODE_LED;
  if (black) {
    LedModeSerializable_SolidColor mode{CRC32_STR("LedModeSerializable_SolidColor")};
    mode.base_settings.step = 0;
    mode.base_settings.brightness = 255;
    mode.base_settings.delay_ms = 100;
    mode.r_ = mode.g_ = mode.b_ = mode.w_ = 0;
    p.header.size = mode.serialize(p.data);
  } else {
    LedModeSerializable_Layer mode{CRC32_STR("LedModeSerializable_Layer")};
    mode.base_settings.step = 0;
    mode.base_settings.brightness = 255;
    mode.base_settings.delay_ms = 10;
    mode.layer = 0;
    mode.fade_is_on = 0;
    p.header.size = mode.serialize(p.data);
  }
  return p;
}

Packet SplashHeatmapPlugin::configuration_packet(uint8_t side, uint8_t stage, const uint8_t *indices) {
  Devices device = devices_[side];
  if (stage == 0) return mode_packet(device, true);
  if (stage <= 3) {
    uint8_t first = (stage - 1) * 6, count = stage == 3 ? 4 : 6;
    Packet p = SplashHeatmap::palette_packet(device, first, count);
    if (restoring_) {
      cRGB colors[16];
      ::LEDPaletteThemeDefy.getColorPalette(colors);
      static_assert(sizeof(cRGB) == 4, "Side palette uses four-channel colors");
      memcpy(&p.data[1], &colors[first], count * sizeof(cRGB));
    }
    return p;
  }
  uint8_t region = stage - 4, layer = region / 2;
  bool ug = region % 2 == 0;
  uint8_t count = ug ? (side ? UNDERGLOW_LEDS_RIGHT_SIDE : UNDERGLOW_LEDS_LEFT_SIDE)
                     : (side ? LEDS_HAND_RIGHT : LEDS_HAND_LEFT);
  if (restoring_) {
    uint8_t map[kaleidoscope::Runtime.device().led_count];
    ::ColormapEffectDefy.getLayer(layer, map);
    uint16_t offset = ug ? LEDS_HAND_LEFT + LEDS_HAND_RIGHT + (side ? UNDERGLOW_LEDS_LEFT_SIDE : 0)
                         : (side ? LEDS_HAND_LEFT : 0);
    return SplashHeatmap::layer_packet(device, ug, map + offset, count, layer);
  }
  return SplashHeatmap::layer_packet(device, ug, ug ? nullptr : indices, count, layer);
}

void SplashHeatmapPlugin::render() {
  if (!running_ && !restoring_) return;
  uint32_t now = millis();
  heatmap_.tick(now);
  bool connected = Communications.is_host_connected();
  if (!connected) { host_connected_ = false; return; }
  if (!host_connected_) {
    host_connected_ = true;
    reset_sides();
  }
  bool enabled = ::LEDControl.isEnabled();
  if (enabled != leds_enabled_) {
    leds_enabled_ = enabled;
    reset_sides();
  }
  auto const &scanner = kaleidoscope::Runtime.device().keyScanner();
  Devices current[2] = {scanner.leftHandDevice(), scanner.rightHandDevice()};
  for (uint8_t side = 0; side < 2; ++side) {
    if (current[side] != devices_[side]) {
      devices_[side] = current[side];
      reset_side(side);
    }
    if (current[side] == UNKNOWN) {
      if (restoring_) restore_complete_[side] = true;
      continue;
    }
    uint8_t count = side ? LEDS_HAND_RIGHT : LEDS_HAND_LEFT;
    uint8_t indices[SplashHeatmap::max_leds]{};
    // Initialization maps are black; RGB frames are generated after setup.
    uint8_t stage = setup_stage_[side];
    if (!enabled && running_) {
      if (stage == 0 && transmit(mode_packet(current[side], true))) ++setup_stage_[side];
      continue;
    }
    if (stage < 24) {
      if (transmit(configuration_packet(side, stage, indices))) ++setup_stage_[side];
    } else if (stage == 24) {
      Packet p = mode_packet(current[side], false);
      if (restoring_) {
        // Re-evaluate stock mode if layers changed during paced restoration.
        ::LEDControl.refreshAll();
        p = stock_mode_;
        if (p.header.command != MODE_LED) p = mode_packet(current[side], true);
        p.header.device = current[side];
      }
      if (transmit(p)) {
        ++setup_stage_[side];
        restore_complete_[side] = restoring_;
        // Maps were paced while heat could change; force one fresh frame.
        frame_stage_[side] = 0;
        palette_known_[side] = map_known_[side] = false;
        last_frame_[side] = now - SplashHeatmap::frame_ms;
      }
    } else if (running_) {
      stream_frame(side, count, now);
    }
  }
  if (restoring_ && restore_complete_[0] && restore_complete_[1]) {
    restoring_ = false;
    ::LEDControl.refreshAll();
  }
}

void SplashHeatmapPlugin::stream_frame(uint8_t side, uint8_t count, uint32_t now) {
  if (frame_stage_[side] != 0 && now - frame_started_[side] >= 1000) {
    // An unavailable transport must not pin an old bright snapshot forever.
    // The next snapshot resends all entries, including any partially sent data.
    frame_stage_[side] = 0;
    palette_known_[side] = map_known_[side] = false;
  }
  if (frame_stage_[side] == 0) {
    if (now - last_frame_[side] < SplashHeatmap::frame_ms) return;
    // Frames are sent only when their palette or map changes.
    heatmap_.make_frame(side, count, frames_[side]);
    frame_started_[side] = now;
    frame_changed_[side] = false;
    frame_stage_[side] = 1;
  }
  const auto &frame = frames_[side];
  while (frame_stage_[side] <= 5) {
    uint8_t stage = frame_stage_[side];
    Packet packet{};
    if (stage <= 3) {
      uint8_t first = (stage - 1) * 6, colors = stage == 3 ? 4 : 6;
      if (palette_known_[side] && memcmp(sent_palette_[side][first], frame.palette[first], colors * 4) == 0) {
        ++frame_stage_[side]; continue;
      }
      packet = SplashHeatmap::palette_packet(devices_[side], first, colors, frame.palette);
      if (!transmit(packet)) return;
      memcpy(sent_palette_[side][first], frame.palette[first], colors * 4);
      frame_changed_[side] = true;
    } else if (stage == 4) {
      palette_known_[side] = true;
      if (map_known_[side] && memcmp(sent_indices_[side], frame.indices, count) == 0) {
        ++frame_stage_[side]; continue;
      }
      packet = SplashHeatmap::layer_packet(devices_[side], false, frame.indices, count);
      if (!transmit(packet)) return;
      memcpy(sent_indices_[side], frame.indices, count);
      map_known_[side] = true;
      frame_changed_[side] = true;
    } else {
      // Palette writes do not update the physical LED buffer. Draw only after
      // all palette chunks and the matching map have been queued in order.
      if (frame_changed_[side] && !transmit(mode_packet(devices_[side], false))) return;
      frame_stage_[side] = 0;
      // Leave at least a full frame interval after MODE_LED so its 10 ms side
      // draw can complete before any palette entries for the next frame change.
      // Idle/no-op comparisons do not postpone the next real key response.
      if (frame_changed_[side]) {
        last_frame_[side] = now;
      }
      return;
    }
    ++frame_stage_[side];
    return; // At most one overlay packet per half per loop, including retries.
  }
}

void SplashHeatmapPlugin::restore() {
  running_ = false;
  restoring_ = true;
  reset_sides();
  // Saved palette/maps remain in EEPROM; the paced sender rereads them.
  ::LEDControl.refreshAll();
}
