#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
: "${IDF_PATH:?请先激活 ESP-IDF 6.0.1 或更新版本的环境}"
export XIAOZHI_BUILD_DIR="${XIAOZHI_BUILD_DIR:-/tmp/walle-single-build}"
python scripts/build.py walle-single --name walle-single-16m --language zh-CN
