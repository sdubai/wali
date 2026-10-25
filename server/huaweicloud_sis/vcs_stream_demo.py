# -*- coding: utf-8 -*-

from huaweicloud_sis.client.vcs_stream_client import VcsStreamClient
from huaweicloud_sis.bean.vcs_stream_request import VcsStreamRequest
from huaweicloud_sis.bean.callback import VcsStreamCallBack
from huaweicloud_sis.bean.sis_config import SisConfig
import os
# 鉴权参数
# 认证用的ak和sk硬编码到代码中或者明文存储都有很大的安全风险，建议在配置文件或者环境变量中密文存放，使用时解密，确保安全；
ak = os.getenv("HUAWEICLOUD_SIS_AK")             # 从环境变量获取ak 参考https://support.huaweicloud.com/sdkreference-sis/sis_05_0003.html
assert ak is not None, "Please add ak in your develop environment"
sk = os.getenv("HUAWEICLOUD_SIS_SK")             # 从环境变量获取sk 参考https://support.huaweicloud.com/sdkreference-sis/sis_05_0003.html
assert sk is not None, "Please add sk in your develop environment"
project_id = ""     # project id 同region一一对应，参考https://support.huaweicloud.com/api-sis/sis_03_0008.html
region = ''  # region，支持上海一region，cn-east-3

# 可选参数,传入VcsStreamClient(ak=ak, sk=sk, use_aksk=True, region=region, project_id=project_id, callback=my_callback,config=config);
service_endpoint = "" # 可选，自定义endpoint，传入VcsClient(ak, sk, region, project_id, sis_config=config)

text_path=""  # 可选 待合成文本存储路径,例如"D://text.txt",要求文本中，每行句子不超过300字符
audio_path = ""  # 待合成的音频保存路径，如test.pcm
# 使用该脚本前,保证要使用的声音已经注册,voice_name为声音名称。若没有注册声音,请使用vcs_register_and_request.py脚本，注册声音和查询声音
voice_name = '' # 音色名称，可以使用注册音色或预置音色

class MyCallback(VcsStreamCallBack):
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


def vcs_stream_example(audio_path, voice_name):
    """
        声音复刻流式demo
    """
    # step1 初始化VcsStreamClient
    my_callback = MyCallback(audio_path)
    config = SisConfig()
    # 设置连接超时,默认是10
    config.set_connect_timeout(10)
    # 设置读取超时, 默认是10
    config.set_read_timeout(10)
    # 设置websocket等待时间
    config.set_websocket_wait_time(20)
    # websocket暂时不支持使用代理
    vcs_stream_client = VcsStreamClient(ak=ak, sk=sk, use_aksk=True, region=region, project_id=project_id, callback=my_callback,
                             config=config)

    # step2 构造请求
    vcs_stream_request = VcsStreamRequest()
    # 设置音色字符串，可以使用注册音色或预置音色
    vcs_stream_request.set_voice_name(voice_name)
    # 设置音频格式, pcm or mp3, 可参考api文档
    vcs_stream_request.set_audio_format('pcm')
    # 设置采样率，8000 or 16000 or 24000, 默认16000
    vcs_stream_request.set_sample_rate('16000')
    # 设置音量，[0, 100]，默认50
    vcs_stream_request.set_volume(50)
    # 设置音高, [-500, 500], 默认0
    vcs_stream_request.set_pitch(0)
    # 设置音速, [-500, 500], 默认0
    vcs_stream_request.set_speed(0)
    # ONLY模式下，用户发送一次文本，服务端流式返回语音数据；MULTI模式下，支持用户多次发送文本，服务端流式返回语音数据，可用于大模型输出实时播报等场景。
    text_pieces = 'MULTI'
    vcs_stream_request.set_text_pieces(text_pieces)
    if text_pieces == 'MULTI':

        # 发送开始请求、发送文本、发送end请求
        vcs_stream_client.sendStart(vcs_stream_request)

        # 发送文本，可连续发送多次，合成结果可以通过监听器获取
        # 方法一
        vcs_stream_client.sendMsg("你好")
        vcs_stream_client.sendMsg("欢迎使用华为云语音交互服务。")
        # 方法二
        # vcs_stream_client.send_msg_batch(text_path)  # 传入待合成文本储存路径

        # 文本发送完毕，发送结束指令
        vcs_stream_client.sendEnd()
    else:
        # ONLY模式下，发送一次文本
        vcs_stream_request.set_text("你好，欢迎使用华为云语音交互服务。")
        vcs_stream_client.synthesis(vcs_stream_request)


if __name__ == '__main__':
    vcs_stream_example(audio_path, voice_name)
