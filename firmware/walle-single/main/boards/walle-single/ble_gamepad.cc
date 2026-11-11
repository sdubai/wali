#include "ble_gamepad.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "gamepad_safety.h"
#include "sdkconfig.h"

#if CONFIG_WALLE_BLE_GAMEPAD && CONFIG_BT_NIMBLE_ENABLED
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include <array>
#include <cstdio>

namespace {
constexpr char TAG[] = "WalleBLE";
constexpr int64_t kFreshUs = walle::GamepadSample::kFreshUs;
portMUX_TYPE snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
walle::GamepadSample snapshot;
void Invalidate() {
  portENTER_CRITICAL(&snapshot_lock);
  snapshot.Release();
  portEXIT_CRITICAL(&snapshot_lock);
}

// All GATT/discovery state below is owned exclusively by NimBLE's host event
// queue, including the periodic callout. Motor control only reads a snapshot.
ble_addr_t target{};
uint8_t own_type;
uint16_t connection = BLE_HS_CONN_HANDLE_NONE;
uint16_t service_start = 0, service_end = 0, map_handle = 0, control_handle = 0;
std::array<ble_gatt_chr, 32> characteristics{};
size_t char_count = 0, report_index = 0;
uint16_t reference_handle = 0, cccd_handle = 0;
std::array<uint8_t, 2048> map_bytes{};
size_t map_size = 0;
walle::HidGamepadMap report_map;
uint8_t report_id = 0;
uint16_t input_handle = 0;
bool ready = false, connecting = false, read_pending = false;
int64_t retry_us = 0, operation_us = 0;
ble_npl_callout heartbeat;

void Fail(const char *step, int status) {
  ESP_LOGW(TAG, "%s failed (%d); controls released", step, status);
  Invalidate();
  ready = false;
  if (connection != BLE_HS_CONN_HANDLE_NONE)
    ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
}
void Check(int rc, const char *step) {
  if (rc)
    Fail(step, rc);
}
void Publish(uint16_t handle, os_mbuf *om) {
  if (!ready || handle != input_handle || !om)
    return;
  std::array<uint8_t, 256> data{};
  const size_t length = OS_MBUF_PKTLEN(om);
  walle::GamepadState state;
  if (length > data.size() || os_mbuf_copydata(om, 0, length, data.data()) ||
      !report_map.Decode(report_id, data.data(), length, state)) {
    Fail("unsupported/malformed HID input", BLE_HS_EINVAL);
    return;
  }
  portENTER_CRITICAL(&snapshot_lock);
  snapshot.Publish(state, esp_timer_get_time());
  portEXIT_CRITICAL(&snapshot_lock);
}
int InputRead(uint16_t conn, const ble_gatt_error *error, ble_gatt_attr *attr,
              void *) {
  if (conn != connection)
    return 0;
  read_pending = false;
  if (error->status || !attr)
    Fail("input read", error->status);
  else
    Publish(attr->handle, attr->om);
  return 0;
}
int Subscribed(uint16_t conn, const ble_gatt_error *error, ble_gatt_attr *,
               void *) {
  if (conn != connection)
    return 0;
  if (error->status) {
    Fail("CCCD subscription", error->status);
    return 0;
  }
  ready = true;
  operation_us = esp_timer_get_time();
  ESP_LOGI(TAG,
           "X3S HID ready, report ID=%u; center sticks/release buttons to arm",
           report_id);
  return 0;
}
void Subscribe() {
  const uint8_t enable[] = {1, 0};
  Check(ble_gattc_write_flat(connection, cccd_handle, enable, sizeof(enable),
                             Subscribed, nullptr),
        "subscribe");
}
int ControlWritten(uint16_t conn, const ble_gatt_error *error, ble_gatt_attr *,
                   void *) {
  if (conn == connection) {
    if (error->status)
      Fail("HID control", error->status);
    else
      Subscribe();
  }
  return 0;
}
void NextReport();
int ReferenceRead(uint16_t conn, const ble_gatt_error *error,
                  ble_gatt_attr *attr, void *) {
  if (conn != connection)
    return 0;
  uint8_t reference[2]{};
  if (error->status || !attr || OS_MBUF_PKTLEN(attr->om) != 2 ||
      os_mbuf_copydata(attr->om, 0, 2, reference)) {
    Fail("report reference", error->status);
    return 0;
  }
  if (reference[1] != 1 || !report_map.Supports(reference[0])) {
    ++report_index;
    NextReport();
    return 0;
  }
  input_handle = characteristics[report_index].val_handle;
  report_id = reference[0];
  if (!cccd_handle) {
    Fail("missing input CCCD", BLE_HS_ENOENT);
    return 0;
  }
  // The manufacturer's example uses 0x00 on 0x2A4C. Preserve that tested
  // handshake (some controllers interpret it nonstandardly).
  if (control_handle) {
    const uint8_t enable = 0;
    Check(ble_gattc_write_flat(connection, control_handle, &enable, 1,
                               ControlWritten, nullptr),
          "HID enable");
  } else
    Subscribe();
  return 0;
}
int Descriptors(uint16_t conn, const ble_gatt_error *error, uint16_t,
                const ble_gatt_dsc *descriptor, void *) {
  if (conn != connection)
    return 0;
  if (!error->status && descriptor) {
    const uint16_t uuid = ble_uuid_u16(&descriptor->uuid.u);
    if (uuid == 0x2908)
      reference_handle = descriptor->handle;
    if (uuid == 0x2902)
      cccd_handle = descriptor->handle;
  } else if (error->status == BLE_HS_EDONE) {
    if (!reference_handle) {
      ++report_index;
      NextReport();
    } else
      Check(
          ble_gattc_read(connection, reference_handle, ReferenceRead, nullptr),
          "read report reference");
  } else
    Fail("descriptor discovery", error->status);
  return 0;
}
void NextReport() {
  while (report_index < char_count) {
    const auto &ch = characteristics[report_index];
    if (ble_uuid_u16(&ch.uuid.u) == 0x2a4d &&
        (ch.properties & BLE_GATT_CHR_PROP_NOTIFY) &&
        (ch.properties & BLE_GATT_CHR_PROP_READ)) {
      reference_handle = cccd_handle = 0;
      const uint16_t end =
          report_index + 1 < char_count
              ? characteristics[report_index + 1].def_handle - 1
              : service_end;
      Check(ble_gattc_disc_all_dscs(connection, ch.val_handle, end, Descriptors,
                                    nullptr),
            "discover descriptors");
      return;
    }
    ++report_index;
  }
  Fail("no supported readable four-axis gamepad report", BLE_HS_ENOTSUP);
}
int MapRead(uint16_t conn, const ble_gatt_error *error, ble_gatt_attr *attr,
            void *) {
  if (conn != connection)
    return 0;
  if (!error->status && attr) {
    const size_t length = OS_MBUF_PKTLEN(attr->om);
    if (attr->offset != map_size || map_size + length > map_bytes.size() ||
        os_mbuf_copydata(attr->om, 0, length, map_bytes.data() + map_size)) {
      Fail("report map bounds", BLE_HS_EINVAL);
      return BLE_HS_EINVAL;
    }
    map_size += length;
  } else if (error->status == BLE_HS_EDONE) {
    if (!report_map.Parse(map_bytes.data(), map_size))
      Fail("unsupported HID report map", BLE_HS_EINVAL);
    else {
      report_index = 0;
      NextReport();
    }
  } else
    Fail("read report map", error->status);
  return 0;
}
int Characteristics(uint16_t conn, const ble_gatt_error *error,
                    const ble_gatt_chr *chr, void *) {
  if (conn != connection)
    return 0;
  if (!error->status && chr) {
    if (char_count == characteristics.size()) {
      Fail("too many HID characteristics", BLE_HS_ENOMEM);
      return BLE_HS_ENOMEM;
    }
    characteristics[char_count++] = *chr;
    const uint16_t uuid = ble_uuid_u16(&chr->uuid.u);
    if (uuid == 0x2a4b)
      map_handle = chr->val_handle;
    if (uuid == 0x2a4c && (chr->properties & BLE_GATT_CHR_PROP_WRITE))
      control_handle = chr->val_handle;
  } else if (error->status == BLE_HS_EDONE) {
    if (!map_handle)
      Fail("missing HID report map", BLE_HS_ENOENT);
    else
      Check(ble_gattc_read_long(connection, map_handle, 0, MapRead, nullptr),
            "read map");
  } else
    Fail("characteristic discovery", error->status);
  return 0;
}
int Services(uint16_t conn, const ble_gatt_error *error,
             const ble_gatt_svc *service, void *) {
  if (conn != connection)
    return 0;
  if (!error->status && service) {
    service_start = service->start_handle;
    service_end = service->end_handle;
  } else if (error->status == BLE_HS_EDONE) {
    if (!service_start)
      Fail("missing HID service", BLE_HS_ENOENT);
    else
      Check(ble_gattc_disc_all_chrs(connection, service_start, service_end,
                                    Characteristics, nullptr),
            "discover characteristics");
  } else
    Fail("service discovery", error->status);
  return 0;
}
int Gap(ble_gap_event *event, void *) {
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    connecting = false;
    if (event->connect.status) {
      ESP_LOGI(TAG, "Connect pending retry (%d); use X+HOME mode",
               event->connect.status);
      retry_us = esp_timer_get_time() + 5000000;
      break;
    }
    connection = event->connect.conn_handle;
    char_count = map_size = 0;
    service_start = service_end = map_handle = control_handle = input_handle =
        0;
    ready = read_pending = false;
    Invalidate();
    operation_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Connected to bound X3S; discovering HID");
    {
      const ble_uuid16_t hid = BLE_UUID16_INIT(0x1812);
      Check(ble_gattc_disc_svc_by_uuid(connection, &hid.u, Services, nullptr),
            "discover HID");
    }
    break;
  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGW(TAG, "Disconnected (%d); controls released",
             event->disconnect.reason);
    Invalidate();
    connection = BLE_HS_CONN_HANDLE_NONE;
    ready = connecting = read_pending = false;
    retry_us = esp_timer_get_time() + 3000000;
    break;
  case BLE_GAP_EVENT_NOTIFY_RX:
    if (event->notify_rx.conn_handle == connection)
      Publish(event->notify_rx.attr_handle, event->notify_rx.om);
    break;
  case BLE_GAP_EVENT_ENC_CHANGE:
    if (event->enc_change.status)
      Fail("security", event->enc_change.status);
    break;
  }
  return 0;
}
void Heartbeat(ble_npl_event *) {
  const int64_t now = esp_timer_get_time();
  if (connection == BLE_HS_CONN_HANDLE_NONE && !connecting && now >= retry_us) {
    connecting = true;
    retry_us = now + 5000000;
    ble_gap_conn_params params{};
    params.scan_itvl = 0x30;
    params.scan_window = 0x10;
    params.itvl_min = 12;
    params.itvl_max = 24;
    params.supervision_timeout = 100;
    params.min_ce_len = 0;
    params.max_ce_len = 0;
    const int rc =
        ble_gap_connect(own_type, &target, 3000, &params, Gap, nullptr);
    if (rc) {
      connecting = false;
      ESP_LOGW(TAG, "Connect start failed (%d)", rc);
    }
  } else if (connection != BLE_HS_CONN_HANDLE_NONE) {
    if ((!ready && now - operation_us > 15000000) ||
        (read_pending && now - operation_us > kFreshUs)) {
      Fail("HID procedure timeout", BLE_HS_ETIMEOUT);
    } else if (ready && !read_pending) {
      // HID notifications can be change-only. Periodic reads confirm a
      // held stick without trusting stale state during a broken link.
      read_pending = true;
      operation_us = now;
      Check(ble_gattc_read(connection, input_handle, InputRead, nullptr),
            "poll report");
    }
  }
  ble_npl_callout_reset(&heartbeat, ble_npl_time_ms_to_ticks32(100));
}
void Synced() {
  if (ble_hs_util_ensure_addr(0) || ble_hs_id_infer_auto(0, &own_type)) {
    ESP_LOGE(TAG, "No BLE identity available");
    return;
  }
  ble_npl_callout_reset(&heartbeat, ble_npl_time_ms_to_ticks32(100));
}
void Reset(int reason) {
  Invalidate();
  connection = BLE_HS_CONN_HANDLE_NONE;
  ready = connecting = read_pending = false;
  retry_us = esp_timer_get_time() + 3000000;
  ESP_LOGW(TAG, "Host reset (%d)", reason);
}
void HostTask(void *) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}
} // namespace

