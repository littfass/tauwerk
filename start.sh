#!/bin/bash
echo "🚀 STARTING TAUWERK..."

export PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="/home/tauwerk/app:$PYTHONPATH"

# GPIO Driver über Service starten (falls nicht läuft)
if ! systemctl is-active --quiet tauwerk_gpio.service; then
    echo "▶ Starting GPIO Driver Service..."
    sudo systemctl start tauwerk_gpio.service
fi

# Virtual Environment prüfen und aktivieren
if [ -d "venv" ] && [ -f "venv/bin/activate" ]; then
    echo "✅ Virtual Environment found - activating..."
    source venv/bin/activate
else
    echo "⚠️  No virtual environment found - using system Python"
    # Optional: venv erstellen
    # python3 -m venv venv
    # source venv/bin/activate
    # pip install -r requirements.txt
fi

# Hauptanwendung starten
echo "▶ Starting Python Main Application..."
python3 -m app.main