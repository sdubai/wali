# -*- coding: utf-8 -*-

from huaweicloud_sis.client.rasr_client import RasrClient
from huaweicloud_sis.bean.rasr_request import RasrRequest
from huaweicloud_sis.bean.callback import RasrCallBack
from huaweicloud_sis.bean.sis_config import SisConfig
import json
import os
# Authentication parameters
# Storing ak and sk directly in code or plaintext has security risks. It is recommended to store them in configuration files or environment variables with encryption.
# This example retrieves ak and sk from environment variables for identity verification. Please set environment variables HUAWEICLOUD_SIS_AK, HUAWEICLOUD_SIS_SK, and HUAWEICLOUD_SIS_PROJECT_ID before running this example.
ak = os.getenv("HUAWEICLOUD_SIS_AK")  # Retrieve ak from environment variables. 
assert ak is not None, "Please add ak to your development environment"
sk = os.getenv("HUAWEICLOUD_SIS_SK")  # Retrieve sk from environment variables. 
assert sk is not None, "Please add sk to your development environment"
project_id = ""  # Project ID corresponds to region. 
region = 'cn-north-4'  # Region, e.g., cn-north-4


"""
    todo Please correctly fill in audio format and model properties
    1. The audio format must match exactly.
         For example, if the audio is pcm format with a sampling rate of 8k, the format should be pcm8k16bit.
         If 'audio_format is invalid' is returned, it means the file format is not supported. For supported audio formats, please refer to the API documentation.

    2. The audio sampling rate must match the model properties.
         For example, if the format is pcm16k16bit, but the model property is chinese_8k_common, it will return 'audio_format' is not match model
"""

# Real-time speech recognition parameters
path = ''  # Path to the audio file to be sent, e.g., D:/test.pcm. The SDK also supports sending data as byte streams.
audio_format = 'pcm16k16bit'  # Audio format, e.g., pcm16k16bit. For details, refer to the API documentation
property = 'chinese_16k_general'  # Model property string, e.g., chinese_16k_general. The sampling rate must match the audio format. For details, refer to the API documentation


class MyCallback(RasrCallBack):
    """ Callback class. Users need to implement their own logic in the corresponding methods, with on_response being mandatory to override """

    def on_open(self):
        """ Callback function when websocket connection is successful """
        print('websocket connect success')

    def on_start(self, message):
        """
            Callback function when websocket starts recognition
        :param message: Input information
        :return: -
        """
        print('websocket starts to recognize, %s' % message)

    def on_response(self, message):
        """
            Callback function when websocket returns response results
        :param message: JSON format
        :return: -
        """
        print(json.dumps(message, indent=2, ensure_ascii=False))

    def on_end(self, message):
        """
            Callback function when websocket ends recognition
        :param message: Input information
        :return: -
        """
        print('websocket is ended, %s' % message)

    def on_close(self):
        """ Callback function when websocket is closed """
        print('websocket is closed')

    def on_error(self, error):
        """
            Callback function when websocket encounters an error
        :param error: Error information
        :return: -
        """
        print('websocket meets error, the error is %s' % error)

    def on_event(self, event):
        """
            Callback function when an event occurs
        :param event: Event name
        :return: -
        """
        print('receive event %s' % event)


def rasr_example():
    """ Real-time speech recognition demo """
    # step1 Initialize RasrClient, currently does not support proxy usage
    my_callback = MyCallback()
    config = SisConfig()
    # Set connection timeout, default is 10
    config.set_connect_timeout(10)
    # Set read timeout, default is 10
    config.set_read_timeout(10)
    # Set connect lost timeout, generally no need to set this value under normal concurrency. Default is 10
    config.set_connect_lost_timeout(10)
    # websocket currently does not support proxy usage
    rasr_client = RasrClient(ak=ak, sk=sk, use_aksk=True, region=region, project_id=project_id, callback=my_callback,
                             config=config)
    try:
        # step2 Construct request
        request = RasrRequest(audio_format, property)
        # All parameters can be set to default values
        request.set_add_punc('yes')  # Set whether to add punctuation, yes or no, default is no
        request.set_vad_head(10000)  # Set valid header, [0, 60000], default is 10000
        request.set_vad_tail(500)  # Set valid tail, [0, 3000], default is 500
        request.set_max_seconds(30)  # Set maximum length of a sentence, [1, 60], default is 30
        request.set_interim_results('no')  # Set whether to return intermediate results, yes or no, default is no
        request.set_digit_norm('no')  # Set whether to convert numbers in speech to Arabic numerals, yes or no, default is yes
        # request.set_vocabulary_id('')     # Set hotword table id, if not exists, leave blank, otherwise will report error
        request.set_need_word_info('no')  # Set whether to need word_info, yes or no, default is no
        request.set_need_smooth('no')
        # step3 Choose connection mode
        # rasr_client.short_stream_connect(request)       # Streaming single-sentence mode
        # rasr_client.sentence_stream_connect(request)    # Real-time speech recognition single-sentence mode
        rasr_client.continue_stream_connect(request)  # Real-time speech recognition continuous mode

        # use enterprise_project_Id
        # headers = {'Enterprise-Project-Id': 'your enterprise project id'}
        # rasr_client.continue_stream_connect(request, headers)

        # step4 Send audio
        rasr_client.send_start()
        # In continuous mode, audio can be sent multiple times, format is byte array
        with open(path, 'rb') as f:
            data = f.read()
            rasr_client.send_audio(data)  # Optional parameters: byte_len and sleep_time, recommended to use default values
        rasr_client.send_end()
    except Exception as e:
        print('rasr error', e)
    finally:
        # step5 Close client, must close after use, otherwise server will report error and disconnect after 20 seconds of no data.
        rasr_client.close()


if __name__ == '__main__':
    rasr_example()
