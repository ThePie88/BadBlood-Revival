#!/usr/bin/env bash
# setup-local.sh — One-stop local setup for BadBlood-Revival (Linux/macOS)
#
# What it does:
#   1. Verifies python3 and stunnel are installed
#   2. Creates a self-signed cert (CN=pls.dlbb.com) in certs/
#   3. Creates a venv and installs Python dependencies
#   4. Writes a working stunnel.conf
#   5. Tells you what to do next
#
# Run from this folder (release/server/). You'll need sudo to start stunnel
# later (port 443 is privileged).

set -e
cd "$(dirname "$0")"

echo
echo "========================================"
echo "  BadBlood-Revival — Local Setup"
echo "========================================"
echo

# 1. Python
echo "[1/4] Checking Python..."
if ! command -v python3 >/dev/null 2>&1; then
    echo "  [!!] python3 not found"
    echo "  Install: apt install python3 python3-venv python3-pip"
    exit 1
fi
echo "  [OK] $(python3 --version)"

# 2. stunnel
echo "[2/4] Checking stunnel..."
if ! command -v stunnel >/dev/null 2>&1 && ! command -v stunnel4 >/dev/null 2>&1; then
    echo "  [!!] stunnel not found"
    echo "  Install: apt install stunnel4   (or: brew install stunnel)"
    exit 1
fi
echo "  [OK] stunnel is available"

# 3. Self-signed cert
echo "[3/4] Self-signed certificate..."
mkdir -p certs
if [ -f certs/cert.pem ] && [ -f certs/key.pem ]; then
    echo "  [OK] certs/cert.pem and certs/key.pem already exist, keeping them"
else
    if ! command -v openssl >/dev/null 2>&1; then
        echo "  [!!] openssl not found"
        echo "  Install: apt install openssl"
        exit 1
    fi
    openssl req -x509 -newkey rsa:4096 \
        -keyout certs/key.pem -out certs/cert.pem -days 365 -nodes \
        -subj "/CN=pls.dlbb.com" >/dev/null 2>&1
    echo "  [OK] Generated certs/cert.pem and certs/key.pem"
fi

# 4. Python deps in venv
echo "[4/4] Installing Python dependencies..."
if [ ! -d venv ]; then
    python3 -m venv venv
fi
# shellcheck source=/dev/null
source venv/bin/activate
pip install -q -r requirements.txt
echo "  [OK] Dependencies installed in venv/"

# stunnel.conf
if [ ! -f stunnel.conf ] && [ -f stunnel.conf.template ]; then
    cp stunnel.conf.template stunnel.conf
    echo "  [OK] stunnel.conf created from template"
fi

cat <<EOF

========================================
  SETUP COMPLETE
========================================

Next steps:

  1. Add to /etc/hosts (needs sudo):
         echo '127.0.0.1 pls.dlbb.com' | sudo tee -a /etc/hosts

  2. In THIS terminal, run the server backend:
         source venv/bin/activate
         ./run.sh

  3. In ANOTHER terminal, start stunnel as root:
         sudo stunnel stunnel.conf

  4. Patch your game (one-time, from the patcher/ folder):
         python3 apply_patches.py --game-dir "/path/to/DLBB" --local

  5. Drop the built stubs + texture-hook DLLs into the game folder
     (see ../README.md for the file list).

  6. Run the BadBlood-Revival launcher and PLAY.

EOF
