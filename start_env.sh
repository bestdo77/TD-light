#!/bin/bash
# TDlight Quick Start Script
# Usage: ./start_env.sh

# Get project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

# TDengine paths
TDENGINE_HOME="${TDENGINE_HOME:-$HOME/taos}"
TAOS_CONFIG_DIR="$PROJECT_ROOT/config/taos_cfg"

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

# Set environment
export LD_LIBRARY_PATH="$PROJECT_ROOT/libs:$TDENGINE_HOME/driver:$LD_LIBRARY_PATH"
export PATH="$TDENGINE_HOME/bin:$PATH"
export TAOS_CONFIG_DIR

# Ensure runtime dirs exist (taosd needs writable log/data/temp)
mkdir -p "$PROJECT_ROOT/runtime/taos_home/log" "$PROJECT_ROOT/runtime/taos_home/data" "$PROJECT_ROOT/runtime/taos_home/temp" "$TAOS_CONFIG_DIR" 2>/dev/null || true

# If taos.cfg is missing (e.g., fresh clone), create a minimal one
if [ ! -f "$TAOS_CONFIG_DIR/taos.cfg" ]; then
    cat > "$TAOS_CONFIG_DIR/taos.cfg" << EOF
# TDlight TDengine Configuration (auto-generated)
fqdn               localhost
serverPort         6030
logDir             $PROJECT_ROOT/runtime/taos_home/log
dataDir            $PROJECT_ROOT/runtime/taos_home/data
tempDir            $PROJECT_ROOT/runtime/taos_home/temp
firstEp            localhost:6030
maxShellConns      500
supportVnodes      256
EOF
fi

# Start TDengine if not running
if ! pgrep -x taosd > /dev/null; then
    echo -e "[i] Starting TDengine..."
    # Always start with our config (avoids systemd using a different taos.cfg)
    nohup "$TDENGINE_HOME/bin/taosd" -c "$TAOS_CONFIG_DIR" > "$PROJECT_ROOT/runtime/taosd.log" 2>&1 &
    sleep 2
    
    if pgrep -x taosd > /dev/null; then
        echo -e "${GREEN}[✓] TDengine started${NC}"
    else
        echo -e "${YELLOW}[!] Failed to start TDengine${NC}"
        echo -e "${YELLOW}    Check: cat $PROJECT_ROOT/runtime/taosd.log${NC}"
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
