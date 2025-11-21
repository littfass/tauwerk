#!/bin/bash
echo "🛑 STOPPING TAUWERK..."

# Python App stoppen (wird durch systemd neu gestartet)
pkill -f "python3 app/main.py" 2>/dev/null && echo "✅ Python App stopped"

# GPIO Driver über Service stoppen
if systemctl is-active --quiet tauwerk_gpio.service; then
    echo "⏹️  Stopping GPIO Driver Service..."
    sudo systemctl stop tauwerk_gpio.service
fi

# Kurz warten damit Services sauber stoppen
sleep 1

# Sicherheits-Check: Falls noch Prozesse laufen (sollte nicht passieren)
if pgrep -f "tauwerk_gpio_driver" > /dev/null; then
    echo "⚠️  Cleaning up remaining GPIO processes..."
    pkill -f "tauwerk_gpio_driver"
fi

echo "✅ TAUWERK STOPPED"
# Services bleiben deaktiviert bis zum nächsten systemctl start