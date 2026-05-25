#!/usr/bin/env bash
# BadBlood-Revival — start the server on Linux/macOS
#
# Starts the FastAPI backend in this terminal. Run stunnel separately for HTTPS.

set -e
cd "$(dirname "$0")"

# Activate venv if present
if [ -f venv/bin/activate ]; then
    source venv/bin/activate
fi

# Verify Python is reachable
if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 not found. Install Python 3.10+."
    exit 1
fi

# Install deps if missing
if ! python3 -c "import fastapi" 2>/dev/null; then
    echo "[setup] Installing dependencies..."
    python3 -m pip install -r requirements.txt
fi

# Load .env if present
if [ -f .env ]; then
    set -a
    source .env
    set +a
fi

echo "========================================"
echo "  BadBlood-Revival Server"
echo "========================================"
exec python3 main.py
