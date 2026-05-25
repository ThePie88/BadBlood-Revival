# HOW TO HOST — Server hosting guide

This guide is for people who want to **run a server** that other players
connect to. Three scenarios are covered, from easiest to most ambitious:

1. [Local server, just you](#scenario-1--local-server-just-you) — single PC, single player
2. [LAN server, you + friends in the same house](#scenario-2--lan-server) — same Wi-Fi
3. [Public server, anyone on the internet](#scenario-3--public-vps-server) — needs a VPS + domain

> If you just want to **play** the game (with or without a server), see
> [HOW_TO_PLAY.md](HOW_TO_PLAY.md) first.

---

## Scenario 1 — Local server, just you

The simplest setup. Server runs on your PC, only you play.

**This is identical to the standard player setup in
[HOW_TO_PLAY.md](HOW_TO_PLAY.md).** Follow that guide entirely.

Pros:
- Zero networking knowledge needed
- No public IP or domain required
- Works offline once you've installed everything

Cons:
- Only you can play
- Goldberg can't find lobbies from other players over the internet

If that's all you want, stop reading and use HOW_TO_PLAY.md.

---

## Scenario 2 — LAN server

You + friends on the same Wi-Fi or wired LAN. One PC is the "server
host" (it runs the FastAPI backend + stunnel + Goldberg relay).
Everyone else connects to it as players.

### Architecture

```
                Host PC (192.168.1.50)
                ┌─────────────────────────────────┐
                │ FastAPI backend  :80            │
                │ stunnel          :443           │
                │ Goldberg relay   :47584         │
                │ BadBloodGame.exe (you play too) │
                └─────────────┬───────────────────┘
                              │ LAN
       ┌──────────────────────┼─────────────────────┐
       │                      │                     │
   Player B (192.168.1.51)   ...               Player N (192.168.1.5X)
   BadBloodGame.exe          BadBloodGame.exe
```

### What the host does

Set everything up exactly as in [HOW_TO_PLAY.md](HOW_TO_PLAY.md), then
also:

1. **Find your LAN IP**:
   - `Win + R`, type `cmd`, Enter
   - Type `ipconfig`, look for "IPv4 Address" under your Wi-Fi or
     Ethernet adapter. Something like `192.168.1.50` or `10.0.0.5`.

2. **Open Windows Firewall ports**:
   - `Win + R`, type `wf.msc`, Enter (Windows Firewall with Advanced Security)
   - Inbound Rules → New Rule
   - Port → TCP → Specific local ports: `80, 443`
   - Allow the connection
   - All profiles (Domain, Private, Public)
   - Name: "BadBlood-Revival TCP"
   - Repeat for UDP port `47584` (this is the Goldberg matchmaking relay)

3. **Rebuild the launcher with the LAN IP** (if you used the pre-built
   one, you'll need to recompile — see [HOW_TO_BUILD.md](HOW_TO_BUILD.md)):
   - Open `launcher/src/main.cpp`
   - Change `#define VPS_IP "127.0.0.1"` to your LAN IP, e.g.
     `#define VPS_IP "192.168.1.50"`
   - `SERVER_HOST` can stay `127.0.0.1` if the launcher only runs on
     the host PC. If other players use the launcher to log in, change
     it to your LAN IP too.
   - Run `build.bat`

4. **Distribute to friends**:
   - The patched game folder (or instructions for them to patch their own)
   - The new `PIE-LAUNCHER.exe`
   - The same `.dll` stubs and Goldberg files

### What each player does

Each player needs their own patched game copy on their own PC:

1. Follow [HOW_TO_PLAY.md](HOW_TO_PLAY.md) Steps 1, 2, 3, 4, 7, 8, 9, 10
   (skip steps 5 and 6 — they don't run a server, the host does).
2. Replace `PIE-LAUNCHER.exe` with the one the host built (pointed at the LAN IP).
3. Edit their **own** hosts file:
   - `notepad` as Administrator → `C:\Windows\System32\drivers\etc\hosts`
   - Add line: `192.168.1.50 pls.dlbb.com` (replace IP with the host's LAN IP)
4. Run the launcher, register, play.

### Caveats

- **Don't change SERVER_HOST if everyone shares one launcher**. Players
  using the same launcher all hit the same server URL. Fine.
- **Goldberg's broadcast discovery may also work** without our relay if
  everyone is on the same Wi-Fi — Goldberg auto-broadcasts on the LAN.
- **NAT'd Wi-Fi (e.g. mobile hotspots)** might isolate clients from
  each other. Use a real router.

---

## Scenario 3 — Public VPS server

For internet multiplayer. Players join from anywhere with a public IP
or domain pointing to your VPS.

Works on **both Linux and Windows** servers — the Python backend is
cross-platform, only the deploy automation differs. Pick the OS you
have a VPS for.

### What you'll need (both platforms)

- A VPS — smallest tier on any provider is fine (1 CPU, 1 GB RAM, 10 GB disk)
- A domain name **exactly 12 ASCII characters long** (see [the 12-char
  constraint](README.md#the-12-character-hostname-constraint))
  - Examples that work: `pls.foobar.it`, `dlbb.host.io`, `bb-game.io`
  - Examples that don't: `pls.mygame.com` (13 chars), `dlbb.com` (8 chars)
- DNS access to set an A record for that domain
- Open inbound TCP `80`, `443`, and UDP/TCP `47584` (OS firewall + cloud
  provider firewall)

### Linux VPS — Step 1

Ubuntu 22.04 / Debian 12 recommended. SSH in as root.

```bash
# 1. Clone the repo
git clone https://github.com/ThePie88/BadBlood-Revival.git /opt/badblood-revival

# 2. Run the install script (replace with your actual 12-char domain)
bash /opt/badblood-revival/server-linux/setup_vps.sh pls.example.it
```

The script:
- Installs python3, stunnel4, certbot
- Creates a venv and installs FastAPI dependencies
- Generates a self-signed bootstrap cert (you'll replace with Let's Encrypt)
- Writes `/etc/stunnel/stunnel.conf`
- Writes `/etc/default/badblood-revival` (environment file)
- Writes `/etc/systemd/system/badblood-revival.service`
- Opens firewall ports (`ufw`)
- Enables and starts everything

You should see at the end:

```
=========================================
  BadBlood-Revival INSTALLED
=========================================
  systemctl status badblood-revival    — check backend
  systemctl status stunnel4            — check TLS termination
  journalctl -u badblood-revival -f    — live logs
```

### Windows VPS — Step 1

Windows Server 2019/2022, or Windows 10/11 with public IP. RDP in,
open Command Prompt **as Administrator**.

```cmd
:: 1. Clone the repo
git clone https://github.com/ThePie88/BadBlood-Revival.git C:\BadBloodRevival

:: 2. Install the prerequisites
::    (winget works on Windows 10/11 and Windows Server 2022.
::     On older Windows Server, install winget manually first, OR
::     download the installers directly — see server-windows/README.md.)
winget install Python.Python.3.12
winget install MichalTrojnara.Stunnel
winget install ShiningLight.OpenSSL.Light

:: 3. Run the install script (replace with your actual 12-char domain)
cd C:\BadBloodRevival\server-windows
setup-public.bat pls.example.it
```

The script:
- Verifies Python, stunnel, openssl are installed
- Installs FastAPI dependencies (pip)
- Generates a self-signed bootstrap cert
- Writes `server\stunnel.conf` with absolute paths
- Installs **stunnel as a Windows service** (`net start/stop stunnel`)
- Creates a **scheduled task** (`BadBloodRevival-Server`) for the FastAPI
  backend — runs at boot as SYSTEM, auto-restarts on failure (Windows'
  equivalent of `systemd Restart=always`)
- Opens Windows Firewall inbound ports 80, 443, 47584
- Starts everything immediately

You should see at the end:

```
=========================================
  SETUP COMPLETE
=========================================
  schtasks /Query /TN "BadBloodRevival-Server"   -- backend status
  sc query stunnel                                -- stunnel service status
```

See [`server-windows/README.md`](server-windows/README.md) for Windows-
specific management commands, Let's Encrypt via `win-acme`, logs, and
backups.

### Step 2 — DNS (both platforms)

In your DNS provider's panel:
1. Add an A record:
   - Name: the **subdomain part** of your 12-char hostname (e.g. `pls`)
   - Type: A
   - Value: your VPS's public IP
     - Linux: `curl ifconfig.me` on the VPS
     - Windows: `curl ifconfig.me` (works if curl is installed) or
       check your cloud provider's panel
   - TTL: 300 (default fine)
2. Wait 1-5 minutes for DNS propagation
3. Verify from your local machine:
   ```cmd
   nslookup pls.example.it
   ```
   You should see the VPS IP.

### Step 3 — Real TLS certificate (replace the bootstrap self-signed)

#### Linux: Let's Encrypt via certbot

```bash
# On the VPS, as root:
certbot certonly --standalone -d pls.example.it
# Follow the prompts (email + accept TOS)
```

Certificate will be saved to `/etc/letsencrypt/live/pls.example.it/`.

Update stunnel to use it:

```bash
nano /etc/stunnel/stunnel.conf
```

Change the `cert=` and `key=` lines to:

```
cert = /etc/letsencrypt/live/pls.example.it/fullchain.pem
key  = /etc/letsencrypt/live/pls.example.it/privkey.pem
```

Save (Ctrl+O, Enter, Ctrl+X). Restart stunnel:

```bash
systemctl restart stunnel4
```

#### Windows: Let's Encrypt via win-acme

The standard ACME client on Windows is [win-acme](https://www.win-acme.com/).

1. On the Windows VPS, download win-acme from <https://www.win-acme.com/>
2. Extract it to e.g. `C:\Tools\win-acme`
3. **Stop stunnel temporarily** (port 80 must be free for the HTTP-01
   challenge):
   ```cmd
   net stop stunnel
   schtasks /End /TN "BadBloodRevival-Server"
   ```
4. Open Command Prompt as Administrator, run:
   ```cmd
   cd C:\Tools\win-acme
   wacs.exe
   ```
5. Pick "Create renewal (with advanced options)" → "Manual input" →
   enter your domain (`pls.example.it`) → HTTP-01 validation → "PemFiles"
   plugin → output path `C:\BadBloodRevival\server\certs\`
6. Configure post-renewal script (in the wacs flow): a `.bat` that
   restarts stunnel:
   ```cmd
   net stop stunnel
   net start stunnel
   ```
7. Restart services:
   ```cmd
   net start stunnel
   schtasks /Run /TN "BadBloodRevival-Server"
   ```

win-acme creates a Windows scheduled task that renews the cert before
expiry. Verify with:
```cmd
schtasks /Query /TN "win-acme renew (acme-v02.api.letsencrypt.org)"
```

See [`server-windows/README.md`](server-windows/README.md) for more
detail.

### Step 4 — Test from outside

From your local PC:

```cmd
curl -sk https://pls.example.it/auth/login/steam/ -X POST -d "auth_session_ticket=test"
```

Should return `{"token":"...","enabled":3071}`. If it does, your server
is live on the internet.

### Step 5 — Build players' launchers and patcher invocations

For your community, you provide a **custom launcher build** with your
domain compiled in:

1. Edit `launcher/src/main.cpp`:
   ```c
   #define SERVER_HOST     "pls.example.it"  // your 12-char domain
   #define VPS_IP          "1.2.3.4"         // your VPS public IP
   ```
2. Run `build.bat` — produces `PIE-LAUNCHER.exe`
3. Same edit for `texture-hook/texture_hook.cpp` (`VPS_IP` and the byte
   computation), and `stubs/src/eac_x64.c` + `dns_redirect.c`
   (`NEW_HOST`). Rebuild them too.
4. Bundle all the resulting `.exe` + `.dll`s into a ZIP.

Distribute the ZIP to your players, plus instructions for them:

1. Buy DLBB on Steam.
2. Steamless unpack `BadBloodGame.exe` (per [HOW_TO_PLAY.md](HOW_TO_PLAY.md) Step 4).
3. Run patcher with your domain:
   ```cmd
   python apply_patches.py --game-dir "C:\DLBB" --server-host pls.example.it
   ```
4. Drop the ZIP contents into the game folder.
5. Run launcher, register, play.

### Step 6 — Renewing the Let's Encrypt cert

Certbot installs a systemd timer that auto-renews. Verify:

```bash
systemctl list-timers | grep certbot
```

Should show a timer like `snap.certbot.renew.timer`. If the cert is
renewed, stunnel needs to be told to reload it — add a deploy hook:

```bash
mkdir -p /etc/letsencrypt/renewal-hooks/deploy/
cat > /etc/letsencrypt/renewal-hooks/deploy/restart-stunnel.sh <<'EOF'
#!/bin/bash
systemctl restart stunnel4
EOF
chmod +x /etc/letsencrypt/renewal-hooks/deploy/restart-stunnel.sh
```

Test the renewal flow:

```bash
certbot renew --dry-run
```

---

## Operational notes

### Logs

**Linux:**
```bash
journalctl -u badblood-revival -f      # live backend log
journalctl -u stunnel4 -f              # live TLS log
tail -f /opt/badblood-revival/server/pls-emu.log  # FastAPI app log
```

**Windows:**
```powershell
# Live backend log:
Get-Content C:\BadBloodRevival\server\pls-emu.log -Wait
# stunnel log: see C:\Program Files (x86)\stunnel\bin\stunnel.log
# Scheduled task run history: open taskschd.msc, find BadBloodRevival-Server
```

### Backups

The database is at:
- Linux:   `/opt/badblood-revival/server/data/dlbb.db`
- Windows: `C:\BadBloodRevival\server\data\dlbb.db`

It's small (a few MB even with thousands of players). Back it up regularly.

**Linux (cron):**
```bash
# Add to /etc/cron.daily/badblood-backup
#!/bin/bash
mkdir -p /var/backups/badblood
sqlite3 /opt/badblood-revival/server/data/dlbb.db ".backup '/var/backups/badblood/dlbb-$(date +%Y%m%d).db'"
# Keep last 30 days
find /var/backups/badblood -mtime +30 -delete
```

**Windows (PowerShell + Task Scheduler):**
```powershell
# Save as C:\BadBloodRevival\backup.ps1
$src = "C:\BadBloodRevival\server\data\dlbb.db"
$dst = "C:\Backups\BadBlood\dlbb-$(Get-Date -Format yyyyMMdd).db"
New-Item -ItemType Directory -Path (Split-Path $dst) -Force | Out-Null
Copy-Item $src $dst
Get-ChildItem "C:\Backups\BadBlood\dlbb-*.db" |
    Where-Object { $_.LastWriteTime -lt (Get-Date).AddDays(-30) } |
    Remove-Item
```
Schedule it daily with `taskschd.msc`.

### Updates

**Linux:**
```bash
cd /opt/badblood-revival
git pull
systemctl restart badblood-revival
```

**Windows:**
```cmd
cd C:\BadBloodRevival
git pull
schtasks /End /TN "BadBloodRevival-Server"
schtasks /Run /TN "BadBloodRevival-Server"
```

The SQLite schema migration is automatic on both — `db.init_db()` runs
on startup and adds any new tables/columns.

### Monitoring (optional)

The server exposes plain endpoints. A health check is just:

```bash
curl -fsS https://pls.example.it/api/version || echo "DOWN"
```

Integrate with whatever monitoring you use (UptimeRobot, Healthchecks.io,
Prometheus, etc.).

### Player management

Open the SQLite database with the `sqlite3` CLI:

```bash
# Linux:
sqlite3 /opt/badblood-revival/server/data/dlbb.db

:: Windows (after installing sqlite3 CLI via winget install SQLite.SQLite):
sqlite3 C:\BadBloodRevival\server\data\dlbb.db
```

Useful queries:

```sql
-- How many players registered
SELECT count(*) FROM players;

-- Most recent registrations
SELECT pls_id, nick, created_at FROM players ORDER BY created_at DESC LIMIT 10;

-- Reset a player's items (e.g. after a bug)
DELETE FROM player_items WHERE pls_id = 123;
-- Re-insert defaults — the game refetches on login

-- Ban a player (no real ban system yet, but you can delete their account)
DELETE FROM players WHERE pls_id = 123;
DELETE FROM player_items WHERE pls_id = 123;
```

### Performance

- 1 vCPU, 1 GB RAM handles hundreds of concurrent players. The PLS
  protocol is HTTP polling, not real-time streams.
- The Goldberg relay (UDP+TCP 47584) carries the matchmaking traffic
  but not gameplay. Gameplay is P2P between players.
- Disk usage stays small: SQLite scales fine to millions of rows.

### Security

- The self-signed cert is fine for the **game** (the engine's TLS
  verification is patched off) but **not** for the launcher/website UI.
  Use Let's Encrypt for anything launcher-facing.
- The catch-all endpoint in `main.py` logs every unknown request. If
  you see suspicious POSTs (SQL injection attempts, etc.), they're all
  hitting a no-op handler — but check the log for patterns.
- Player passwords are SHA-256 hashed without salt. Not great, not
  terrible. PRs to add bcrypt welcome.
- Run the backend as a **non-privileged user** in production. The
  default install scripts use `root` (Linux) / `SYSTEM` (Windows) for
  simplicity. For a long-running deploy:
  - Linux: create a `badblood` user, `chown -R` the install dir,
    update the systemd unit's `User=` line, restart
  - Windows: change the scheduled task to run as a dedicated local user
    (Task Scheduler → BadBloodRevival-Server → Properties → "Change User
    or Group") instead of SYSTEM

### Migration (move server to a new VPS)

**Same on Linux and Windows:**

1. On old VPS: stop services, copy `server/data/dlbb.db` and any
   custom `.env` / `stunnel.conf` to a backup
2. On new VPS: run the appropriate setup script, restore the files,
   restart services
3. Update DNS A record to point at the new IP
4. Wait for DNS propagation, verify with `curl -sk https://<domain>/...`

---

## Going from local to public (later)

If you started with a local setup (HOW_TO_PLAY) and want to graduate to
hosting publicly:

1. Get a 12-char domain
2. Set up a VPS following Scenario 3
3. Re-patch your game files with `--server-host <yourdomain>` instead
   of `--local` (after restoring `.original`)
4. Rebuild the launcher with your domain/IP
5. Update DNS, test
6. Tell your players the new server URL

The local setup keeps working in parallel — it's just a different
hostname in the binaries.

---

## Costs (rough)

| Cost                          | Amount                 |
|-------------------------------|------------------------|
| 12-char domain registration   | $5-20/year             |
| Smallest VPS (1 CPU, 1GB RAM) | $3-6/month             |
| Let's Encrypt certificate     | Free                   |
| Total for a year              | ~$40-80                |

You can host this for less than a Steam game cost per year.

---

## When something breaks

Open an issue at <https://github.com/ThePie88/BadBlood-Revival/issues>
with:

- Your OS (Linux distro + version, or Windows version)
- Service status:
  - Linux: `systemctl status badblood-revival` and `systemctl status stunnel4`
  - Windows: `schtasks /Query /TN "BadBloodRevival-Server" /V /FO LIST`
    and `sc query stunnel`
- Backend logs (last ~50 lines):
  - Linux: `journalctl -u badblood-revival -n 50`
  - Windows: last 50 lines of `C:\BadBloodRevival\server\pls-emu.log`
- What you tried, what failed
- Whether DNS resolves correctly (`nslookup pls.example.it`)
