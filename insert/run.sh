#!/bin/bash
# TDengine 数据导入运行脚本
# 用法: ./run.sh [lightcurve|catalog|sql] [参数...]

set -e

# ==================== 路径配置 ====================
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# 自动查找 apptainer（优先使用环境中的，否则使用系统的）
APPTAINER_BIN="${APPTAINER_BIN:-$(which apptainer 2>/dev/null || echo apptainer)}"
CONTAINER="${PROJECT_ROOT}/tdengine-fs"
IMPORT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ==================== Apptainer 通用参数 ====================
APPTAINER_OPTS=(
    --bind "${PROJECT_ROOT}/runtime/taos_home/cfg:/etc/taos"
    --bind "${PROJECT_ROOT}/runtime:/app"
    --bind "/usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1"
    --bind "${PROJECT_ROOT}/runtime/libs:/app/libs"
    --env "LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver"
)

# ==================== 命令分发 ====================
case "$1" in
    lightcurve)
        # 光变曲线导入
        # 用法: ./run.sh lightcurve [数据库名]
        DB_NAME="${2:-lightcurve_db}"
        echo "🚀 导入光变曲线到数据库: ${DB_NAME}"
        ${APPTAINER_BIN} exec "${APPTAINER_OPTS[@]}" ${CONTAINER} \
            /app/src/IO/import/lightcurve_importer \
            /app/data/gaiadr2/individual_lightcurves \
            ${DB_NAME}
        ;;
    
    catalog)
        # 星表导入
        # 用法: ./run.sh catalog [数据库名]
        DB_NAME="${2:-catalog_db}"
        echo "🚀 导入星表到数据库: ${DB_NAME}"
        ${APPTAINER_BIN} exec "${APPTAINER_OPTS[@]}" ${CONTAINER} \
            /app/src/IO/import/catalog_importer \
            --catalogs /app/data/catalogs_gaiadr2/catalogs \
            --coords /app/data/gaiadr2/source_coordinates.csv \
            --db ${DB_NAME}
        ;;
    
    sql)
        # 执行 SQL
        # 用法: ./run.sh sql "SQL语句"
        shift
        SQL="$*"
        echo "📊 执行 SQL: ${SQL}"
        ${APPTAINER_BIN} exec \
            --bind "${PROJECT_ROOT}/runtime/taos_home/cfg:/etc/taos" \
            ${CONTAINER} \
            taos -s "${SQL}"
        ;;
    
    shell)
        # 进入容器 shell
        echo "🐚 进入 TDengine 容器..."
        ${APPTAINER_BIN} shell "${APPTAINER_OPTS[@]}" ${CONTAINER}
        ;;
    
    compile)
        # 编译导入程序
        echo "🔧 编译导入程序..."
        cd ${IMPORT_DIR}
        TAOS_DIR="${PROJECT_ROOT}/tdengine-fs/usr/local/taos"
        LIBS_DIR="${PROJECT_ROOT}/runtime/libs"
        DEPS_DIR="${PROJECT_ROOT}/runtime/deps/local"
        
        g++ -std=c++17 -O3 -march=native lightcurve_importer.cpp -o lightcurve_importer \
            -I${TAOS_DIR}/include -I${DEPS_DIR}/include \
            -L${TAOS_DIR}/driver -L${LIBS_DIR} \
            -ltaos -lhealpix_cxx -lpthread \
            -Wl,-rpath,${TAOS_DIR}/driver -Wl,-rpath,${LIBS_DIR}
        
        g++ -std=c++17 -O3 -march=native catalog_importer.cpp -o catalog_importer \
            -I${TAOS_DIR}/include -I${DEPS_DIR}/include \
            -L${TAOS_DIR}/driver -L${LIBS_DIR} \
            -ltaos -lhealpix_cxx -lpthread \
            -Wl,-rpath,${TAOS_DIR}/driver -Wl,-rpath,${LIBS_DIR}
        
        echo "✅ 编译完成"
        ;;
    
    *)
        echo "TDengine 数据导入工具"
        echo ""
        echo "用法: $0 <命令> [参数]"
        echo ""
        echo "命令:"
        echo "  lightcurve [db]  导入光变曲线数据"
        echo "  catalog [db]     导入星表数据"
        echo "  sql \"SQL\"        执行 SQL 语句"
        echo "  shell            进入容器 shell"
        echo "  compile          编译导入程序"
        echo ""
        echo "示例:"
        echo "  $0 lightcurve test_db"
        echo "  $0 catalog catalog_test"
        echo "  $0 sql \"SHOW DATABASES;\""
        echo "  $0 sql \"DROP DATABASE IF EXISTS test_db;\""
        ;;
esac

