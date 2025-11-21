#!/bin/bash
echo "CONFIGURE SYSTEM"

# 1. Services optimieren
sudo systemctl mask bluetooth
sudo systemctl disable apt-daily.timer
sudo systemctl disable man-db.timer
sudo systemctl mask systemd-binfmt.service
sudo systemctl mask proc-sys-fs-binfmt_misc.mount
sudo systemctl disable irqbalance

for tty in tty1 tty2 tty3 tty4 tty5 tty6; do
    if systemctl is-enabled getty@$tty > /dev/null 2>&1; then
        sudo systemctl disable getty@$tty
        echo "Console on $tty disabled"
    fi
done

if [ ! -f /etc/systemd/system/NetworkManager.service.d/override.conf ]; then
    sudo mkdir -p /etc/systemd/system/NetworkManager.service.d/
    sudo tee /etc/systemd/system/NetworkManager.service.d/override.conf > /dev/null << 'EOF'
[Service]
TimeoutStartSec=3s
EOF
    echo "NetworkManager timeout optimized"
    echo "Resore with: sudo rm -rf /etc/systemd/system/NetworkManager.service.d/"
fi

# 2. Boot Parameter optimieren
if ! grep -q "systemd.default_timeout_start_sec" /boot/firmware/cmdline.txt; then
    echo "systemd.default_timeout_start_sec=5s" | sudo tee -a /boot/firmware/cmdline.txt
fi

if ! grep -q "dwc_otg.fiq_fsm_mask" /boot/firmware/config.txt; then
    echo "dwc_otg.fiq_fsm_mask=0x3" | sudo tee -a /boot/firmware/config.txt
fi

if ! grep -q "i2c_arm_baudrate=400000" /boot/firmware/config.txt; then
    echo "dtparam=i2c_arm=on" | sudo tee -a /boot/firmware/config.txt
    echo "dtparam=i2c_arm_baudrate=400000" | sudo tee -a /boot/firmware/config.txt
    echo "I2C optimized: 400kHz"
fi

# I2C1 für zusätzliche Ports
if ! grep -q "i2c1_baudrate=400000" /boot/firmware/config.txt; then
    echo "dtparam=i2c1=on" | sudo tee -a /boot/firmware/config.txt  
    echo "dtparam=i2c1_baudrate=400000" | sudo tee -a /boot/firmware/config.txt
    echo "I2C1 optimized: 400kHz"
fi

# 3. Cloud-init deaktivieren (BIG WIN!)
if [ ! -f /etc/cloud/cloud-init.disabled ]; then
    sudo touch /etc/cloud/cloud-init.disabled
    echo "Cloud-init disabled"
fi

# 4. AUTOMATISCHE NETWORK DETECTION
if ! grep -q "interface wlan0" /etc/dhcpcd.conf; then
    echo "Auto-detecting network configuration..."
    
    # Aktuelle Network Config auslesen
    CURRENT_IP=$(ip -4 addr show wlan0 | grep -oP '(?<=inet\s)\d+(\.\d+){3}/\d+')
    ROUTER_IP=$(ip route show default | grep -oP '(?<=via\s)\d+(\.\d+){3}')
    DNS_SERVER=$(grep nameserver /etc/resolv.conf | head -1 | awk '{print $2}')
    
    # Falls keine IP via DHCP, verwende Standard
    if [ -z "$CURRENT_IP" ]; then
        NETWORK_BASE="192.168.1"
        STATIC_IP="${NETWORK_BASE}.100/24"
        ROUTER_IP="${NETWORK_BASE}.1"
        DNS_SERVER="${NETWORK_BASE}.1"
        echo "No DHCP lease - using fallback: $STATIC_IP"
    else
        # Extrahiere Network Base aus current IP (192.168.1.100/24 → 192.168.1)
        NETWORK_BASE=$(echo "$CURRENT_IP" | cut -d. -f1-3)
        STATIC_IP="${NETWORK_BASE}.100/24"
        echo "Detected network: $NETWORK_BASE.0/24"
    fi
    
    # Static IP konfigurieren
    echo "interface wlan0" | sudo tee -a /etc/dhcpcd.conf
    echo "static ip_address=$STATIC_IP" | sudo tee -a /etc/dhcpcd.conf
    echo "static routers=$ROUTER_IP" | sudo tee -a /etc/dhcpcd.conf
    echo "static domain_name_servers=$ROUTER_IP 8.8.8.8" | sudo tee -a /etc/dhcpcd.conf
    
    echo "Static IP configured: $STATIC_IP"
fi

# 5. Journald optimieren
if ! grep -q "Storage=volatile" /etc/systemd/journald.conf; then
    echo "Storage=volatile" | sudo tee -a /etc/systemd/journald.conf
    echo "SystemMaxUse=16M" | sudo tee -a /etc/systemd/journald.conf
    echo "Journald optimized"
fi

echo "CONFIGURE FRAMEBUFFER"
echo "U:800x480p-0" | sudo tee /sys/class/graphics/fb0/mode
echo 0 | sudo tee /sys/class/graphics/fb0/blank
echo 1 | sudo tee /sys/class/graphics/fb0/bits_per_pixel

# CPU Governor auf Performance
#echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
#sudo sh -c 'echo ondemand | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor'


echo "OPTIMIZING SHUTDOWN"

# Nur moderate Optimierung - SICHER!
echo "DefaultTimeoutStopSec=10s" | sudo tee -a /etc/systemd/system.conf
echo "DefaultTimeoutAbortSec=5s" | sudo tee -a /etc/systemd/system.conf

# Tauwerk Service schnell beenden
sudo mkdir -p /etc/systemd/system/tauwerk.service.d/
sudo tee /etc/systemd/system/tauwerk.service.d/override.conf > /dev/null << 'EOF'
[Service]
TimeoutStopSec=3s
EOF

sudo systemctl daemon-reload

echo "System configured - REBOOT to apply (~8-10s boot expected)"
echo "Your Pi will be available at: ${STATIC_IP%%/*} (static) OR pi.local"