import base64
import os
from huaweicloud_sis.bean.vcs_register_voice_request import RegisterVoiceRequest
from huaweicloud_sis.client.vcs_client import VcsClient
from huaweicloud_sis.bean.sis_config import SisConfig

# 鉴权参数
# 认证用的ak和sk硬编码到代码中或者明文存储都有很大的安全风险，建议在配置文件或者环境变量中密文存放，使用时解密，确保安全；
# 本示例以ak和sk保存在环境变量中来实现身份验证为例，运行本示例前请先在本地环境中设置环境变量HUAWEICLOUD_SIS_AK/HUAWEICLOUD_SIS_SK/HUAWEICLOUD_SIS_PROJECT_ID。
ak = os.getenv("HUAWEICLOUD_SIS_AK")             # 从环境变量获取ak 参考https://support.huaweicloud.com/sdkreference-sis/sis_05_0003.html
assert ak is not None, "Please add ak in your develop environment"
sk = os.getenv("HUAWEICLOUD_SIS_SK")             # 从环境变量获取sk 参考https://support.huaweicloud.com/sdkreference-sis/sis_05_0003.html
assert sk is not None, "Please add sk in your develop environment"
region = ""         # region，支持上海一region，cn-east-3
project_id = ""     # project id 同region一一对应，参考https://support.huaweicloud.com/api-sis/sis_03_0008.html
path = "" # 参考语音路径,请根据实际路径修改,要求采样率不小于16k，例如 r"D://test.wav"
voice_name = "" # 音色名称，可以使用注册音色或预置音色
service_endpoint = "" # 可选，自定义endpoint，例如"https://sis-ext.cn-east-3.myhuaweicloud.com"

def register_voice_example(path, voice_name):
    with open(path, 'rb') as f:
        data = f.read()
        data = str(base64.b64encode(data), 'utf-8')
    config = SisConfig()
    config.set_connect_timeout(10)  # 设置连接超时，单位s
    config.set_read_timeout(60)  # 设置读取超时，单位s
    vcs_client = VcsClient(ak, sk, region, project_id, sis_config=config, service_endpoint=service_endpoint)
    register_voice_request = RegisterVoiceRequest(data, voice_name)
    result = vcs_client.register_voice(register_voice_request)
    print(result)

def query_example():
    config = SisConfig()
    config.set_connect_timeout(10)  # 设置连接超时，单位s
    config.set_read_timeout(60)  # 设置读取超时，单位s
    vcs_client = VcsClient(ak, sk, region, project_id, sis_config=config, service_endpoint=service_endpoint)
    result = vcs_client.query_voice_name(10, 0) # 默认查询10条，支持分页查询
    print(result)

if __name__ == '__main__':
    # 两个接口可单独运行
    register_voice_example(path, voice_name) # 注册
    query_example() # 查询
