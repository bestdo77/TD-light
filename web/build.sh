#!/bin/bash
set -e

# Get absolute path of script directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$SCRIPT_DIR"

# Check if inside container
if [ -n "$APPTAINER_NAME" ] || [ -n "$SINGULARITY_NAME" ]; then
    echo "⚠️  Detected container environment."
    if ! command -v g++ &> /dev/null; then
        echo "❌ Error: g++ compiler not found in container."
        echo "💡 Suggestion: Please run this build script on the host machine (outside container) first, then run the program inside the container."
        exit 1
    fi
fi

echo "🔧 Initializing build environment..."

# Define dependency paths (prefer project-internal paths)
LOCAL_INCLUDE="${PROJECT_ROOT}/include"
LOCAL_LIB="${PROJECT_ROOT}/libs"

# Development environment fallback paths (adapt to your local environment)
DEV_BACKUP_ROOT="${PROJECT_ROOT}/../TDengine-test (backup)"
DEV_RUNTIME_PATH="${DEV_BACKUP_ROOT}/runtime"

# Determine include path
if [ -d "$LOCAL_INCLUDE" ] && [ "$(ls -A $LOCAL_INCLUDE)" ]; then
    INCLUDE_PATH="$LOCAL_INCLUDE"
    echo "📚 Using project include files: $INCLUDE_PATH"
elif [ -d "${DEV_RUNTIME_PATH}/deps/local/include" ]; then
    INCLUDE_PATH="${DEV_RUNTIME_PATH}/deps/local/include"
    echo "⚠️  Project include not found, using development path: $INCLUDE_PATH"
else
    echo "❌ Error: Include directory not found"
    exit 1
fi

# Determine library path
if [ -d "$LOCAL_LIB" ] && [ "$(ls -A $LOCAL_LIB)" ]; then
    LIB_PATH="$LOCAL_LIB"
    echo "📚 Using project library files: $LIB_PATH"
elif [ -d "${DEV_RUNTIME_PATH}/libs" ]; then
    LIB_PATH="${DEV_RUNTIME_PATH}/libs"
    echo "⚠️  Project libs not found, using development path: $LIB_PATH"
else
    echo "❌ Error: Library directory not found"
    exit 1
fi

echo "🔨 Compiling web_api..."

# Compile command
# -Wl,-rpath,'$ORIGIN/../libs' : Key parameter to ensure runtime library discovery
g++ -o web_api web_api.cpp \
    -I"$INCLUDE_PATH" \
    -L"$LIB_PATH" \
    -ltaos -lhealpix_cxx -lsharp -lcfitsio -lpthread -std=c++11 \
    -Wl,-rpath,'$ORIGIN/../libs'

echo "✅ Build successful: web_api"
