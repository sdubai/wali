#include "application.h"
#include "assets/lang_config.h"
#include "audio/audio_service.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "motion_link.h"
#include "ogg_demuxer.h"
#include "walle_motion.h"
#include "wifi_board.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/task.h>

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
