#!/bin/bash
# 进入 TDlight 运行环境

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"

# 检查 Conda 环境
if [ -z "$CONDA_PREFIX" ]; then
    echo "警告: 未检测到 Conda 环境，Python 分类功能可能不可用"
    echo "建议: conda activate tdlight"
fi

# 查找 apptainer
if command -v apptainer &> /dev/null; then
    APPTAINER="apptainer"
elif command -v singularity &> /dev/null; then
    APPTAINER="singularity"
else
    echo "错误: 未找到 apptainer"
    echo "安装: conda install -c conda-forge apptainer"
    exit 1
fi

# 查找容器
CONTAINER=""
if [ -d "$PROJECT_ROOT/tdengine-fs" ]; then
    CONTAINER="$PROJECT_ROOT/tdengine-fs"
elif [ -n "$TDLIGHT_CONTAINER" ]; then
    CONTAINER="$TDLIGHT_CONTAINER"
else
    echo "错误: 未找到容器目录 tdengine-fs/"
    exit 1
fi

echo "项目: $PROJECT_ROOT"
echo "容器: $CONTAINER"
[ -n "$CONDA_PREFIX" ] && echo "Conda: $CONDA_PREFIX"
echo ""

# 挂载选项
BIND_OPTS="--bind $PROJECT_ROOT:/app"
BIND_OPTS="$BIND_OPTS --bind $PROJECT_ROOT/config/taos_cfg:/etc/taos"

# 挂载 Conda 环境
if [ -n "$CONDA_PREFIX" ]; then
    BIND_OPTS="$BIND_OPTS --bind $CONDA_PREFIX:$CONDA_PREFIX"
fi

$APPTAINER shell \
    $BIND_OPTS \
    --env "LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver:\$LD_LIBRARY_PATH" \
    --env "PATH=$CONDA_PREFIX/bin:\$PATH" \
    "$CONTAINER"
