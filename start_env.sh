#!/bin/bash
# TDlight Quick Start Script
# Usage: ./start_env.sh

# Get project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

# TDengine paths
TDENGINE_HOME="${TDENGINE_HOME:-$HOME/taos}"
TAOS_CONFIG_DIR="$PROJECT_ROOT/config/taos_cfg"
CONDA_ENV_NAME="tdlight"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo ""
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${CYAN}         TDlight Quick Start           ${NC}"
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

# Check TDengine installation
if [ ! -d "$TDENGINE_HOME" ]; then
    echo -e "${YELLOW}[!] TDengine not found. Run ./install.sh first${NC}"
    exit 1
fi

# Activate conda environment for Python scripts
if command -v conda &> /dev/null; then
    eval "$(conda shell.bash hook)" 2>/dev/null
    if conda env list | grep -q "^$CONDA_ENV_NAME "; then
        conda activate "$CONDA_ENV_NAME" 2>/dev/null
        echo -e "${GREEN}[✓] Conda environment: $CONDA_ENV_NAME${NC}"
    else
        echo -e "${YELLOW}[!] Conda env '$CONDA_ENV_NAME' not found, using system Python${NC}"
    fi
fi

# Set environment
export LD_LIBRARY_PATH="$PROJECT_ROOT/libs:$TDENGINE_HOME/driver:$LD_LIBRARY_PATH"
export PATH="$TDENGINE_HOME/bin:$PATH"
export TAOS_CONFIG_DIR

# Start TDengine if not running
if ! pgrep -x taosd > /dev/null; then
    echo -e "[i] Starting TDengine..."
    if [ -f "$HOME/.config/systemd/user/taosd.service" ]; then
        systemctl --user start taosd 2>/dev/null || true
    else
        nohup "$TDENGINE_HOME/bin/taosd" -c "$TAOS_CONFIG_DIR" > /dev/null 2>&1 &
    fi
    sleep 2
    
    if pgrep -x taosd > /dev/null; then
        echo -e "${GREEN}[✓] TDengine started${NC}"
    else
        echo -e "${YELLOW}[!] Failed to start TDengine${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}[✓] TDengine running${NC}"
fi

# Kill old web_api if exists
pkill -f "./web_api" 2>/dev/null || true
sleep 1

# Check if web_api binary exists
if [ ! -f "$PROJECT_ROOT/web/web_api" ]; then
    echo -e "${YELLOW}[!] web_api not found. Run 'make' first${NC}"
    exit 1
fi

# Start web_api in background with nohup
echo -e "[i] Starting web server..."
cd "$PROJECT_ROOT/web"
nohup ./web_api > "$PROJECT_ROOT/runtime/web_api.log" 2>&1 &
WEB_PID=$!
sleep 2

# Check if started
if kill -0 $WEB_PID 2>/dev/null; then
    echo -e "${GREEN}[✓] Web server started (PID: $WEB_PID)${NC}"
    echo ""
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "  Open: ${GREEN}http://localhost:5001${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo ""
    echo -e "  Logs: tail -f runtime/web_api.log"
    echo -e "  Stop: pkill -f web_api"
    echo ""
else
    echo -e "${YELLOW}[!] Failed to start web server${NC}"
    echo "Check logs: cat $PROJECT_ROOT/runtime/web_api.log"
    exit 1
fi
