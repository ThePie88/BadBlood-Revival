#!/bin/bash
# BadBlood-Revival — VPS setup script
# Run as root on a fresh Ubuntu/Debian VPS.
#
# Usage:
#   sudo bash server-linux/setup_vps.sh pls.example.it
#
# After this, point your DNS to this VPS, then run:
#   certbot certonly --standalone -d pls.example.it
# and update /etc/stunnel/stunnel.conf to use the Let's Encrypt cert paths.

set -e

DOMAIN="${1:-pls.example.it}"
INSTALL_DIR="${INSTALL_DIR:-/opt/badblood-revival}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "========================================="
echo "  BadBlood-Revival — VPS Setup"
echo "========================================="
echo "  Domain:    $DOMAIN"
echo "  Install:   $INSTALL_DIR"
echo "  Source:    $REPO_ROOT"
echo ""

# 1/6 Update system
echo "[1/6] Updating system..."
apt update && apt upgrade -y

# 2/6 Install dependencies
echo "[2/6] Installing dependencies..."
apt install -y python3 python3-pip python3-venv stunnel4 certbot ufw

# 3/6 Install to /opt
echo "[3/6] Installing to $INSTALL_DIR..."
mkdir -p "$INSTALL_DIR"
cp -r "$REPO_ROOT/." "$INSTALL_DIR/"
cd "$INSTALL_DIR/server"

# Python venv
python3 -m venv venv
# shellcheck source=/dev/null
source venv/bin/activate
pip install -r requirements.txt

# 4/6 Self-signed bootstrap cert (replace with Let's Encrypt below)
echo "[4/6] Generating self-signed bootstrap certificate (replace with Let's Encrypt)..."
mkdir -p certs
openssl req -x509 -newkey rsa:4096 \
    -keyout certs/key.pem -out certs/cert.pem \
    -days 365 -nodes \
    -subj "/CN=$DOMAIN"

# 5/6 Configure stunnel
echo "[5/6] Configuring stunnel..."
cat > /etc/stunnel/stunnel.conf <<EOF
; BadBlood-Revival — stunnel config
pid = /var/run/stunnel.pid
setuid = stunnel4
setgid = stunnel4

[pls]
accept = 443
connect = 127.0.0.1:80
cert = $INSTALL_DIR/server/certs/cert.pem
key = $INSTALL_DIR/server/certs/key.pem
TIMEOUTclose = 0
EOF

# Enable stunnel
sed -i 's/^ENABLED=0/ENABLED=1/' /etc/default/stunnel4 || true

# 6/6 Systemd unit for the Python backend
echo "[6/6] Creating systemd service..."

# Write env file
cat > /etc/default/badblood-revival <<EOF
# Environment for badblood-revival.service. Override here, not in the unit.
DLBB_HOST=0.0.0.0
DLBB_HTTP_PORT=80
DLBB_DOMAIN=$DOMAIN
DLBB_DB_PATH=$INSTALL_DIR/server/data/dlbb.db
DLBB_LOG_FILE=$INSTALL_DIR/server/pls-emu.log
EOF

cat > /etc/systemd/system/badblood-revival.service <<EOF
[Unit]
Description=BadBlood-Revival — DLBB PLS server emulator
After=network.target stunnel4.service

[Service]
Type=simple
User=root
EnvironmentFile=/etc/default/badblood-revival
WorkingDirectory=$INSTALL_DIR/server
ExecStart=$INSTALL_DIR/server/venv/bin/python -m uvicorn main:app --host \${DLBB_HOST} --port \${DLBB_HTTP_PORT}
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

# Open ports
echo "[ufw] opening ports 80, 443, 47584..."
ufw allow 80/tcp  >/dev/null || true
ufw allow 443/tcp >/dev/null || true
ufw allow 47584   >/dev/null || true

# Enable and start
systemctl daemon-reload
systemctl enable --now stunnel4 badblood-revival

echo ""
echo "========================================="
echo "  BadBlood-Revival INSTALLED"
echo "========================================="
echo ""
echo "  Backend:  http://localhost:80"
echo "  HTTPS:    https://localhost:443 (stunnel)"
echo "  Domain:   $DOMAIN"
echo ""
echo "  Commands:"
echo "    systemctl status badblood-revival    — check backend"
echo "    systemctl status stunnel4            — check TLS termination"
echo "    journalctl -u badblood-revival -f    — live logs"
echo ""
echo "  Next steps:"
echo "    1. Point DNS A record for $DOMAIN at this server's public IP"
echo "    2. Replace the bootstrap self-signed cert with Let's Encrypt:"
echo "         certbot certonly --standalone -d $DOMAIN"
echo "         # then edit /etc/stunnel/stunnel.conf to use /etc/letsencrypt/..."
echo "         systemctl restart stunnel4"
echo "    3. Verify: curl -sk https://$DOMAIN/auth/login/steam/ -X POST -d 'auth_session_ticket=test'"
echo ""
