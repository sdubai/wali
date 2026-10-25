# -*- coding: utf-8 -*-

from huaweicloud_sis.client.tts_client import TtsCustomizationClient
from huaweicloud_sis.bean.tts_request import TtsCustomRequest
from huaweicloud_sis.bean.sis_config import SisConfig
from huaweicloud_sis.exception.exceptions import ClientException
from huaweicloud_sis.exception.exceptions import ServerException
import json
import os
# 鉴权参数
# 认证用的ak和sk硬编码到代码中或者明文存储都有很大的安全风险，建议在配置文件或者环境变量中密文存放，使用时解密，确保安全； 
# 本示例以ak和sk保存在环境变量中来实现身份验证为例，运行本示例前请先在本地环境中设置环境变量HUAWEICLOUD_SIS_AK/HUAWEICLOUD_SIS_SK/HUAWEICLOUD_SIS_PROJECT_ID。
ak = os.getenv("HUAWEICLOUD_SIS_AK")             # 从环境变量获取ak 参考https://support.huaweicloud.com/sdkreference-sis/sis_05_0003.html
assert ak is not None, "Please add ak in your develop environment"
sk = os.getenv("HUAWEICLOUD_SIS_SK")             # 从环境变量获取sk 参考https://support.huaweicloud.com/sdkreference-sis/sis_05_0003.html
assert sk is not None, "Please add sk in your develop environment"
project_id = ""     # project id 同region一一对应，参考https://support.huaweicloud.com/api-sis/sis_03_0008.html


def ttsc_example():
    """ 语音合成demo """
    region = ''         # region，如cn-north-4
    text = ''           # 待合成文本，不超过500字
    path = ''           # 保存路径，如D:/test.wav。 可在设置中选择不保存本地

    # step1 初始化客户端
    config = SisConfig()
    config.set_connect_timeout(10)       # 设置连接超时，单位s
    config.set_read_timeout(10)          # 设置读取超时，单位s
    # 设置代理，使用代理前一定要确保代理可用。 代理格式可为[host, port] 或 [host, port, username, password]
    # config.set_proxy(proxy)
    ttsc_client = TtsCustomizationClient(ak, sk, region, project_id, sis_config=config)

    # step2 构造请求
    ttsc_request = TtsCustomRequest(text)
    # 设置请求，所有参数均可不设置，使用默认参数
    # 设置属性字符串， language_speaker_domain, 默认chinese_xiaoyan_common, 参考api文档
    ttsc_request.set_property('chinese_xiaoyan_common')
    # 设置音频格式，默认wav，可选mp3和pcm
    ttsc_request.set_audio_format('wav')
    # 设置采样率，8000 or 16000, 默认8000
    ttsc_request.set_sample_rate('8000')
    # 设置音量，[0, 100]，默认50
    ttsc_request.set_volume(50)
    # 设置音高, [-500, 500], 默认0
    ttsc_request.set_pitch(0)
    # 设置音速, [-500, 500], 默认0
    ttsc_request.set_speed(0)
    # 设置是否保存，默认False
    ttsc_request.set_saved(True)
    # 设置保存路径，只有设置保存，此参数才生效
    ttsc_request.set_saved_path(path)

    # step3 发送请求，返回结果。如果设置保存，可在指定路径里查看保存的音频。
    result = ttsc_client.get_ttsc_response(ttsc_request)
    # use enterprise_project_Id
    # headers = {'Enterprise-Project-Id': 'your enterprise project id', 'Content-Type': 'application/json'}
    # result = ttsc_client.get_ttsc_response(ttsc_request, headers)
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    try:
        ttsc_example()
    except ClientException as e:
        print(e)
    except ServerException as e:
        print(e)
