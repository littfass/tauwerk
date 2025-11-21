#!/bin/bash
# Git Snapshot Helper für Tauwerk
# Usage: ./scripts/git_snapshot.sh "Beschreibung der Änderungen"

if [ -z "$1" ]; then
  echo "⚠️  Keine Beschreibung angegeben"
  echo "Usage: ./scripts/git_snapshot.sh \"Deine Commit-Message\""
  exit 1
fi

cd /home/tauwerk

# Status anzeigen
echo "📦 Git Snapshot..."
git add -A

# Commit mit Timestamp
TIMESTAMP=$(date '+%Y-%m-%d %H:%M')
git commit -m "$1

Snapshot: $TIMESTAMP"

# Push wenn Remote vorhanden
if git remote | grep -q 'origin'; then
  echo "🚀 Pushing to remote..."
  git push
  echo "✅ Snapshot committed & pushed"
else
  echo "✅ Snapshot committed (nur lokal, kein remote)"
fi
