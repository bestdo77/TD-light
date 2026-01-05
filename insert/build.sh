#!/bin/bash
# Compile data import programs

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Header and library paths
INCLUDE_DIR="$PROJECT_ROOT/include"
LIBS_DIR="$PROJECT_ROOT/libs"

# Check paths
if [ ! -d "$INCLUDE_DIR" ]; then
    echo "Error: Include directory not found $INCLUDE_DIR"
    exit 1
fi

if [ ! -d "$LIBS_DIR" ]; then
    echo "Error: Library directory not found $LIBS_DIR"
    exit 1
fi

echo "Compiling catalog_importer..."
g++ -std=c++17 -O3 catalog_importer.cpp -o catalog_importer \
    -I"$INCLUDE_DIR" \
    -L"$LIBS_DIR" \
    -ltaos -lhealpix_cxx -lsharp -lcfitsio -lpthread \
    -Wl,-rpath,"$LIBS_DIR"

echo "Compiling lightcurve_importer..."
g++ -std=c++17 -O3 lightcurve_importer.cpp -o lightcurve_importer \
    -I"$INCLUDE_DIR" \
    -L"$LIBS_DIR" \
    -ltaos -lhealpix_cxx -lsharp -lcfitsio -lpthread \
    -Wl,-rpath,"$LIBS_DIR"

echo "Compiling check_candidates..."
g++ -std=c++17 -O3 check_candidates.cpp -o check_candidates \
    -I"$INCLUDE_DIR" \
    -L"$LIBS_DIR" \
    -ltaos -lpthread \
    -Wl,-rpath,"$LIBS_DIR"

echo "Compilation complete"
chmod +x catalog_importer lightcurve_importer check_candidates
