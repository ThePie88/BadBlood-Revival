# server-linux/

Linux-specific deployment helpers for the BadBlood-Revival server. The Python
code in `../server/` is portable; this folder adds systemd units, an install
script, and Let's Encrypt notes.

## Quick install on a fresh Ubuntu/Debian VPS

```bash
# 1. Get the repo
git clone https://github.com/ThePie88/BadBlood-Revival.git /opt/badblood-revival
cd /opt/badblood-revival

# 2. Run the installer (as root)
bash server-linux/setup_vps.sh

# 3. Point your DNS A record to this VPS, then:
certbot certonly --standalone -d pls.yourdomain.it
# stunnel config is generated to use Let's Encrypt path; check /etc/stunnel/stunnel.conf

# 4. Start
systemctl enable --now stunnel4 badblood-revival
journalctl -u badblood-revival -f
```

## What `setup_vps.sh` does

- `apt install` Python 3, stunnel4, certbot, nginx (optional reverse proxy)
- Creates `/opt/badblood-revival/server/venv` and installs deps
- Generates self-signed cert as initial bootstrap (replace with Let's Encrypt)
- Writes `/etc/stunnel/stunnel.conf`
- Writes `/etc/systemd/system/badblood-revival.service`
- Enables both services

You can run it on a re-deploy too — it's idempotent for the system-wide
bits but won't touch existing certs.

## Files

- `setup_vps.sh` — one-shot installer (copy of the historical `setup_vps.sh`,
  paths normalized for `/opt/badblood-revival`)
- `goldberg_relay.py` — alternative standalone relay process if you want
  the Goldberg relay decoupled from the main server (the default in
  `../server/main.py` runs them in the same process)
- `udp_relay.py` — minimal UDP-only relay (legacy, kept for reference)

## Notes

- Port 47584 (Goldberg) must be reachable from the public internet for
  matchmaking to work behind CGNAT. `ufw allow 47584` after enabling ufw.
- Port 80 doesn't strictly need to be public if you only serve the game
  via stunnel on 443. But if you offer the launcher API over HTTP, open
  it too.
- The Python backend listens on `DLBB_HOST` (default `0.0.0.0`). On a
  hardened VPS you may prefer `127.0.0.1` and have stunnel be the only
  public listener.
