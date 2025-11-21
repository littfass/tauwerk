#!/bin/bash
# TAUWERK SYSTEM RESTORE

# Parameter oder neuestes Backup
if [ -n "$1" ]; then
    BACKUP_DIR="$1"
else
    BACKUP_DIR=$(ls -d /home/tauwerk/backups/config/* 2>/dev/null | tail -1)
    if [ -z "$BACKUP_DIR" ]; then
        echo "No backups found in /home/tauwerk/backups/config/"
        echo "💡 Usage: $0 [backup_directory]"
        exit 1
    fi
fi

# Backup validieren
if [ ! -f "$BACKUP_DIR/cmdline.txt" ]; then
    echo "Invalid backup directory: $BACKUP_DIR"
    echo "Backup must contain cmdline.txt"
    exit 1
fi

echo "Restoring from: $BACKUP_DIR"

# System Files restore
sudo cp "$BACKUP_DIR/cmdline.txt" /boot/firmware/cmdline.txt
sudo cp "$BACKUP_DIR/config.txt" /boot/firmware/config.txt
sudo cp "$BACKUP_DIR/tauwerk.service" /etc/systemd/system/tauwerk.service 2>/dev/null || true
sudo cp "$BACKUP_DIR/hosts" /etc/hosts 2>/dev/null || true
sudo cp "$BACKUP_DIR/dhcpcd.conf" /etc/dhcpcd.conf 2>/dev/null || true

echo "Restore complete from $BACKUP_DIR"
echo "REBOOT to apply system changes"