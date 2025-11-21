#!/bin/bash
# TAUWERK SYSTEM BACKUP

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
BACKUP_BASE="/home/tauwerk/backups/config"
BACKUP_DIR="$BACKUP_BASE/$TIMESTAMP"
LATEST_DIR="$BACKUP_BASE/latest"

echo "Creating system backup: $BACKUP_DIR"

# Verzeichnis als normaler User erstellen (kein sudo!)
mkdir -p "$BACKUP_DIR"

# System Configuration Files backupen (MIT SUDO für System-Files)
sudo cp /boot/firmware/cmdline.txt "$BACKUP_DIR/"
sudo cp /boot/firmware/config.txt "$BACKUP_DIR/"
sudo cp /etc/systemd/system/tauwerk.service "$BACKUP_DIR/" 2>/dev/null || true
sudo cp /etc/hosts "$BACKUP_DIR/" 2>/dev/null || true
sudo cp /etc/dhcpcd.conf "$BACKUP_DIR/" 2>/dev/null || true

# Berechtigungen korrigieren (sudo-copierte Files gehören root → tauwerk)
sudo chown -R tauwerk:tauwerk "$BACKUP_DIR"

# Backup Info (JETZT mit korrekten Berechtigungen)
echo "Backup created: $TIMESTAMP" > "$BACKUP_DIR/backup.info"
echo "Script: backup.sh" >> "$BACKUP_DIR/backup.info"
echo "User: $(whoami)" >> "$BACKUP_DIR/backup.info"

# LATEST SYMLINK ERSTELLEN
rm -f "$LATEST_DIR" 2>/dev/null || true
ln -sf "$BACKUP_DIR" "$LATEST_DIR"

echo "Backup complete: $BACKUP_DIR"
echo "Latest symlink: $LATEST_DIR → $TIMESTAMP"
echo "Restore with: ./scripts/restore.sh $BACKUP_DIR"