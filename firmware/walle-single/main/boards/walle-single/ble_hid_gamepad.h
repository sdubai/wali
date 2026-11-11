#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace walle {
struct GamepadState {
  uint16_t buttons = 0; // Normalized to existing PS2 button bits.
  int lx = 128, ly = 128, rx = 128, ry = 128;
};

// Bounded HID Report Map parser. GATT reports exclude the report-ID byte.
// Only absolute, variable gamepad fields are supported; unknown layouts fail
// closed instead of interpreting keyboard/vendor bytes as motor commands.
class HidGamepadMap {
  struct Field {
    uint16_t offset, usage, page;
    uint8_t bits, id;
    int32_t minimum;
    int64_t maximum;
  };
  std::array<Field, 96> fields_{};
  size_t count_ = 0;
  std::array<uint16_t, 256> lengths_{};
  static int32_t Signed(uint32_t value, unsigned bits) {
    if (bits == 32)
      return static_cast<int32_t>(value);
    return (value & (1u << (bits - 1))) ? int64_t(value) - (1LL << bits)
                                        : value;
  }

public:
  bool Parse(const uint8_t *data, size_t size) {
    count_ = 0;
    lengths_.fill(0);
    struct Global {
      uint16_t page = 0;
      int32_t minimum = 0;
      int64_t maximum = 0;
      unsigned bits = 0, count = 0;
      uint8_t id = 0;
    } g;
    std::array<Global, 8> stack{};
    unsigned depth = 0;
    std::array<uint32_t, 96> usages{};
    unsigned usage_count = 0;
    uint32_t usage_min = 0, usage_max = 0;
    bool range = false, gamepad = false;
    unsigned collections = 0, gamepad_collection = 0;
    for (size_t pos = 0; pos < size;) {
      const uint8_t prefix = data[pos++];
      if (prefix == 0xfe)
        return false;
      const unsigned bytes = (prefix & 3) == 3 ? 4 : prefix & 3;
      if (pos + bytes > size)
        return false;
      uint32_t value = 0;
      for (unsigned i = 0; i < bytes; ++i)
        value |= uint32_t(data[pos++]) << (8 * i);
      const unsigned type = (prefix >> 2) & 3, tag = prefix >> 4;
      if (type == 1) {
        switch (tag) {
        case 0:
          g.page = value;
          break;
        case 1:
          if (!bytes)
            return false;
          g.minimum = Signed(value, bytes * 8);
          break;
        case 2:
          if (!bytes)
            return false;
          g.maximum = g.minimum < 0 ? Signed(value, bytes * 8) : int64_t(value);
          break;
        case 7:
          g.bits = value;
          break;
        case 8:
          if (!value || value > 255)
            return false;
          g.id = value;
          break;
        case 9:
          g.count = value;
          break;
        case 10:
          if (depth == stack.size())
            return false;
          stack[depth++] = g;
          break;
        case 11:
          if (!depth)
            return false;
          g = stack[--depth];
          break;
        }
      } else if (type == 2) {
        uint32_t usage = bytes == 4 ? value : (uint32_t(g.page) << 16) | value;
        if (tag == 0) {
          if (usage_count == usages.size())
            return false;
          usages[usage_count++] = usage;
        } else if (tag == 1) {
          usage_min = usage;
          range = true;
        } else if (tag == 2)
          usage_max = usage;
      } else if (type == 0) {
        if (tag == 10) {
          ++collections;
          if (value == 1 && usage_count && usages[0] >> 16 == 1 &&
              ((usages[0] & 0xffff) == 4 || (usages[0] & 0xffff) == 5)) {
            gamepad = true;
            gamepad_collection = collections;
          }
        } else if (tag == 12) {
          if (!collections)
            return false;
          if (collections == gamepad_collection)
            gamepad = false;
          --collections;
        } else if (tag == 8) {
          if (!g.bits || g.bits > 32 || g.count > 1024 ||
              lengths_[g.id] + uint64_t(g.bits) * g.count > 2048)
            return false;
          for (unsigned i = 0; i < g.count; ++i) {
            uint32_t usage =
                usage_count ? usages[std::min(i, usage_count - 1)] : 0;
            if (range && usage_min <= usage_max)
              usage = usage_min + std::min<uint32_t>(i, usage_max - usage_min);
            const uint16_t page = usage >> 16, u = usage & 0xffff;
            const bool supported =
                (page == 1 && ((u >= 0x30 && u <= 0x35) || u == 0x39)) ||
                (page == 9 && u >= 1 && u <= 16) ||
                (page == 2 && (u == 0xc4 || u == 0xc5));
            if (gamepad && (value & 7) == 2 && supported) {
              if (count_ == fields_.size() || g.maximum <= g.minimum)
                return false;
              fields_[count_++] = {uint16_t(lengths_[g.id] + i * g.bits),
                                   u,
                                   page,
                                   uint8_t(g.bits),
                                   g.id,
                                   g.minimum,
                                   g.maximum};
            }
          }
          lengths_[g.id] += g.bits * g.count;
        }
        usage_count = 0;
        range = false;
        usage_min = usage_max = 0;
      }
    }
    return collections == 0 && depth == 0 && count_ != 0;
  }
  bool Supports(uint8_t id) const {
    unsigned axes = 0;
    for (size_t i = 0; i < count_; ++i) {
      const auto &f = fields_[i];
      if (f.id == id && f.page == 1 && f.usage >= 0x30 && f.usage <= 0x35)
        axes |= 1u << (f.usage - 0x30);
    }
    return (axes & 3) == 3 && ((axes & 0x18) == 0x18 || (axes & 0x24) == 0x24);
  }
  bool Decode(uint8_t id, const uint8_t *data, size_t size,
              GamepadState &result) const {
    if (!data || !Supports(id) || size != (lengths_[id] + 7u) / 8)
      return false;
    GamepadState state;
    int axes[6] = {128, 128, 128, 128, 128, 128};
    unsigned present = 0;
    // Android HID button usages: A B X Y LB RB LT RT SELECT START L3 R3 HOME.
    constexpr uint16_t button_map[] = {1u << 14, 1u << 13, 1u << 15, 1u << 12,
                                       1u << 10, 1u << 11, 1u << 8,  1u << 9,
                                       1u << 0,  1u << 3,  1u << 1,  1u << 2,
                                       0,        0,        0,        0};
    for (size_t i = 0; i < count_; ++i) {
      const auto &f = fields_[i];
      if (f.id != id)
        continue;
      uint32_t raw = 0;
      for (unsigned b = 0; b < f.bits; ++b)
        raw |= uint32_t((data[(f.offset + b) / 8] >> ((f.offset + b) % 8)) & 1)
               << b;
      const int64_t v = f.minimum < 0 ? Signed(raw, f.bits) : int64_t(raw);
      if (f.page == 9) {
        if (v)
          state.buttons |= button_map[f.usage - 1];
      } else if (f.page == 2) {
        if (v < f.minimum || v > f.maximum)
          return false;
        // Simulation controls: brake -> LT, accelerator -> RT.
        if ((v - f.minimum) * 2 > f.maximum - f.minimum)
          state.buttons |= f.usage == 0xc5 ? (1u << 8) : (1u << 9);
      } else if (f.usage == 0x39) {
        const int64_t hat = v - f.minimum;
        constexpr uint16_t hats[] = {0x10, 0x30, 0x20, 0x60,
                                     0x40, 0xc0, 0x80, 0x90};
        if (hat >= 0 && hat < 8)
          state.buttons |= hats[hat];
      } else {
        if (v < f.minimum || v > f.maximum)
          return false;
        axes[f.usage - 0x30] =
            int((v - f.minimum) * 255 / (f.maximum - f.minimum));
        present |= 1u << (f.usage - 0x30);
      }
    }
    state.lx = axes[0];
    state.ly = axes[1];
    state.rx = axes[(present & 0x18) == 0x18 ? 3 : 2];
    state.ry = axes[(present & 0x18) == 0x18 ? 4 : 5];
    result = state;
    return true;
  }
};
} // namespace walle
