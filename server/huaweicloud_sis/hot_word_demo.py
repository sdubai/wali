# -*- coding: utf-8 -*-

from huaweicloud_sis.client.hot_word_client import HotWordClient
from huaweicloud_sis.bean.hot_word_request import HotWordRequest
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
# 热词参数
name = ''           # 创建热词时，需要保证name在此之前没有被创建使用过。如 test1
word_list = list()  # 用于存放热词表。每个热词表最多可以存放1024个热词。如["计算机", "网络"]
vocabulary_id = ''  # 用于更新指定热词表id信息，查询指定热词表id信息，删除指定热词表id信息。使用前要保证热词表id存在，否则就不要使用。


def hot_word_example():
    """
        1. 热词使用包含创建、更新、查询、删除等，一个用户可以创建多个热词表，一个热词表可以包含多个热词。一个vocabulary_id对应一个热词表。
        2. 目前支持一个用户最多创建10个热词表，一个热词表最多包含1024个热词。
        3. 热词可在一句话识别、录音文件识别、实时语音识别使用。例如将地名和人名作为热词，则语音可以准确识别出人名和地名。
    :return: 无
    """
    # 初始化客户端
    config = SisConfig()
    config.set_connect_timeout(10)       # 设置连接超时
    config.set_read_timeout(10)          # 设置读取超时
    # 设置代理，使用代理前一定要确保代理可用。 代理格式可为[host, port] 或 [host, port, username, password]
    # config.set_proxy(proxy)
    hot_word_client = HotWordClient(ak, sk, region, project_id, sis_config=config)

    # option 1 创建热词表
    word_list.append('测试')
    create_request = HotWordRequest(name, word_list)
    # 可选，热词语言，目前仅支持中文 chinese_mandarin。
    create_request.set_language('chinese_mandarin')
    # 可选，热词表描述信息
    create_request.set_description('test')
    create_result = hot_word_client.create(create_request)
    # 返回结果为json格式
    print('成功创建热词表')
    print(json.dumps(create_result, indent=2, ensure_ascii=False))

    # option 2 根据热词表id 更新热词表。新的热词表会替换旧的热词表。使用前需确保热词表id已存在。
    word_list.append('计算机')
    update_request = HotWordRequest('test2', word_list)
    update_result = hot_word_client.update(update_request, vocabulary_id)
    # 返回结果为json格式
    print('成功更新热词表', vocabulary_id)
    print(json.dumps(update_result, indent=2, ensure_ascii=False))

    # option 3 查看热词表列表
    query_list_result = hot_word_client.query_list()
    print(json.dumps(query_list_result, indent=2, ensure_ascii=False))

    # option 4 根据热词表id查询具体热词表信息，使用前需确保热词表id已存在。
    query_result = hot_word_client.query_by_vocabulary_id(vocabulary_id)
    print(json.dumps(query_result, indent=2, ensure_ascii=False))

    # option 5 根据热词表id删除热词表，使用前需确保热词表id已存在。
    delete_result = hot_word_client.delete(vocabulary_id)
    if delete_result is None:
        print('成功删除热词表', vocabulary_id)
    else:
        print(json.dumps(delete_result, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    try:
        hot_word_example()
    except ClientException as e:
        print(e)
    except ServerException as e:
        print(e)
