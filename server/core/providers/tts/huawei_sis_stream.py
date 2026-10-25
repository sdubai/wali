import os
import queue
import asyncio
import traceback

from config.logger import setup_logging
from core.utils.tts import MarkdownCleaner
from core.providers.tts.base import TTSProviderBase
from core.providers.tts.dto.dto import SentenceType, ContentType, InterfaceType

from huaweicloud_sis.client.rtts_client import RttsClient
from huaweicloud_sis.bean.rtts_request import RttsRequest
from huaweicloud_sis.bean.callback import RttsCallBack
from huaweicloud_sis.bean.sis_config import SisConfig

TAG = __name__
logger = setup_logging()


class _StreamingCallback(RttsCallBack):
    def __init__(self, provider, text, sentence_id):
        self.provider = provider
        self.text = text
        self.sentence_id = sentence_id
        self.first_chunk = True
        self.error = None
        self.audio_bytes = 0
        self.resampled_bytes = 0
        self._rate_state = None

    def on_response(self, data):
        if not data or self.provider.conn.client_abort:
            return
        if self.first_chunk:
            self.provider.tts_audio_queue.put(
                (SentenceType.FIRST, None, self.text, self.sentence_id)
            )
            self.first_chunk = False
        self.audio_bytes += len(data)
        source_rate = int(self.provider.sample_rate)
        target_rate = int(self.provider.conn.sample_rate)
        if source_rate != target_rate:
            import audioop
            data, self._rate_state = audioop.ratecv(
                data, 2, 1, source_rate, target_rate, self._rate_state
            )
        self.resampled_bytes += len(data)
        self.provider.opus_encoder.encode_pcm_to_opus_stream(
            data, False, self.provider.handle_opus
        )

    def on_response_subtitle(self, subtitle):
        pass

    def on_error(self, error):
        self.error = str(error)
        logger.bind(tag=TAG).error(f"Huawei SIS TTS error: {self.error}")


class _CollectCallback(RttsCallBack):
    def __init__(self):
        self.chunks = []
        self.error = None

    def on_response(self, data):
        if data:
            self.chunks.append(data)

    def on_response_subtitle(self, subtitle):
        pass

    def on_error(self, error):
        self.error = str(error)


