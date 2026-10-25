# -*- coding: utf-8 -*-

from huaweicloud_sis.client.flash_lasr_client import FlashLasrClient
from huaweicloud_sis.bean.flash_lasr_request import FlashLasrRequest
from huaweicloud_sis.exception.exceptions import ClientException
from huaweicloud_sis.exception.exceptions import ServerException
from huaweicloud_sis.bean.sis_config import SisConfig
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

region = ''         # region，如cn-north-4
obs_bucket_name = ''    # obs桶名
obs_object_key = ''     # obs对象的key
audio_format = ''       # 文件格式，如wav等， 支持格式详见api文档
property = ''           # 属性字符串，language_sampleRate_domain, 如chinese_8k_common, 详见api文档


def flash_lasr_example():
    """ 录音文件极速版示例 """
    # step1 初始化客户端
    config = SisConfig()
    config.set_connect_timeout(10)  # 设置连接超时
    config.set_read_timeout(10)  # 设置读取超时
    # 设置代理，使用代理前一定要确保代理可用。 代理格式可为[host, port] 或 [host, port, username, password]
    # config.set_proxy(proxy)
    client = FlashLasrClient(ak, sk, region, project_id, sis_config=config)

    # step2 构造请求
    asr_request = FlashLasrRequest()
    # 以下参数必选
    # 设置存放音频的桶名，必选
    asr_request.set_obs_bucket_name(obs_bucket_name)
    # 设置桶内音频对象名，必选
    asr_request.set_obs_object_key(obs_object_key)
    # 设置格式，必选
    asr_request.set_audio_format(audio_format)
    # 设置属性，必选
    asr_request.set_property(property)

    # 以下参数可选
    # 设置是否添加标点，yes or no，默认no
    asr_request.set_add_punc('yes')
    # 设置是否将语音中数字转写为阿拉伯数字，yes or no，默认yes
    asr_request.set_digit_norm('yes')
    # 设置是否添加热词表id，没有则不填
    # asr_request.set_vocabulary_id(None)
    # 设置是否需要word_info，yes or no, 默认no
    asr_request.set_need_word_info('no')
    # 设置是否只识别收个声道的音频数据，默认no
    asr_request.set_first_channel_only('no')

    # step3 发送请求，返回结果,返回结果为json格式
    result = client.get_flash_lasr_result(asr_request)
    # use enterprise_project_Id
    # headers = {'Enterprise-Project-Id': 'your enterprise project id', 'Content-Type': 'application/json'}
    # result = client.get_flash_lasr_result(asr_request, headers)

    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    try:
        flash_lasr_example()
    except ClientException as e:
        print(e)
    except ServerException as e:
        print(e)
