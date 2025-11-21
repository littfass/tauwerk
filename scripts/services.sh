#!/bin/bash
set -e  # Exit on error

# ═══════════════════════════════════════════════════════════════════════
# HELPER COMMANDS
# ═══════════════════════════════════════════════════════════════════════

if [ "$1" == "stop" ]; then
    echo "🛑 Stopping all Tauwerk services..."
    sudo systemctl stop tauwerk.service 2>/dev/null || true
    sudo systemctl stop tauwerk_touchpad.service 2>/dev/null || true
    sudo systemctl stop tauwerk_gpio.service 2>/dev/null || true
    echo "✅ All services stopped"
    exit 0
fi

if [ "$1" == "start" ]; then
    echo "▶️  Starting all Tauwerk services..."
    sudo systemctl start tauwerk_gpio.service
    sudo systemctl start tauwerk_touchpad.service
    sudo systemctl start tauwerk.service
    echo "✅ All services started"
    exit 0
fi

if [ "$1" == "restart" ]; then
    echo "🔄 Restarting all Tauwerk services..."
    sudo systemctl restart tauwerk_gpio.service
    sudo systemctl restart tauwerk_touchpad.service
    sudo systemctl restart tauwerk.service
    echo "✅ All services restarted"
    exit 0
fi

if [ "$1" == "status" ]; then
    echo "📊 Service Status:"
    echo ""
    echo "━━━ GPIO Driver ━━━"
    sudo systemctl status tauwerk_gpio.service --no-pager -l
    echo ""
    echo "━━━ Touchpad Driver (DRM) ━━━"
    sudo systemctl status tauwerk_touchpad.service --no-pager -l
    echo ""
    echo "━━━ Main Application ━━━"
    sudo systemctl status tauwerk.service --no-pager -l
    exit 0
fi

if [ "$1" == "logs" ]; then
    echo "📋 Live Logs (Ctrl+C to exit):"
    sudo journalctl -u tauwerk_gpio.service -u tauwerk_touchpad.service -u tauwerk.service -f
    exit 0
fi

if [ "$1" == "errors" ]; then
    echo "❌ Error Logs (last 50 lines):"
    sudo journalctl -u tauwerk_gpio.service -u tauwerk_touchpad.service -u tauwerk.service -p err -n 50
    exit 0
fi

# ═══════════════════════════════════════════════════════════════════════
# SERVICE DEPLOYMENT
# ═══════════════════════════════════════════════════════════════════════

echo "🚀 Deploying Tauwerk Services..."

# Touchpad Service (DRM VERSION!)
sudo tee /etc/systemd/system/tauwerk_touchpad.service > /dev/null <<EOF
[Unit]
Description=Tauwerk Touchpad Driver (DRM/KMS High Priority)
After=graphical.target
Before=tauwerk.service

[Service]
Type=simple
User=root
WorkingDirectory=/home/tauwerk
ExecStart=/home/tauwerk/bin/tauwerk_touchpad_driver
Restart=always
RestartSec=3
StandardOutput=journal
StandardError=journal

# DRM/KMS requires root or video group access
SupplementaryGroups=video input

# MAXIMUM PERFORMANCE
Nice=-20
CPUSchedulingPolicy=rr
CPUSchedulingPriority=99
OOMScoreAdjust=-1000

[Install]
WantedBy=tauwerk.service
EOF
echo "  ✅ tauwerk_touchpad.service created (DRM)"

# GPIO Service
sudo tee /etc/systemd/system/tauwerk_gpio.service > /dev/null <<EOF
[Unit]
Description=Tauwerk GPIO Driver (High Priority)
After=network.target
Before=tauwerk.service

[Service]
Type=simple
User=tauwerk
WorkingDirectory=/home/tauwerk
ExecStart=/home/tauwerk/bin/tauwerk_gpio_driver
Restart=always
RestartSec=3

# MAXIMUM PERFORMANCE
Nice=-20
CPUSchedulingPolicy=rr
CPUSchedulingPriority=99
OOMScoreAdjust=-1000

[Install]
WantedBy=multi-user.target
EOF
echo "  ✅ tauwerk_gpio.service created"

# Main Application Service
sudo tee /etc/systemd/system/tauwerk.service > /dev/null <<EOF
[Unit]
Description=Tauwerk Sequencer
After=tauwerk_gpio.service tauwerk_touchpad.service
Wants=tauwerk_gpio.service tauwerk_touchpad.service
Requires=tauwerk_touchpad.service

[Service]
Type=simple
User=tauwerk
WorkingDirectory=/home/tauwerk
ExecStart=/home/tauwerk/start.sh
Restart=always
RestartSec=5
Environment=PYTHONUNBUFFERED=1
Environment=PYTHONPATH=/home/tauwerk

StandardOutput=journal
StandardError=journal

Nice=0

[Install]
WantedBy=multi-user.target
EOF
echo "  ✅ tauwerk.service created"

echo ""
echo "🔄 Reloading systemd..."
sudo systemctl daemon-reload

echo "🔗 Enabling services..."
sudo systemctl enable tauwerk_gpio.service
sudo systemctl enable tauwerk_touchpad.service
sudo systemctl enable tauwerk.service

echo ""
echo "✅ Tauwerk services deployed successfully!"
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Usage:"
echo "  ./scripts/services.sh           # Deploy/update services"
echo "  ./scripts/services.sh start     # Start all services"
echo "  ./scripts/services.sh stop      # Stop all services"
echo "  ./scripts/services.sh restart   # Restart all services"
echo "  ./scripts/services.sh status    # Show detailed status"
echo "  ./scripts/services.sh logs      # Live log stream"
echo "  ./scripts/services.sh errors    # Show error logs only"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"