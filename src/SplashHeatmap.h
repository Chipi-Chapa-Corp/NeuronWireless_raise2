/* Neuron-side Raise 2 heatmap model and stock keyscanner packet encoding.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <stdint.h>
#include <string.h>
#include "Communications_protocol.h"
#include "SplashColor.h"

class SplashHeatmap {
 public:
  enum { rows = 5, cols = 8, no_led = 255, max_leds = 36, frame_ms = 20,
         tap_boost = 48, decay_ms = 10 };

  void reset(uint32_t now) {
    memset(heat_, 0, sizeof(heat_));
    last_decay_ = now;
  }

  void tick(uint32_t now) {
    uint32_t steps = (now - last_decay_) / decay_ms;
    if (!steps) return;
    for (auto &side : heat_)
      for (auto &value : side) {
        // Q8 energy keeps the dim tail from falling through integer steps.
        // (251/256)^(1/5), rounded to Q16: five times slower than r4,
        // with the same 10 ms time resolution rather than larger fade steps.
        if (steps >= 5000) value = 0;
        else for (uint32_t i = 0; i < steps && value; ++i) {
          uint16_t next = static_cast<uint32_t>(value) * 65278 / 65536;
          // Keep the long exponential fade at normal energy, then use a
          // linear tail below ~16/255. Last RGB levels must not linger a second.
          uint16_t loss = value - next;
          if (loss < 16) loss = 16;  // Q8: one target unit per 160 ms.
          value = value > loss ? value - loss : 0;
        }
      }
    last_decay_ += steps * decay_ms;
  }

  // Coordinates here are physical Kaleidoscope coordinates; right columns mirror.
  void press(uint16_t row, uint16_t column, uint32_t now) {
    if (row >= rows || column >= cols * 2) return;
    uint8_t side = column >= cols;
    uint8_t col = side ? 15 - column : column;
    if (led(side, row, col) == no_led) return;
    tick(now);
    // Expand across the half, halving the injected energy at every ring.
    // Thumb keys occupy a separate lower row in the layout despite sharing
    // electrical row 4, so use that visual row when measuring their distance.
    uint8_t source_r, source_c;
    position(side, row, col, source_r, source_c);
    for (uint8_t r = 0; r < rows; ++r) {
      for (uint8_t c = 0; c < cols; ++c) {
        uint8_t index = led(side, r, c);
        if (index == no_led) continue;
        uint8_t target_r, target_c;
        position(side, r, c, target_r, target_c);
        uint8_t dr = source_r > target_r ? source_r - target_r : target_r - source_r;
        uint8_t dc = source_c > target_c ? source_c - target_c : target_c - source_c;
        uint8_t distance = dr > dc ? dr : dc;
        uint32_t value = heat_[side][index] + ((tap_boost * 256u) >> distance);
        heat_[side][index] = value > 255 * 256u ? 255 * 256u : value;
      }
    }
  }

  uint8_t value(uint8_t side, uint8_t index) const {
    return energy(side, index) / 256;
  }

  uint16_t energy(uint8_t side, uint8_t index) const {
    return side < 2 && index < max_leds ? heat_[side][index] : 0;
  }

  static uint8_t led(uint8_t side, uint8_t row, uint8_t col) {
    // Matches the stock Raise 2 Stalker side maps.
    static const uint8_t map[2][rows][cols] = {
      {{0,1,2,3,4,5,6,255}, {7,8,9,10,11,12,255,255},
       {13,14,15,16,17,18,255,255}, {19,20,21,22,23,24,25,255},
       {26,27,28,29,30,31,32,255}},
      {{0,1,2,3,4,5,6,255}, {7,8,9,10,11,12,13,14},
       {15,16,17,18,19,20,21,255}, {22,23,24,25,26,27,255,255},
       {28,29,30,31,32,33,34,35}}
    };
    return side < 2 && row < rows && col < cols ? map[side][row][col]
                                                : static_cast<uint8_t>(no_led);
  }

  static void position(uint8_t side, uint8_t row, uint8_t col, uint8_t &r, uint8_t &c) {
    // Logical key adjacency, not millimetres: the lower thumb pair is below the
    // inner space keys rather than beside the modifier row. No cross-split links.
    r = row;
    c = col;
    if (row == 4 && col >= (side ? 6 : 5)) {
      r = 5;
      c = col - 1;
    }
  }

  struct Frame {
    uint8_t indices[max_leds];
    uint8_t palette[16][4];
  };

  static void color_energy(uint16_t heat, uint8_t *rgba) {
    uint16_t value = (static_cast<uint32_t>(heat) + 128) / 256;
    if (value > 255) value = 255;
    uint16_t r = value < 128 ? value * 2 : 255;
    uint16_t g = value < 128 ? 255 : 255 - (value - 128) * 2;
    // The side's Layer renderer applies gamma and unequal R/G gains. Sending
    // linear low RGB directly collapses a single tap into just two lit levels.
    rgba[0] = SplashColor::red[r * value / 255];
    rgba[1] = SplashColor::green[g * value / 255];
    rgba[2] = rgba[3] = 0;
  }

  void make_frame(uint8_t side, uint8_t count, Frame &output) const {
    memset(&output, 0, sizeof(output));
    if (side >= 2 || count > max_leds) return;
    uint16_t centers[max_leds]{};
    uint8_t weights[max_leds]{};
    uint8_t clusters = 0;
    // Sort distinct visible energies. Black is always palette entry zero.
    for (uint8_t i = 0; i < count; ++i) {
      uint16_t heat = energy(side, i);
      if (heat < 128) continue;
      uint8_t at = 0;
      while (at < clusters && centers[at] < heat) ++at;
      if (at < clusters && centers[at] == heat) { ++weights[at]; continue; }
      for (uint8_t j = clusters; j > at; --j) {
        centers[j] = centers[j - 1]; weights[j] = weights[j - 1];
      }
      centers[at] = heat; weights[at] = 1; ++clusters;
    }
    // A single splash has at most eight distinct ring energies, so every RGB
    // shade is exact. For busy overlapping splashes, merge nearest energies
    // until the side's 15 nonblack palette slots suffice. No temporal dithering.
    while (clusters > 15) {
      uint8_t at = 0;
      uint16_t gap = 65535;
      for (uint8_t j = 0; j + 1 < clusters; ++j) {
        uint16_t distance = centers[j + 1] - centers[j];
        if (distance < gap) { gap = distance; at = j; }
      }
      uint8_t weight = weights[at] + weights[at + 1];
      centers[at] = (static_cast<uint32_t>(centers[at]) * weights[at] +
                     static_cast<uint32_t>(centers[at + 1]) * weights[at + 1]) / weight;
      weights[at] = weight;
      for (uint8_t j = at + 1; j + 1 < clusters; ++j) {
        centers[j] = centers[j + 1]; weights[j] = weights[j + 1];
      }
      --clusters;
    }
    for (uint8_t j = 0; j < clusters; ++j) color_energy(centers[j], output.palette[j + 1]);
    for (uint8_t i = 0; i < count; ++i) {
      uint16_t heat = energy(side, i);
      if (heat < 128) continue;
      uint8_t best = 0;
      uint16_t distance = 65535;
      for (uint8_t j = 0; j < clusters; ++j) {
        uint16_t d = heat > centers[j] ? heat - centers[j] : centers[j] - heat;
        if (d < distance) { distance = d; best = j + 1; }
      }
      output.indices[i] = best;
    }
  }

  static Communications_protocol::Packet palette_packet(
      Communications_protocol::Devices device, uint8_t first, uint8_t count,
      const uint8_t (*colors)[4] = nullptr) {
    Communications_protocol::Packet packet{};
    if (first >= 16 || count > 6 || first + count > 16) return packet;
    packet.header.device = device;
    packet.header.command = Communications_protocol::PALETTE_COLORS;
    packet.header.size = 1 + count * 4;
    packet.data[0] = first;
    if (colors) memcpy(&packet.data[1], colors[first], count * 4);
    return packet;
  }

  static Communications_protocol::Packet layer_packet(
      Communications_protocol::Devices device, bool underglow,
      const uint8_t *indices, uint8_t count, uint8_t layer = 0) {
    Communications_protocol::Packet packet{};
    uint8_t wire_count = underglow ? 54 : 36;
    if (count > wire_count || layer >= 10) return packet;
    packet.header.device = device;
    packet.header.command = underglow ? Communications_protocol::LAYER_UNDERGLOW_COLORS
                                      : Communications_protocol::LAYER_KEYMAP_COLORS;
    // Receivers always decode 36 BL / 54 UG entries, including unused left LEDs.
    packet.header.size = 1 + wire_count / 2;
    packet.data[0] = layer;
    for (uint8_t i = 0; i < count; ++i)
      packet.data[1 + i / 2] |= (indices ? indices[i] & 15 : 0) << ((i & 1) * 4);
    return packet;
  }

 private:
  uint16_t heat_[2][max_leds] = {};
  uint32_t last_decay_ = 0;
};
