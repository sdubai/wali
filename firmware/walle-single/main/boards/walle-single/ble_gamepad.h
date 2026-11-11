#pragma once
#include "ble_hid_gamepad.h"

void InitBleGamepad();
// True only for a complete, recently received/read gamepad report.
bool ReadBleGamepad(walle::GamepadState &state);