void InitBleGamepad() {
  unsigned bytes[6];
  int consumed = 0;
  if (std::sscanf(CONFIG_WALLE_BLE_GAMEPAD_MAC, "%2x:%2x:%2x:%2x:%2x:%2x%n",
                  &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4],
                  &bytes[5], &consumed) != 6 ||
      consumed != 17) {
    ESP_LOGE(TAG, "Invalid X3S MAC; BLE controls disabled");
    return;
  }
  target.type = BLE_ADDR_PUBLIC;
  for (int i = 0; i < 6; ++i)
    target.val[5 - i] = bytes[i];
  const esp_err_t err = nimble_port_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NimBLE init failed: %s", esp_err_to_name(err));
    return;
  }
  ble_hs_cfg.sync_cb = Synced;
  ble_hs_cfg.reset_cb = Reset;
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 0;
  ble_hs_cfg.sm_mitm = 0;
  ble_npl_callout_init(&heartbeat, nimble_port_get_dflt_eventq(), Heartbeat,
                       nullptr);
  ESP_LOGI(TAG, "Bound X3S %s; X+HOME to connect",
           CONFIG_WALLE_BLE_GAMEPAD_MAC);
  nimble_port_freertos_init(HostTask);
}
bool ReadBleGamepad(walle::GamepadState &state) {
  portENTER_CRITICAL(&snapshot_lock);
  const bool valid = snapshot.Read(state, esp_timer_get_time());
  portEXIT_CRITICAL(&snapshot_lock);
  return valid;
}
#else
void InitBleGamepad() {}
bool ReadBleGamepad(walle::GamepadState &) { return false; }
#endif
