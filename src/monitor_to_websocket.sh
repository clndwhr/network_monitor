#!/bin/sh
#
# Monitor bridge_traffic_monitor output and stream to WebSocket
# Usage: ./monitor_to_websocket.sh [websocket_url]
#

# Default WebSocket URL (change as needed)
WS_URL="${1:-ws://ws://localhost:8838}"

# File to monitor
MONITOR_FILE="/tmp/quecmanger/bridge_traffic_monitor"

# Check if websocat is installed
if ! command -v websocat >/dev/null 2>&1; then
    echo "Error: websocat is not installed"
    echo "Install with: opkg install websocat"
    exit 1
fi

# Check if monitor file exists
if [ ! -f "$MONITOR_FILE" ]; then
    echo "Warning: Monitor file does not exist yet: $MONITOR_FILE"
    echo "Waiting for file to be created..."
fi

echo "Monitoring: $MONITOR_FILE"
echo "Streaming to: $WS_URL"
echo "Press Ctrl+C to stop"
echo ""

# Function to read and send file content
send_update() {
    if [ -f "$MONITOR_FILE" ]; then
        cat "$MONITOR_FILE"
    fi
}

# Stream updates to WebSocket every second
while true; do
    send_update | websocat --one-message "$WS_URL"
    sleep 1
done
