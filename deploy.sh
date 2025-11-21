#!/bin/bash
set -e
echo "TAUWERK DEPLOYMENT"
echo "Setting permissions..."
find . -name "*.sh" -exec chmod +x {} \;
chmod +x scripts/*.sh

echo "Running scripts..."
./scripts/backup.sh
./scripts/configure.sh
./scripts/setup.sh
./build.sh
./scripts/services.sh

echo "DEPLOYMENT COMPLETE"