#!/bin/bash
# Enter TDlight runtime environment

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"

# Check Conda environment
if [ -z "$CONDA_PREFIX" ]; then
    echo "Warning: Conda environment not detected, Python classification may not be available"
    echo "Suggestion: conda activate tdlight"
fi

# Find apptainer
if command -v apptainer &> /dev/null; then
    APPTAINER="apptainer"
elif command -v singularity &> /dev/null; then
    APPTAINER="singularity"
else
    echo "Error: apptainer not found"
    echo "Install: conda install -c conda-forge apptainer"
    exit 1
fi

# Find container
CONTAINER=""
if [ -d "$PROJECT_ROOT/tdengine-fs" ]; then
    CONTAINER="$PROJECT_ROOT/tdengine-fs"
elif [ -n "$TDLIGHT_CONTAINER" ]; then
    CONTAINER="$TDLIGHT_CONTAINER"
else
    echo "Error: Container directory tdengine-fs/ not found"
    exit 1
fi

echo "Project: $PROJECT_ROOT"
echo "Container: $CONTAINER"
[ -n "$CONDA_PREFIX" ] && echo "Conda: $CONDA_PREFIX"
echo ""

# Mount options
BIND_OPTS="--bind $PROJECT_ROOT:/app"
BIND_OPTS="$BIND_OPTS --bind $PROJECT_ROOT/config/taos_cfg:/etc/taos"

# Mount Conda environment
if [ -n "$CONDA_PREFIX" ]; then
    BIND_OPTS="$BIND_OPTS --bind $CONDA_PREFIX:$CONDA_PREFIX"
fi

$APPTAINER shell \
    $BIND_OPTS \
    --env "LD_LIBRARY_PATH=/app/libs:/usr/local/taos/driver:\$LD_LIBRARY_PATH" \
    --env "PATH=$CONDA_PREFIX/bin:\$PATH" \
    "$CONTAINER"
