# -*- coding: utf-8 -*-

from huaweicloud_sis.client.asr_client import AsrCustomizationClient
from huaweicloud_sis.bean.asr_request import AsrCustomShortRequest
from huaweicloud_sis.exception.exceptions import ClientException
from huaweicloud_sis.exception.exceptions import ServerException
from huaweicloud_sis.utils import io_utils
from huaweicloud_sis.bean.sis_config import SisConfig
import json
import os
# Authentication parameters
# Storing ak and sk directly in code or plaintext poses significant security risks. It is recommended to store them in configuration files or environment variables with encryption.
# This example retrieves ak and sk from environment variables for identity verification. Please set environment variables HUAWEICLOUD_SIS_AK, HUAWEICLOUD_SIS_SK, and HUAWEICLOUD_SIS_PROJECT_ID before running this example.
ak = os.getenv("HUAWEICLOUD_SIS_AK")  # Retrieve ak from environment variables. 
assert ak is not None, "Please add ak to your development environment"
sk = os.getenv("HUAWEICLOUD_SIS_SK")  # Retrieve sk from environment variables. 
assert sk is not None, "Please add sk to your development environment"
project_id = ""  # Project ID corresponds to region. 
region = ''  # Region, e.g., cn-north-4

"""
    todo Please correctly fill in audio format and model properties
    1. The audio format must match exactly.
         For example, if the audio is wav format, the format should be wav. For more details, refer to the API documentation.
         If the audio is pcm format with a sampling rate of 8k, the format should be pcm8k16bit.
         If 'audio_format is invalid' is returned, it means the file format is not supported. For supported audio formats, please refer to the API documentation.

    2. The audio sampling rate must match the model properties.
         For example, if the format is pcm16k16bit, but the model property is chinese_8k_common, it will return 'audio_format' is not match model.
         Similarly, if the audio is wav with 16k sampling rate, but the property is chinese_8k_common, it will also return 'audio_format' is not match model.
"""

# Parameters for short audio recognition, audio file is passed as base64 encoding, suitable for audio within 1 minute
path = ''  # File location, specify the full path, e.g., D:/test.wav
audio_format = ''  # Audio format, e.g., wav, etc. For details, refer to the API documentation
property = 'chinese_16k_general'  # Model property string, e.g., chinese_16k_general. The sampling rate must match the audio format. For details, refer to the API documentation


def sasr_example():
    """ Example for short audio recognition """
    # step1 Initialize client
    config = SisConfig()
    config.set_connect_timeout(10)  # Set connection timeout
    config.set_read_timeout(10)  # Set read timeout
    # Set proxy, ensure proxy is available before using. Proxy format can be [host, port] or [host, port, username, password]
    # config.set_proxy(proxy)
    asr_client = AsrCustomizationClient(ak, sk, region, project_id, sis_config=config)

    # step2 Construct request
    data = io_utils.encode_file(path)
    asr_request = AsrCustomShortRequest(audio_format, property, data)
    # All parameters can be set to default values
    # Set whether to add punctuation, yes or no, default is no
    asr_request.set_add_punc('yes')
    # Set whether to convert numbers in speech to Arabic numerals, yes or no, default is yes
    asr_request.set_digit_norm('yes')
    # Set whether to use vocabulary_id, leave blank if not exists
    # asr_request.set_vocabulary_id(None)
    # Set whether to need word_info, yes or no, default is no
    asr_request.set_need_word_info('no')
    # Set whether to enable automatic language detection, yes or no, default is no
    # Enabling this switch will cause the backend model to determine the corresponding language based on the actual content of the audio
    # and select the appropriate model for recognition. It supports automatic detection of Mandarin, English and Arabic.
    # Note that language detection is not always accurate, so if the language is already known, it is not recommended to enable this switch
    # as it may affect accuracy.
    asr_request.set_auto_language_detect("no")
    # step3 Send request and get result, result is in json format
    result = asr_client.get_short_response(asr_request)
    # use enterprise_project_Id
    # headers = {'Enterprise-Project-Id': 'your enterprise project id', 'Content-Type': 'application/json'}
    # result = asr_client.get_short_response(asr_request, headers)
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    try:
        sasr_example()
    except ClientException as e:
        print(e)
    except ServerException as e:
        print(e)