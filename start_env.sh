#!/bin/bash
# TDlight environment setup (native TDengine, no container)

# Get project root - works with both 'source' and direct execution
if [[ -n "${BASH_SOURCE[0]}" && "${BASH_SOURCE[0]}" != "${0}" ]]; then
    # Sourced
    PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
else
    # Direct execution
    PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
fi

# TDengine paths (user-mode installation)
TDENGINE_HOME="${TDENGINE_HOME:-$HOME/taos}"

# Check TDengine installation
if [ ! -d "$TDENGINE_HOME" ]; then
    echo "Error: TDengine not found at $TDENGINE_HOME"
    echo "Install TDengine 3.4.0+ in user mode (no sudo required):"
    echo "  wget https://downloads.tdengine.com/tdengine-tsdb-oss/3.4.0.0/tdengine-tsdb-oss-3.4.0.0-linux-x64.tar.gz"
    echo "  tar -xzf tdengine-tsdb-oss-3.4.0.0-linux-x64.tar.gz"
    echo "  cd tdengine-tsdb-oss-3.4.0.0 && ./install.sh -e no"
    return 1 2>/dev/null || exit 1
fi

# Check Conda environment
if [ -z "$CONDA_PREFIX" ]; then
    echo "Warning: Conda environment not detected"
    echo "Suggestion: conda activate tdlight"
fi

# Export environment variables
export LD_LIBRARY_PATH="$PROJECT_ROOT/libs:$TDENGINE_HOME/driver:$LD_LIBRARY_PATH"
export PATH="$TDENGINE_HOME/bin:$PATH"
export TAOS_CONFIG_DIR="$PROJECT_ROOT/config/taos_cfg"

echo "========================================"
echo "TDlight Environment"
echo "========================================"
echo "Project:    $PROJECT_ROOT"
echo "TDengine:   $TDENGINE_HOME"
echo "Config:     $TAOS_CONFIG_DIR"
[ -n "$CONDA_PREFIX" ] && echo "Conda:      $CONDA_PREFIX"
echo "========================================"
echo ""
echo "TDengine service commands:"
echo "  Start:    systemctl --user start taosd"
echo "  Stop:     systemctl --user stop taosd"
echo "  Status:   systemctl --user status taosd"
echo ""
echo "Quick start:"
echo "  cd web && ./web_api"
echo ""
