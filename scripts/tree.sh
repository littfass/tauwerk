#!/bin/bash

# Skript zum Speichern der Verzeichnisstruktur in tree.txt
# Aufruf: ./scripts/tree.sh

echo "Erstelle Verzeichnisbaum (backups Ordner wird ausgeschlossen)..."

# Wechsle zum Home-Verzeichnis
cd /home/tauwerk

# Erstelle die tree.txt Datei mit der Verzeichnisstruktur, ohne backups Ordner
tree -f -I 'backups|venv|__pycache__' > ./ki/tree.txt

# Prüfe ob der Befehl erfolgreich war
if [ $? -eq 0 ]; then
    echo "✅ Verzeichnisbaum erfolgreich in /home/tauwerk/tree.txt gespeichert (backups ausgeschlossen)"
else
    echo "❌ Fehler: 'tree' Befehl nicht verfügbar. Installiere mit: sudo apt install tree"
    exit 1
fi