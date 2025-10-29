#!/bin/sh

echo "═══════════════════════════════════════════════════════════════"
echo "  BRIDGE TRAFFIC MONITOR - Setup and Launch"
echo "═══════════════════════════════════════════════════════════════"
echo ""

echo "Compiling bridge traffic monitor..."
if gcc -o bridge_traffic_monitor bridge_traffic_monitor.c; then
    echo "✅ Compilation successful!"
    echo ""
    
    echo "Starting monitor in background..."
    ./bridge_traffic_monitor &
    MONITOR_PID=$!
    echo "Monitor PID: $MONITOR_PID"
    echo "$MONITOR_PID" > /tmp/bridge_monitor.pid
    
    echo ""
    echo "Waiting for initial data collection..."
    sleep 2
    
    echo ""
    echo "═══════════════════════════════════════════════════════════════"
    echo "  MONITOR STARTED"
    echo "═══════════════════════════════════════════════════════════════"
    echo ""
    echo "📊 View real-time data:"
    echo "   watch -n 0.5 cat /tmp/bridge_traffic_monitor"
    echo ""
    echo "📊 Or continuously tail:"
    echo "   tail -f /tmp/bridge_traffic_monitor"
    echo ""
    echo "📊 Single view:"
    echo "   cat /tmp/bridge_traffic_monitor"
    echo ""
    echo "🛑 Stop monitor:"
    echo "   kill \$(cat /tmp/bridge_monitor.pid)"
    echo ""
    echo "═══════════════════════════════════════════════════════════════"
    echo ""
    echo "Current snapshot:"
    echo ""
    cat /tmp/bridge_traffic_monitor
    
else
    echo "❌ Compilation failed!"
    exit 1
fi
