import base64
import os
from huaweicloud_sis.bean.vcs_register_voice_request import RegisterVoiceRequest
from huaweicloud_sis.bean.vcs_synthesis_request import VcsSynthesisRequest
from huaweicloud_sis.client.vcs_client import VcsClient
from huaweicloud_sis.bean.sis_config import SisConfig

# 鉴权参数
# 认证用的ak和sk硬编码到代码中或者明文存储都有很大的安全风险，建议在配置文件或者环境变量中密文存放，使用时解密，确保安全； 
# 本示例以ak和sk保存在环境变量中来实现身份验证为例，运行本示例前请先在本地环境中设置环境变量HUAWEICLOUD_SIS_AK/HUAWEICLOUD_SIS_SK/HUAWEICLOUD_SIS_PROJECT_ID。
ak = os.getenv("HUAWEICLOUD_SIS_AK")             # 从环境变量获取ak 参考https://support.huaweicloud.com/sdkreference-sis/sis_05_0003.html
assert ak is not None, "Please add ak in your develop environment"
sk = os.getenv("HUAWEICLOUD_SIS_SK")             # 从环境变量获取sk 参考https://support.huaweicloud.com/sdkreference-sis/sis_05_0003.html
assert sk is not None, "Please add sk in your develop environment"
region = ''         # region，支持上海一region，cn-east-3
project_id = ""     # project id 同region一一对应，参考https://support.huaweicloud.com/api-sis/sis_03_0008.html

service_endpoint = "" # 可选，自定义endpoint，传入VcsClient(ak, sk, region, project_id, sis_config=config)
# 使用该脚本前,保证要使用的声音已经注册,voice_name为声音名称。若没有注册声音,请使用vcs_register_and_request.py脚本，注册声音和查询声音
voice_name = "" # 音色名称，可以使用注册音色或预置音色
audio_path = ""  # 待合成结果保存路径,例如"D://SIS/audio.wav"
text = ""  # 待合成文本

def synthesis_example(text, voice_name):
    # step1 初始化客户端
    config = SisConfig()
    config.set_connect_timeout(10)       # 设置连接超时，单位s
    config.set_read_timeout(60)          # 设置读取超时，单位s
    vcs_client = VcsClient(ak, sk, region, project_id, sis_config=config)
    synthesis_request = VcsSynthesisRequest(text, voice_name)
    # 设置采样率，8000 or 16000 or 24000, 默认16000
    synthesis_request.set_sample_rate("16000")
    # 设置音频格式, wav、pcm or mp3, 可参考api文档
    synthesis_request.set_audio_format("wav")
    # 设置音量，[0, 100]，默认50
    synthesis_request.set_volume(50)
    # 设置音高, [-500, 500], 默认0
    synthesis_request.set_pitch(0)
    # 设置音速, [-500, 500], 默认0
    synthesis_request.set_speed(0)
    synthesis_request.set_saved(True) # 是否对合成结果进行保存
    synthesis_request.set_saved_path(audio_path)  # 保存路径
    result = vcs_client.synthesis(synthesis_request)
    print(result)

if __name__ == '__main__':
    # 合成
    synthesis_example(text, voice_name)