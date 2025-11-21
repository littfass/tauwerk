#!/bin/bash

echo "════════════════════════════════════════"
echo "  🔍 DSI Display Diagnostic Tool"
echo "════════════════════════════════════════"
echo ""

echo "📋 1. Boot Configuration:"
echo "───────────────────────────────────────"
if [ -f /boot/firmware/config.txt ]; then
    echo "File: /boot/firmware/config.txt"
    grep -E "^[^#]*(dtoverlay|dtparam).*" /boot/firmware/config.txt | grep -i -E "dsi|display|panel|vc4|kms"
elif [ -f /boot/config.txt ]; then
    echo "File: /boot/config.txt"
    grep -E "^[^#]*(dtoverlay|dtparam).*" /boot/config.txt | grep -i -E "dsi|display|panel|vc4|kms"
else
    echo "⚠️  Boot config not found"
fi
echo ""

echo "🔌 2. Device Tree Parameters:"
echo "───────────────────────────────────────"
if command -v dtparam &> /dev/null; then
    dtparam -l 2>/dev/null | grep -i -E "display|dsi|panel" || echo "No display-related overlays found"
else
    echo "⚠️  dtparam command not available"
fi
echo ""

echo "💾 3. DRM Devices:"
echo "───────────────────────────────────────"
if ls /dev/dri/card* 2>/dev/null; then
    for card in /dev/dri/card*; do
        echo "  📦 $(basename $card):"
        if [ -r "$card" ]; then
            # Try to get driver info from sysfs
            card_name=$(basename $card)
            if [ -d "/sys/class/drm/$card_name/device" ]; then
                driver=$(basename $(readlink -f /sys/class/drm/$card_name/device/driver 2>/dev/null) 2>/dev/null)
                [ -n "$driver" ] && echo "     Driver: $driver"
            fi
        else
            echo "     ⚠️  No read permission"
        fi
    done
else
    echo "  ❌ No DRM devices found!"
fi
echo ""

echo "🧩 4. Loaded Kernel Modules:"
echo "───────────────────────────────────────"
lsmod | grep -E "vc4|v3d|drm|panel|dsi|backlight" | awk '{printf "  ✓ %-20s %s\n", $1, $2}' || echo "  ⚠️  No relevant modules loaded"
echo ""

echo "🖥️  5. Framebuffer Devices:"
echo "───────────────────────────────────────"
if ls /dev/fb* 2>/dev/null; then
    for fb in /dev/fb*; do
        echo "  📺 $(basename $fb)"
        if [ -f /sys/class/graphics/$(basename $fb)/name ]; then
            echo "     Name: $(cat /sys/class/graphics/$(basename $fb)/name)"
        fi
    done
else
    echo "  ❌ No framebuffer devices found"
fi
echo ""

echo "⚙️  6. DRM Connector Details (via sysfs):"
echo "───────────────────────────────────────"
if ls /sys/class/drm/card*/card*/status 2>/dev/null; then
    for status_file in /sys/class/drm/card*/card*/status; do
        connector=$(basename $(dirname $status_file))
        card=$(basename $(dirname $(dirname $status_file)))
        status=$(cat $status_file 2>/dev/null)
        echo "  🔌 $card/$connector: $status"
    done
else
    echo "  ⚠️  No connector info available"
fi
echo ""

echo "🔍 7. DSI-specific checks:"
echo "───────────────────────────────────────"
dmesg | grep -i -E "dsi|panel" | tail -n 10 | sed 's/^/  /' || echo "  No DSI messages in dmesg"
echo ""

echo "════════════════════════════════════════"
echo "✅ Diagnostic complete!"
echo "════════════════════════════════════════"