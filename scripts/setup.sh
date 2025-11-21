#!/bin/bash
set -e

echo "═══════════════════════════════════════════════════════════════"
echo "  🔧 TAUWERK SYSTEM SETUP (DRM/KMS)"
echo "═══════════════════════════════════════════════════════════════"

echo ""
echo "📦 Installing system packages..."
sudo apt update && sudo apt upgrade -y

sudo apt install -y \
  git build-essential cmake \
  python3 python3-pip python3-venv \
  i2c-tools \
  libdrm-dev libgbm-dev \
  libegl1-mesa-dev libgles2-mesa-dev \
  libdrm-tests \
  libfreetype6-dev \
  libharfbuzz-dev \
  libpng-dev

echo ""
echo "🐍 Setting up Python environment..."
if [ ! -d "venv" ]; then
    python3 -m venv venv
fi

source venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt

echo ""
echo "👤 Setting user permissions..."
sudo usermod -aG video,input,gpio,i2c tauwerk

echo ""
echo "✅ SYSTEM SETUP COMPLETE"
echo ""
echo "⚠️  REBOOT REQUIRED for group permissions to take effect!"
echo "    Run: sudo reboot"