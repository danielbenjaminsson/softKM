#!/bin/bash
# Start softKM on both Haiku (taurus) and macOS (mini)

set -e

echo "Starting softKM on taurus (Haiku)..."
ssh user@taurus.microgeni.synology.me "cd /Data/Code/Projects/softKM && ./objects.*/softKM &>/dev/null &"
echo "Done."

echo "Starting softKM on mini (macOS)..."
ssh mini "open /Applications/softKM.app"
echo "Done."

echo "softKM started on all machines."
