#!/bin/bash
# Kill softKM on both macOS (mini) and Haiku (taurus)

set -e

echo "Killing softKM on mini (macOS)..."
ssh daniel@mini.microgeni.synology.me "pkill -x softKM 2>/dev/null || true"
echo "Done."

echo "Killing softKM on taurus (Haiku)..."
ssh user@taurus.microgeni.synology.me "kill \$(pidof softKM) 2>/dev/null || true"
echo "Done."

echo "softKM stopped on all machines."
