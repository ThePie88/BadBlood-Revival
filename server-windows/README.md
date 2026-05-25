# server-windows/

Helpers for hosting a BadBlood-Revival server **publicly** on a Windows
machine (typically Windows Server 2019/2022 on a VPS like Azure, AWS EC2
Windows, Hetzner Windows, etc.).

> If you want to host **locally** on Windows (single PC, just you or LAN
> with friends), use `server/setup-local.bat` instead. Much simpler.
>
> If you're on Linux, see [`../server-linux/`](../server-linux/).

## Quick install

On the Windows VPS (RDP in, open PowerShell or Command Prompt **as
Administrator**):

```cmd
:: 1. Clone the repo
git clone https://github.com/ThePie88/BadBlood-Revival.git C:\BadBloodRevival
cd C:\BadBloodRevival

:: 2. Install the prerequisites (winget on Windows 10/11/Server 2022)
winget install Python.Python.3.12
winget install MichalTrojnara.Stunnel
winget install ShiningLight.OpenSSL.Light

:: 3. Run the public-host setup (uses your 12-char domain)
cd server-windows
setup-public.bat pls.example.it
```

The script will:
- Verify dependencies are in PATH
- Install Python dependencies
- Generate a self-signed bootstrap certificate
- Write `server\stunnel.conf` with absolute paths
- Install **stunnel as a Windows service** (the equivalent of a Linux
  systemd unit)
- Create a **scheduled task** for the FastAPI backend that:
  - Runs at system startup
  - Auto-restarts on failure (up to 99 times, 1-minute delay)
  - Runs as SYSTEM (no logon required)
- Open Windows Firewall inbound ports 80, 443, 47584

## After install

### Verify it's running

```cmd
:: Backend
schtasks /Query /TN "BadBloodRevival-Server"

:: stunnel
sc query stunnel

:: External test (from another machine, replace with your domain)
curl -sk https://pls.example.it/auth/login/steam/ -X POST -d "auth_session_ticket=test"
:: Expected: {"token":"...","enabled":3071}
```

### Set up DNS

In your DNS provider's panel:
- Add an **A record** for the subdomain part of your 12-char hostname
- Pointed at the VPS's public IP
- TTL 300

`nslookup pls.example.it` from your home PC should return the VPS IP.

### Replace the self-signed certificate with Let's Encrypt

The default install uses a self-signed cert. The game accepts it
(engine SSL verification is patched off) but **browsers and the launcher
will reject it** when used over HTTPS.

