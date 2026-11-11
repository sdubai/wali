#include "ble_hid_gamepad.h"
#include "gamepad_safety.h"
#include "ps2_pad.h"
#include <cassert>
#include <vector>

int main() {
  walle::GamepadSample sample;
  walle::GamepadState cached;
  assert(!sample.Read(cached, 0));
  sample.Publish({0, 128, 0, 128, 128}, 100);
  assert(sample.Read(cached, 350100));
  assert(!sample.Read(cached, 350101));
  assert(!sample.Read(cached, 99));
  sample.Release();
  assert(!sample.Read(cached, 101));
  walle::GamepadArmGate gate;
  assert(!gate.Update(2, false)); // Held stick on connect.
  assert(gate.Update(2, true));
  assert(gate.Update(2, false));
  assert(!gate.Update(0, false)); // Disconnect/timeout releases controls.
  assert(!gate.Update(2, false)); // Held stick on reconnect cannot restart.
  assert(gate.Update(2, true));
  assert(!gate.Update(1, false)); // PS2 fallback also requires neutral.
  assert(gate.Update(1, true));
  assert(!gate.Update(2, false)); // BLE taking priority cannot inherit arming.
  // Two HID report IDs: a four-axis gamepad and an unrelated consumer key.
  const uint8_t map[] = {
      0x05, 1,    0x09, 5,    0xa1, 1,    0x85, 1,    0x15, 0,    0x26, 0xff,
      0,    0x75, 8,    0x95, 4,    0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09,
      0x35, 0x81, 2,    0x15, 0,    0x25, 7,    0x75, 4,    0x95, 1,    0x09,
      0x39, 0x81, 0x42, 0x75, 4,    0x95, 1,    0x81, 1,    0x05, 9,    0x19,
      1,    0x29, 16,   0x15, 0,    0x25, 1,    0x75, 1,    0x95, 16,   0x81,
      2,    0xc0, 0x05, 0x0c, 0x09, 1,    0xa1, 1,    0x85, 2,    0x15, 0,
      0x25, 1,    0x75, 1,    0x95, 1,    0x09, 0x30, 0x81, 2,    0xc0};
  walle::HidGamepadMap parser;
  assert(parser.Parse(map, sizeof(map)));
  assert(parser.Supports(1));
  assert(!parser.Supports(2));
  walle::GamepadState state;
  uint8_t input[] = {128, 128, 128, 128, 0x0f, 0, 0};
  assert(parser.Decode(1, input, sizeof(input), state));
  assert(state.lx == 128 && state.ly == 128 && state.rx == 128 &&
         state.ry == 128 && !state.buttons);
  int l, r;
  Ps2StickSpeeds(state.lx, state.ly, 100, l, r);
  assert(l == 0 && r == 0);
  input[1] = 0;
  input[4] = 0;
  input[5] = 1;
  assert(parser.Decode(1, input, sizeof(input), state));
  assert((state.buttons & PS2_CROSS) && (state.buttons & 0x10));
  Ps2StickSpeeds(state.lx, state.ly, 100, l, r);
  assert(l == 100 && r == 100);
  input[0] = 255;
  input[1] = 128;
  input[4] = 0x0f;
  input[5] = 0x10;
  assert(parser.Decode(1, input, sizeof(input), state));
  assert(state.buttons == PS2_L1);
  Ps2StickSpeeds(state.lx, state.ly, 100, l, r);
  assert(l == 100 && r == -100);
  const auto old = state;
  assert(!parser.Decode(1, input, sizeof(input) - 1, state));
  assert(state.buttons == old.buttons && state.lx == old.lx);
  assert(!parser.Decode(2, input, sizeof(input), state));
  assert(!parser.Decode(1, nullptr, sizeof(input), state));
  // Signed 16-bit axes, HID global push/pop and unaligned report fields.
  const uint8_t signed_map[] = {0x05, 1,    0x09, 4,    0xa1, 1,    0x16, 0,
                                0x80, 0x26, 0xff, 0x7f, 0x75, 16,   0x95, 4,
                                0xa4, 0x09, 0x30, 0x09, 0x31, 0x09, 0x33, 0x09,
                                0x34, 0x81, 2,    0xb4, 0xc0};
  assert(parser.Parse(signed_map, sizeof(signed_map)));
  uint8_t signed_input[] = {0, 0, 0, 0x80, 0xff, 0x7f, 0, 0};
  assert(parser.Decode(0, signed_input, sizeof(signed_input), state));
  assert(state.lx == 127 && state.ly == 0 && state.rx == 255 &&
         state.ry == 127);
  const uint8_t trigger_map[] = {
      0x05, 1,    0x09, 5,    0xa1, 1,    0x15, 0,    0x26, 0xff, 0,    0x75,
      8,    0x95, 4,    0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x81,
      2,    0x05, 2,    0x95, 2,    0x09, 0xc5, 0x09, 0xc4, 0x81, 2,    0xc0};
  assert(parser.Parse(trigger_map, sizeof(trigger_map)));
  uint8_t triggers[] = {128, 128, 128, 128, 255, 0};
  assert(parser.Decode(0, triggers, sizeof(triggers), state));
  assert(state.buttons == PS2_L2);
  triggers[4] = 0;
  triggers[5] = 255;
  assert(parser.Decode(0, triggers, sizeof(triggers), state));
  assert(state.buttons == PS2_R2);
  // Truncated/malformed maps must never turn an unsupported report into input.
  for (size_t n = 0; n < sizeof(signed_map); ++n) {
    walle::HidGamepadMap partial;
    assert(!partial.Parse(signed_map, n));
  }
  const uint8_t pop[] = {0xb4};
  assert(!parser.Parse(pop, sizeof(pop)));
  auto oversized = std::vector<uint8_t>(std::begin(map), std::end(map));
  oversized[16] = 0xff; // Report count 255: bounded field table must reject it.
  assert(!parser.Parse(oversized.data(), oversized.size()));
}
