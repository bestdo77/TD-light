#!/bin/bash
# TDengine Data Import Run Script
# Usage: ./run.sh [lightcurve|catalog|sql] [args...]

set -e

# ==================== Path Configuration ====================
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Auto-detect apptainer (prefer environment, fallback to system)
APPTAINER_BIN="${APPTAINER_BIN:-$(which apptainer 2>/dev/null || echo apptainer)}"
CONTAINER="${PROJECT_ROOT}/tdengine-fs"
IMPORT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ==================== Apptainer Common Options ====================
APPTAINER_OPTS=(
    --bind "${PROJECT_ROOT}/runtime/taos_home/cfg:/etc/taos"
    --bind "${PROJECT_ROOT}/runtime:/app"
    --bind "/usr/lib/x86_64-linux-gnu/libgomp.so.1:/usr/lib/libgomp.so.1"
    --bind "${PROJECT_ROOT}/runtime/libs:/app/libs"
    --env "LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver"
)

# ==================== Command Dispatch ====================
case "$1" in
    lightcurve)
        # Light curve import
        # Usage: ./run.sh lightcurve [database_name]
        DB_NAME="${2:-lightcurve_db}"
        echo "Importing light curves to database: ${DB_NAME}"
        ${APPTAINER_BIN} exec "${APPTAINER_OPTS[@]}" ${CONTAINER} \
            /app/src/IO/import/lightcurve_importer \
            /app/data/gaiadr2/individual_lightcurves \
            ${DB_NAME}
        ;;
    
    crossmatch)
        # Cross-match new catalog with database
        # Usage: ./run.sh crossmatch <catalog.csv> [database_name] [match_radius_arcsec]
        CATALOG_FILE="$2"
        DB_NAME="${3:-gaiadr2_lc}"
        MATCH_RADIUS="${4:-1.0}"
        echo "Cross-matching catalog: ${CATALOG_FILE}"
        echo "Database: ${DB_NAME}, Match radius: ${MATCH_RADIUS} arcsec"
        ${APPTAINER_BIN} exec "${APPTAINER_OPTS[@]}" ${CONTAINER} \
            /app/src/IO/import/crossmatch \
            --catalog "${CATALOG_FILE}" \
            --db ${DB_NAME} \
            --radius ${MATCH_RADIUS}
        ;;
    
    catalog)
        # Catalog import with automatic cross-match
        # Usage: ./run.sh catalog [database_name] [crossmatch_radius]
        DB_NAME="${2:-gaiadr2_lc}"
        MATCH_RADIUS="${3:-1.0}"
        echo "Importing catalog to database: ${DB_NAME}"
        echo "Cross-match radius: ${MATCH_RADIUS} arcsec"
        ${APPTAINER_BIN} exec "${APPTAINER_OPTS[@]}" ${CONTAINER} \
            /app/src/IO/import/catalog_importer \
            --catalogs /app/data/catalogs_gaiadr2/catalogs \
            --coords /app/data/gaiadr2/source_coordinates.csv \
            --db ${DB_NAME} \
            --crossmatch 1 \
            --radius ${MATCH_RADIUS}
        ;;
    
    sql)
        # Execute SQL
        # Usage: ./run.sh sql "SQL statement"
        shift
        SQL="$*"
        echo "Executing SQL: ${SQL}"
        ${APPTAINER_BIN} exec \
            --bind "${PROJECT_ROOT}/runtime/taos_home/cfg:/etc/taos" \
            ${CONTAINER} \
            taos -s "${SQL}"
        ;;
    
    shell)
        # Enter container shell
        echo "Entering TDengine container..."
        ${APPTAINER_BIN} shell "${APPTAINER_OPTS[@]}" ${CONTAINER}
        ;;
    
    compile)
        # Compile import programs
        echo "Compiling import programs..."
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
        
        echo "Compilation complete"
        ;;
    
    *)
        echo "TDengine Data Import Tool"
        echo ""
        echo "Usage: $0 <command> [args]"
        echo ""
        echo "Commands:"
        echo "  crossmatch <file> [db] [radius]  Cross-match catalog with database"
        echo "  catalog [db] [radius]            Import catalog with auto cross-match"
        echo "  lightcurve [db]                  Import light curve data"
        echo "  sql \"SQL\"                        Execute SQL statement"
        echo "  shell                            Enter container shell"
        echo "  compile                          Compile import programs"
        echo ""
        echo "Examples:"
        echo "  $0 crossmatch new_catalog.csv gaiadr2_lc 1.0"
        echo "  $0 catalog gaiadr2_lc 1.0        # Import with cross-match"
        echo "  $0 lightcurve test_db"
        echo "  $0 sql \"SHOW DATABASES;\""
        echo "  $0 sql \"DROP DATABASE IF EXISTS test_db;\""
        ;;
esac