For Windows, the standard ACME client is [win-acme](https://www.win-acme.com/):

1. Download win-acme from <https://www.win-acme.com/>
2. Extract to e.g. `C:\Tools\win-acme`
3. Open **PowerShell as Administrator**
4. Run `wacs.exe`
5. Choose "Create renewal (with advanced options)"
6. Pick "Single binding of an IIS website" → "No (manual input)"
7. Enter your domain (`pls.example.it`)
8. Validation: choose **HTTP-01** (`[http]` self-hosted) — win-acme will
   spin up a tiny web server on port 80 to answer the challenge.
   **Note**: this means port 80 must NOT be in use by anything else
   during renewal. Either stop the backend briefly or set up win-acme
   to use a different port via reverse proxy.
9. Choose to write the cert to `.pem` files at
   `C:\BadBloodRevival\server\certs\` (use win-acme's "PemFiles" plugin)
10. Configure post-renewal script to restart stunnel:
    ```cmd
    net stop stunnel
    net start stunnel
    ```
11. Test renewal manually before relying on it:
    `wacs.exe --test`

win-acme auto-creates a scheduled task that renews the cert before it
expires.

### Open ports at the cloud provider

Most cloud VPS providers have their own firewall layer on top of the OS
firewall. Make sure inbound is open in BOTH:
- The Windows Firewall (already done by setup-public.bat)
- The provider's network security group / firewall / inbound rules

Ports needed: TCP 80 (Let's Encrypt + optional plain HTTP),
TCP 443 (HTTPS for the game), UDP+TCP 47584 (Goldberg matchmaking relay).

## Logs

- **Backend (FastAPI)**: `C:\BadBloodRevival\server\pls-emu.log` —
  also visible live with PowerShell:
  ```powershell
  Get-Content C:\BadBloodRevival\server\pls-emu.log -Wait
  ```
- **stunnel**: typically `C:\Program Files (x86)\stunnel\bin\stunnel.log`
  (path may vary by version)
- **Scheduled task results**:
  ```cmd
  schtasks /Query /TN "BadBloodRevival-Server" /V /FO LIST
  ```

## Service management

| Action | Command |
|--------|---------|
| Start backend manually | `schtasks /Run /TN "BadBloodRevival-Server"` |
| Stop backend manually | `schtasks /End /TN "BadBloodRevival-Server"` |
| Disable backend | `schtasks /Change /TN "BadBloodRevival-Server" /DISABLE` |
| Re-enable backend | `schtasks /Change /TN "BadBloodRevival-Server" /ENABLE` |
| Delete the task | `schtasks /Delete /TN "BadBloodRevival-Server" /F` |
| Start stunnel | `net start stunnel` |
| Stop stunnel | `net stop stunnel` |
| Restart stunnel | `net stop stunnel & net start stunnel` |
| Remove stunnel service | `"C:\Program Files (x86)\stunnel\bin\stunnel.exe" -uninstall` |

## Updates

```cmd
cd C:\BadBloodRevival
git pull

:: Restart services to pick up code changes
schtasks /End /TN "BadBloodRevival-Server"
schtasks /Run /TN "BadBloodRevival-Server"
:: stunnel only needs a restart if you changed stunnel.conf or certs:
:: net stop stunnel & net start stunnel
```

## Backups

The database is at `C:\BadBloodRevival\server\data\dlbb.db`. Back it up
with the `sqlite3` CLI or a scheduled PowerShell task:

```powershell
# Save as C:\BadBloodRevival\backup.ps1
$src = "C:\BadBloodRevival\server\data\dlbb.db"
$dst = "C:\Backups\BadBlood\dlbb-$(Get-Date -Format yyyyMMdd).db"
New-Item -ItemType Directory -Path (Split-Path $dst) -Force | Out-Null
Copy-Item $src $dst
# Keep last 30 days
Get-ChildItem "C:\Backups\BadBlood\dlbb-*.db" |
    Where-Object { $_.LastWriteTime -lt (Get-Date).AddDays(-30) } |
    Remove-Item
```

Schedule with Task Scheduler to run daily.

## Differences vs Linux setup

| Aspect              | Linux (`server-linux/`)              | Windows (`server-windows/`) |
|---------------------|--------------------------------------|------------------------------|
| Service manager     | systemd (`badblood-revival.service`) | Task Scheduler + stunnel service |
| Auto-restart        | systemd `Restart=always`             | Task Scheduler restart count 99 |
| Logs                | `journalctl -u badblood-revival -f`  | `pls-emu.log` + Event Viewer |
| Firewall            | `ufw allow 443`                      | `netsh advfirewall firewall add rule` |
| Cert renewal        | `certbot --standalone`               | `win-acme` (wacs.exe) |
| Install path        | `/opt/badblood-revival/`             | `C:\BadBloodRevival\` |

Functionally equivalent. The Python server code is identical.

## Troubleshooting

### Scheduled task created but doesn't start

Check the task's history in Task Scheduler (`taskschd.msc`):
1. Open Task Scheduler
2. Task Scheduler Library → find `BadBloodRevival-Server`
3. History tab — look for the last run result code

Common causes:
- Python not in SYSTEM's PATH — Use full Python path in the task action,
  or set environment variable via Group Policy
- `server/main.py` not found — verify the path in the task's Action tab
- Port 80 already in use — `netstat -ano | findstr ":80"` to find who

### stunnel service won't start

`sc query stunnel` shows STOPPED. Check Event Viewer (`eventvwr.msc`)
→ Windows Logs → Application — look for "stunnel" entries.

Common causes:
- Cert path in `stunnel.conf` doesn't exist
- Cert file isn't readable by SYSTEM (rare; usually inherited fine)
- Port 443 already in use

### "winget not recognized"

Windows Server typically doesn't ship with winget. Either:
1. Install winget manually from
   <https://github.com/microsoft/winget-cli/releases>, OR
2. Download installers directly:
   - Python: <https://www.python.org/downloads/>
   - stunnel: <https://www.stunnel.org/downloads.html>
   - OpenSSL: <https://slproweb.com/products/Win32OpenSSL.html>

### Help, I prefer NSSM-style service wrapping

If Task Scheduler isn't to your taste, the alternative is [NSSM](https://nssm.cc/)
(Non-Sucking Service Manager). It wraps any executable as a real Windows
service:

```cmd
nssm install BadBloodRevival "C:\Python312\python.exe" "C:\BadBloodRevival\server\main.py"
nssm set BadBloodRevival AppDirectory "C:\BadBloodRevival\server"
nssm set BadBloodRevival Start SERVICE_AUTO_START
nssm start BadBloodRevival
```

It gives you `net start/stop` semantics and finer control over restart
behaviour. Trade-off: extra dependency.
