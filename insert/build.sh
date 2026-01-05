#!/bin/bash
# 编译数据导入程序

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# 头文件和库路径
INCLUDE_DIR="$PROJECT_ROOT/include"
LIBS_DIR="$PROJECT_ROOT/libs"

# 检查路径
if [ ! -d "$INCLUDE_DIR" ]; then
    echo "错误: 找不到头文件目录 $INCLUDE_DIR"
    exit 1
fi

if [ ! -d "$LIBS_DIR" ]; then
    echo "错误: 找不到库目录 $LIBS_DIR"
    exit 1
fi

echo "编译 catalog_importer..."
g++ -std=c++17 -O3 catalog_importer.cpp -o catalog_importer \
    -I"$INCLUDE_DIR" \
    -L"$LIBS_DIR" \
    -ltaos -lhealpix_cxx -lsharp -lcfitsio -lpthread \
    -Wl,-rpath,"$LIBS_DIR"

echo "编译 lightcurve_importer..."
g++ -std=c++17 -O3 lightcurve_importer.cpp -o lightcurve_importer \
    -I"$INCLUDE_DIR" \
    -L"$LIBS_DIR" \
    -ltaos -lhealpix_cxx -lsharp -lcfitsio -lpthread \
    -Wl,-rpath,"$LIBS_DIR"

echo "编译 check_candidates..."
g++ -std=c++17 -O3 check_candidates.cpp -o check_candidates \
    -I"$INCLUDE_DIR" \
    -L"$LIBS_DIR" \
    -ltaos -lpthread \
    -Wl,-rpath,"$LIBS_DIR"

echo "编译完成"
chmod +x catalog_importer lightcurve_importer check_candidates

