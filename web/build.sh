#!/bin/bash
set -e

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$SCRIPT_DIR"

# 检查是否在容器内
if [ -n "$APPTAINER_NAME" ] || [ -n "$SINGULARITY_NAME" ]; then
    echo "⚠️  检测到在容器环境内。"
    if ! command -v g++ &> /dev/null; then
        echo "❌ 错误: 容器内未找到 g++ 编译器。"
        echo "💡 建议: 请先在宿主机(容器外)运行此编译脚本，然后再进容器运行程序。"
        exit 1
    fi
fi

echo "🔧 初始化编译环境..."

# 定义依赖路径 (优先使用项目内路径)
LOCAL_INCLUDE="${PROJECT_ROOT}/include"
LOCAL_LIB="${PROJECT_ROOT}/libs"

# 开发环境回退路径 (适配你的本地环境)
DEV_BACKUP_ROOT="${PROJECT_ROOT}/../TDengine-test (backup)"
DEV_RUNTIME_PATH="${DEV_BACKUP_ROOT}/runtime"

# 确定头文件路径
if [ -d "$LOCAL_INCLUDE" ] && [ "$(ls -A $LOCAL_INCLUDE)" ]; then
    INCLUDE_PATH="$LOCAL_INCLUDE"
    echo "📚 使用项目内头文件: $INCLUDE_PATH"
elif [ -d "${DEV_RUNTIME_PATH}/deps/local/include" ]; then
    INCLUDE_PATH="${DEV_RUNTIME_PATH}/deps/local/include"
    echo "⚠️  未找到项目内 include，使用开发环境路径: $INCLUDE_PATH"
else
    echo "❌ 错误: 找不到头文件目录 (include)"
    exit 1
fi

# 确定库文件路径
if [ -d "$LOCAL_LIB" ] && [ "$(ls -A $LOCAL_LIB)" ]; then
    LIB_PATH="$LOCAL_LIB"
    echo "📚 使用项目内库文件: $LIB_PATH"
elif [ -d "${DEV_RUNTIME_PATH}/libs" ]; then
    LIB_PATH="${DEV_RUNTIME_PATH}/libs"
    echo "⚠️  未找到项目内 libs，使用开发环境路径: $LIB_PATH"
else
    echo "❌ 错误: 找不到库文件目录 (libs)"
    exit 1
fi

echo "🔨 正在编译 web_api..."

# 编译命令
# -Wl,-rpath,'$ORIGIN/../libs' : 关键参数，确保运行时能找到库
g++ -o web_api web_api.cpp \
    -I"$INCLUDE_PATH" \
    -L"$LIB_PATH" \
    -ltaos -lhealpix_cxx -lsharp -lcfitsio -lpthread -std=c++11 \
    -Wl,-rpath,'$ORIGIN/../libs'

echo "✅ 编译成功: web_api"
