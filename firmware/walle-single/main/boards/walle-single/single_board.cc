#include "application.h"
#include "assets/lang_config.h"
#include "audio/audio_service.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/oled_display.h"
#include "motion_link.h"
#include "ogg_demuxer.h"
#include "walle_motion.h"
#include "wifi_board.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/task.h>
#include <led_strip.h>

class SharedClockCodec : public NoAudioCodecDuplex {
public:
  SharedClockCodec()
      : NoAudioCodecDuplex(32000, 32000, AUDIO_BCLK, AUDIO_WS, AUDIO_DOUT,
                           AUDIO_DIN) {
    output_volume_ = 25;
  }
  void Start() override {
    AudioCodec::Start();
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
  }
  void EnableInput(bool enable) override { AudioCodec::EnableInput(enable); }
  void EnableOutput(bool enable) override { AudioCodec::EnableOutput(enable); }
};

void WalleCancelVoiceDrive() { MotionLink::CancelLocalDrive(); }

class WalleSingleBoard : public WifiBoard {
  i2c_master_bus_handle_t oled_bus_ = nullptr;
  Display *oled_display_ = nullptr;

  void TurnOffBoardRgb() {
    led_strip_config_t strip = {};
    strip.strip_gpio_num = WALLE_RGB_GPIO;
    strip.max_leds = 1;
    strip.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    strip.led_model = LED_MODEL_WS2812;
    led_strip_rmt_config_t rmt = {};
    rmt.resolution_hz = 10000000;
    led_strip_handle_t led = nullptr;
    esp_err_t err = led_strip_new_rmt_device(&strip, &rmt, &led);
    if (err == ESP_OK) {
      err = led_strip_clear(led);
      led_strip_del(led);
    }
    if (err != ESP_OK) {
      ESP_LOGW("WalleRGB", "Could not clear RGB on GPIO48: %s",
               esp_err_to_name(err));
      return;
    }
    // Hold the data input quiet after transmitting the off frame.
    gpio_reset_pin(WALLE_RGB_GPIO);
    gpio_set_direction(WALLE_RGB_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(WALLE_RGB_GPIO, 0);
    ESP_LOGI("WalleRGB", "Board RGB cleared on GPIO48");
  }

  void InitializeOled() {
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_1;
    bus.sda_io_num = WALLE_OLED_SDA;
    bus.scl_io_num = WALLE_OLED_SCL;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    esp_err_t err = i2c_new_master_bus(&bus, &oled_bus_);
    if (err != ESP_OK) {
      ESP_LOGW("WalleOLED", "I2C1 unavailable: %s; continuing without OLED",
               esp_err_to_name(err));
      return;
    }
    uint8_t address = 0;
    for (uint8_t candidate : {0x3c, 0x3d}) {
      if (i2c_master_probe(oled_bus_, candidate, 50) == ESP_OK) {
        address = candidate;
        break;
      }
    }
    if (!address) {
      ESP_LOGW("WalleOLED",
               "No OLED at 0x3C/0x3D on SDA47/SCL42; check SSD1306 wiring");
      i2c_del_master_bus(oled_bus_);
      oled_bus_ = nullptr;
      return;
    }
    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_handle_t panel = nullptr;
    esp_lcd_panel_io_i2c_config_t io_cfg = {};
    io_cfg.dev_addr = address;
    io_cfg.scl_speed_hz = 400000;
    io_cfg.control_phase_bytes = 1;
    io_cfg.dc_bit_offset = 6;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;
    err = esp_lcd_new_panel_io_i2c(oled_bus_, &io_cfg, &io);
    if (err == ESP_OK) {
      esp_lcd_panel_ssd1306_config_t vendor = {};
      vendor.height = WALLE_OLED_HEIGHT;
      esp_lcd_panel_dev_config_t dev = {};
      dev.reset_gpio_num = GPIO_NUM_NC;
      dev.bits_per_pixel = 1;
      dev.vendor_config = &vendor;
      err = esp_lcd_new_panel_ssd1306(io, &dev, &panel);
    }
    if (err == ESP_OK)
      err = esp_lcd_panel_reset(panel);
    if (err == ESP_OK)
      err = esp_lcd_panel_init(panel);
    if (err == ESP_OK)
      err = esp_lcd_panel_invert_color(panel, false);
    if (err == ESP_OK)
      err = esp_lcd_panel_disp_on_off(panel, true);
    if (err != ESP_OK) {
      ESP_LOGW("WalleOLED", "OLED init failed: %s; continuing without display",
               esp_err_to_name(err));
      if (panel)
        esp_lcd_panel_del(panel);
      if (io)
        esp_lcd_panel_io_del(io);
      i2c_del_master_bus(oled_bus_);
      oled_bus_ = nullptr;
      return;
    }
    oled_display_ = new OledDisplay(io, panel, WALLE_OLED_WIDTH,
                                    WALLE_OLED_HEIGHT, false, false);
    ESP_LOGI("WalleOLED", "SSD1306 128x64 ready at 0x%02X, I2C1 SDA47 SCL42",
             address);
  }

  Button key1_{GPIO_NUM_38, false, 3000};
  Button key2_{GPIO_NUM_39};
  Button key3_{GPIO_NUM_40};
  Button key4_{GPIO_NUM_41};
  MotionLink *link_ = nullptr;
  std::atomic<bool> playing_{false};
  std::atomic<unsigned> generation_{0};
  TaskHandle_t sound_task_ = nullptr;
  void StopShow() {
    generation_.fetch_add(1);
    playing_.store(false);
    MotionLink::CancelLocalDrive();
    WalleMotionStop();
    Application::GetInstance().GetAudioService().ResetDecoder();
  }
  static void SoundWorker(void *ctx) {
    static_cast<WalleSingleBoard *>(ctx)->SoundLoop();
  }
  void SoundLoop() {
    for (;;) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      const auto epoch = generation_.load();
      if (!playing_.load())
        continue;
      auto &audio = Application::GetInstance().GetAudioService();
      // A short built-in test sound; replace startup.ogg with Ogg/Opus music.
      extern const char startup_start[] asm("_binary_startup_ogg_start");
      extern const char startup_end[] asm("_binary_startup_ogg_end");
      auto demux = std::make_unique<OggDemuxer>();
      demux->OnPacket(
          [&](const uint8_t *data, int rate, int duration, size_t size) {
            while (generation_.load() == epoch && playing_.load()) {
              auto packet = std::make_unique<AudioStreamPacket>();
              packet->sample_rate = rate;
              packet->frame_duration = duration;
              packet->payload.assign(data, data + size);
              if (audio.PushPacketToDecodeQueue(std::move(packet), false))
                break;
              vTaskDelay(pdMS_TO_TICKS(10));
            }
          });
      // Feed short chunks, bounding cancellation latency even for long music
      // files.
      const auto *data = reinterpret_cast<const uint8_t *>(startup_start);
      const size_t size = startup_end - startup_start;
      demux->Reset();
      for (size_t i = 0; i < size && generation_.load() == epoch; i += 1024)
        demux->Process(data + i, std::min<size_t>(1024, size - i));
      const int64_t start = esp_timer_get_time();
      while (
          generation_.load() == epoch &&
          (esp_timer_get_time() - start < 9000000 || !audio.IsPlaybackIdle()))
        vTaskDelay(pdMS_TO_TICKS(20));
      if (generation_.load() == epoch) {
        Application::GetInstance().Schedule([this, epoch]() {
          if (generation_.load() == epoch)
            StopShow();
        });
      }
    }
  }

public:
  WalleSingleBoard() {
    TurnOffBoardRgb();
    InitializeOled();
    WalleMotionStart();
    link_ = new MotionLink();
    link_->RegisterTools();
    ESP_ERROR_CHECK(xTaskCreate(SoundWorker, "startup_sound", 6144, this, 3,
                                &sound_task_) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    key1_.OnClick([this]() {
      Application::GetInstance().Schedule([this]() {
        if (playing_.load())
          StopShow();
        Application::GetInstance().ToggleChatState();
      });
    });
    key1_.OnLongPress([this]() {
      Application::GetInstance().Schedule([this]() {
        StopShow();
        EnterWifiConfigMode();
      });
    });
    key2_.OnClick([this]() {
      Application::GetInstance().Schedule([this]() {
        auto &app = Application::GetInstance();
        if (playing_.load()) {
          StopShow();
          return;
        }
        if (app.GetDeviceState() != kDeviceStateIdle) {
          ESP_LOGW("WalleSingle",
                   "Finish conversation before starting the show");
          return;
        }
        app.GetAudioService().ResetDecoder();
        generation_.fetch_add(1);
        playing_.store(true);
        MotionLink::CancelLocalDrive();
        WalleMotionSubmit("{\"cmd\":\"action\",\"name\":\"introduce\"}");
        xTaskNotifyGive(sound_task_);
      });
    });
    // K3 and K4 are deliberately unassigned.
  }
  Display *GetDisplay() override {
    return oled_display_ ? oled_display_ : Board::GetDisplay();
  }
  AudioCodec *GetAudioCodec() override {
    static SharedClockCodec codec;
    return &codec;
  }
  void StartNetwork() override {
    WifiBoard::StartNetwork();
    link_->StartNetwork();
  }
};
DECLARE_BOARD(WalleSingleBoard);