class TTSProvider(TTSProviderBase):
    TTS_PARAM_CONFIG = [
        ("ttsVolume", "volume", 0, 100, 70, int),
        ("ttsRate", "speed", -500, 500, 0, int),
        ("ttsPitch", "pitch", -500, 500, 0, int),
    ]

    def __init__(self, config, delete_audio_file):
        super().__init__(config, delete_audio_file)
        self.interface_type = InterfaceType.SINGLE_STREAM
        self.ak = config.get("ak") or os.getenv("HUAWEI_SIS_AK")
        self.sk = config.get("sk") or os.getenv("HUAWEI_SIS_SK")
        self.region = config.get("region") or os.getenv("HUAWEI_SIS_REGION", "cn-north-4")
        self.project_id = config.get("project_id") or os.getenv("HUAWEI_SIS_PROJECT_ID")
        self.voice = config.get("voice", "chinese_huaxiaoru_common")
        self.sample_rate = str(config.get("sample_rate", 16000))
        self.speed = int(config.get("speed", 0))
        self.pitch = int(config.get("pitch", 0))
        self.volume = int(config.get("volume", 70))
        self.service_endpoint = config.get("service_endpoint") or None
        self.report_on_last = False
        self._active_client = None

        if not self.ak or not self.sk or not self.project_id:
            raise ValueError(
                "Huawei SIS TTS requires HUAWEI_SIS_AK, HUAWEI_SIS_SK and HUAWEI_SIS_PROJECT_ID"
            )

    def _new_request(self, text):
        request = RttsRequest(text)
        request.set_property(self.voice)
        request.set_audio_format("pcm")
        request.set_sample_rate(self.sample_rate)
        request.set_speed(self.speed)
        request.set_pitch(self.pitch)
        request.set_volume(self.volume)
        return request

    def _new_client(self, callback):
        cfg = SisConfig()
        cfg.set_connect_timeout(min(self.tts_timeout, 10))
        cfg.set_websocket_wait_time(self.tts_timeout)
        return RttsClient(
            ak=self.ak,
            sk=self.sk,
            use_aksk=True,
            region=self.region,
            project_id=self.project_id,
            callback=callback,
            config=cfg,
            service_endpoint=self.service_endpoint,
        )

    def _synthesize_stream(self, text, sentence_id):
        text = MarkdownCleaner.clean_markdown(text).strip()
        if self._correct_words_pattern:
            text = self._correct_words_pattern.sub(
                lambda m: self.correct_words[m.group(0)], text
            )
        if not text or self.conn.client_abort:
            return

        callback = _StreamingCallback(self, text, sentence_id)
        client = self._new_client(callback)
        self._active_client = client
        try:
            client.synthesis(self._new_request(text))
            if callback.error:
                raise RuntimeError(callback.error)
            if callback.audio_bytes == 0 and not self.conn.client_abort:
                raise RuntimeError("Huawei SIS returned no audio")
            logger.bind(tag=TAG).info(
                f"Huawei SIS generated {callback.audio_bytes} PCM bytes, "
                f"resampled to {callback.resampled_bytes} bytes at {self.conn.sample_rate} Hz"
            )
        finally:
            self._active_client = None

    def tts_text_priority_thread(self):
        while not self.conn.stop_event.is_set():
            try:
                message = self.tts_text_queue.get(timeout=1)
                if self.conn.client_abort or message.sentence_id != self.conn.sentence_id:
                    continue

                if message.sentence_type == SentenceType.FIRST:
                    self.current_sentence_id = message.sentence_id
                    self.reset_stream_state()
                    self.tts_text_buff = []
                    self.processed_chars = 0
                    self.is_first_sentence = True
                    self.tts_audio_first_sentence = True
                    self.before_stop_play_files.clear()

                if ContentType.TEXT == message.content_type:
                    self.tts_text_buff.append(message.content_detail)
                    segment_text = self._get_segment_text()
                    if segment_text:
                        self._synthesize_stream(segment_text, message.sentence_id)

                elif ContentType.FILE == message.content_type:
                    self._process_remaining_text_stream(opus_handler=self.handle_opus)
                    if message.content_file and os.path.exists(message.content_file):
                        self._process_audio_file_stream(
                            message.content_file, callback=self.handle_opus
                        )

                if message.sentence_type == SentenceType.LAST:
                    full_text = "".join(self.tts_text_buff)
                    remaining = full_text[self.processed_chars:].strip()
                    self.tts_text_buff = []
                    self.processed_chars = 0
                    if remaining:
                        self._synthesize_stream(remaining, message.sentence_id)
                    self.tts_audio_queue.put(
                        (SentenceType.LAST, [], message.content_detail, message.sentence_id)
                    )

            except queue.Empty:
                continue
            except Exception as exc:
                logger.bind(tag=TAG).error(
                    f"Huawei SIS TTS failed: {exc}; {traceback.format_exc()}"
                )

    async def text_to_speak(self, text, output_file):
        def collect():
            callback = _CollectCallback()
            client = self._new_client(callback)
            client.synthesis(self._new_request(text))
            if callback.error:
                raise RuntimeError(callback.error)
            return b"".join(callback.chunks)

        pcm = await asyncio.to_thread(collect)
        import io
        import wave
        if output_file:
            with wave.open(output_file, "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(int(self.sample_rate))
                wav.writeframes(pcm)
            return output_file
        buffer = io.BytesIO()
        with wave.open(buffer, "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(int(self.sample_rate))
            wav.writeframes(pcm)
        return buffer.getvalue()

    async def close(self):
        client = self._active_client
        if client is not None:
            try:
                client._close()
            except Exception:
                pass
        self._active_client = None
        await super().close()
