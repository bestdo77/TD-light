#!/bin/bash
# TDlight Stop Script
# Usage: ./stop_env.sh

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo ""
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${CYAN}         TDlight Stop Services         ${NC}"
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

# Stop web_api
if pgrep -f "web_api" > /dev/null; then
    echo -e "[i] Stopping web server..."
    pkill -f "web_api"
    sleep 1
    echo -e "${GREEN}[✓] Web server stopped${NC}"
else
    echo -e "${YELLOW}[i] Web server not running${NC}"
fi

# Stop TDengine
if pgrep -x taosd > /dev/null; then
    echo -e "[i] Stopping TDengine..."
    if [ -f "$HOME/.config/systemd/user/taosd.service" ]; then
        systemctl --user stop taosd 2>/dev/null
    else
        pkill -x taosd
    fi
    sleep 2
    
    if ! pgrep -x taosd > /dev/null; then
        echo -e "${GREEN}[✓] TDengine stopped${NC}"
    else
        echo -e "${YELLOW}[!] TDengine still running, force kill...${NC}"
        pkill -9 -x taosd
    fi
else
    echo -e "${YELLOW}[i] TDengine not running${NC}"
fi

echo ""
echo -e "${GREEN}All services stopped${NC}"
echo ""

