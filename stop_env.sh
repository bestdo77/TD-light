#!/bin/bash
# TDlight Stop Script
# Default: stop web only
# Usage:
#   ./stop_env.sh            # stop web only
#   ./stop_env.sh --all      # stop web + TDengine

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

# Parse args
STOP_TDENGINE=false
if [[ "${1:-}" == "--all" ]] || [[ "${1:-}" == "--tdengine" ]]; then
    STOP_TDENGINE=true
fi

# Stop web_api
if pgrep -f "web_api" > /dev/null; then
    echo -e "[i] Stopping web server..."
    pkill -f "web_api"
    sleep 1
    echo -e "${GREEN}[✓] Web server stopped${NC}"
else
    echo -e "${YELLOW}[i] Web server not running${NC}"
fi

if [[ "$STOP_TDENGINE" == "true" ]]; then
    # Stop TDengine (optional)
    if pgrep -x taosd > /dev/null; then
        echo -e "[i] Stopping TDengine..."
        if [ -f "$HOME/.config/systemd/user/taosd.service" ]; then
            systemctl --user stop taosd 2>/dev/null || true
        else
            pkill -x taosd || true
        fi
        sleep 2
        
        if ! pgrep -x taosd > /dev/null; then
            echo -e "${GREEN}[✓] TDengine stopped${NC}"
        else
            echo -e "${YELLOW}[!] TDengine still running, force kill...${NC}"
            pkill -9 -x taosd || true
        fi
    else
        echo -e "${YELLOW}[i] TDengine not running${NC}"
    fi
else
    echo -e "${CYAN}[i] TDengine left running (use --all to stop it)${NC}"
fi

echo ""
echo -e "${GREEN}Done${NC}"
echo ""


