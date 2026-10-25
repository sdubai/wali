# -*- coding: utf-8 -*-

from huaweicloud_sis.client.rtts_client import RttsClient
from huaweicloud_sis.bean.rtts_request import RttsRequest
from huaweicloud_sis.bean.callback import RttsCallBack
from huaweicloud_sis.bean.sis_config import SisConfig
import os
# 鉴权参数
# 认证用的ak和sk硬编码到代码中或者明文存储都有很大的安全风险，建议在配置文件或者环境变量中密文存放，使用时解密，确保安全； 
# 本示例以ak和sk保存在环境变量中来实现身份验证为例，运行本示例前请先在本地环境中设置环境变量HUAWEICLOUD_SIS_AK/HUAWEICLOUD_SIS_SK/HUAWEICLOUD_SIS_PROJECT_ID。
ak = os.getenv("HUAWEICLOUD_SIS_AK")             # 从环境变量获取ak 参考https://support.huaweicloud.com/sdkreference-sis/sis_05_0003.html
assert ak is not None, "Please add ak in your develop environment"
sk = os.getenv("HUAWEICLOUD_SIS_SK")             # 从环境变量获取sk 参考https://support.huaweicloud.com/sdkreference-sis/sis_05_0003.html
assert sk is not None, "Please add sk in your develop environment"
project_id = ""     # project id 同region一一对应，参考https://support.huaweicloud.com/api-sis/sis_03_0008.html
region = ''  # region，如cn-north-4


text = ''  # 待合成的文本
path = ''  # 待合成的音频保存路径，如test.pcm


class MyCallback(RttsCallBack):
    """ 回调类，用户需要在对应方法中实现自己的逻辑，其中on_response必须重写 """

    def __init__(self, save_path):
        self._f = open(save_path, 'wb')

    def on_open(self):
        """ websocket连接成功会回调此函数 """
        print('websocket connect success')

    def on_start(self, message):
        """
            websocket 开始识别回调此函数
        :param message: 传入信息
        :return: -
        """
        print('webscoket start to recognize, %s' % message)

    def on_response(self, data):
        """
            回调返回的音频合成数据，byte数组格式
        :param data byte数组，合成的音频数据
        :return: -
        """
        print('receive data %d' % len(data))
        self._f.write(data)

    def on_end(self, message):
        """
            websocket 结束识别回调此函数
        :param message: 传入信息
        :return: -
        """
        print('websocket is ended, %s' % message)
        self._f.close()

    def on_close(self):
        """ websocket关闭会回调此函数 """
        print('websocket is closed')
        self._f.close()

    def on_error(self, error):
        """
            websocket出错回调此函数
        :param error: 错误信息
        :return: -
        """
        print('websocket meets error, the error is %s' % error)
        self._f.close()


def rtts_example():
    """ 
        实时语音合成demo
        1. RttsClient 只能发送一次文本，如果需要多次发送文本，需要新建多个RttsClient 和 callback
        2. 识别完成后服务端会返回end响应。
        3. 当识别出现问题时，会触发on_error回调，同时会关闭websocket。
        4. 实时语音合成会多次返回结果，demo的处理方式是将多次返回结果集合在一个音频文件里。
    """
    # step1 初始化RttsClient, 暂不支持使用代理
    my_callback = MyCallback(path)
    config = SisConfig()
    # 设置连接超时,默认是10
    config.set_connect_timeout(10)
    # 设置读取超时, 默认是10
    config.set_read_timeout(10)
    # 设置websocket等待时间
    config.set_websocket_wait_time(20)
    # websocket暂时不支持使用代理
    rtts_client = RttsClient(ak=ak, sk=sk, use_aksk=True, region=region, project_id=project_id, callback=my_callback,
                             config=config)

    # step2 构造请求
    rtts_request = RttsRequest(text)
    # 设置属性字符串， language_speaker_domain, 默认chinese_xiaoyan_common, 参考api文档
    rtts_request.set_property('chinese_xiaoyan_common')
    # 设置音频格式为pcm
    rtts_request.set_audio_format('pcm')
    # 设置采样率，8000 or 16000, 默认8000
    rtts_request.set_sample_rate('8000')
    # 设置音量，[0, 100]，默认50
    rtts_request.set_volume(50)
    # 设置音高, [-500, 500], 默认0
    rtts_request.set_pitch(0)
    # 设置音速, [-500, 500], 默认0
    rtts_request.set_speed(0)

    # step3 合成
    rtts_client.synthesis(rtts_request)
    # use enterprise_project_Id
    # headers = {'Enterprise-Project-Id': 'your enterprise project id'}
    # rtts_client.synthesis(rtts_request, headers)


if __name__ == '__main__':
    rtts_example()
