"""
Server configuration — BadBlood-Revival

All settings can be overridden via environment variables (see .env.example).
Defaults are tuned for a local Windows run (`python main.py`).
"""

import os
from pathlib import Path

# ---------------------------------------------------------------------------
# Networking
# ---------------------------------------------------------------------------

# Interface to bind to. 0.0.0.0 = all interfaces, 127.0.0.1 = localhost only.
HOST = os.getenv("DLBB_HOST", "0.0.0.0")

# HTTPS port (used only if you terminate TLS in Python itself; usually stunnel handles 443).
PORT = int(os.getenv("DLBB_PORT", "443"))

# Plain HTTP port the game will hit AFTER stunnel decrypts the request.
HTTP_PORT = int(os.getenv("DLBB_HTTP_PORT", "80"))

# Public hostname the game connects to. For LOCAL play this stays as
# 'pls.dlbb.com' (the original Techland hostname, routed via hosts file to
# 127.0.0.1). For internet hosting set this to your own 12-char domain and
# pass the same value to the patcher's --server-host.
DOMAIN = os.getenv("DLBB_DOMAIN", "pls.dlbb.com")

# Goldberg P2P matchmaking relay port (UDP+TCP). Used to traverse CGNAT.
RELAY_PORT = int(os.getenv("DLBB_RELAY_PORT", "47584"))


# ---------------------------------------------------------------------------
# TLS — only if you run uvicorn with --ssl directly. The default deployment
# uses stunnel for HTTPS, so these are normally unused.
# ---------------------------------------------------------------------------

USE_TLS = os.getenv("DLBB_USE_TLS", "0") == "1"
TLS_CERT = os.getenv("DLBB_TLS_CERT", "certs/cert.pem")
TLS_KEY = os.getenv("DLBB_TLS_KEY", "certs/key.pem")


# ---------------------------------------------------------------------------
# Storage
# ---------------------------------------------------------------------------

# Database file. Relative to server/ directory by default.
DB_PATH = os.getenv("DLBB_DB_PATH", "data/dlbb.db")

# Patch zip directory served by /api/patch (optional, can be absent).
# Points to a folder containing your patched client files; the server zips it
# on demand for the launcher. Leave empty/missing to disable the patch endpoint.
PATCH_DIR = os.getenv("DLBB_PATCH_DIR", "")


# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

LOG_LEVEL = os.getenv("DLBB_LOG_LEVEL", "INFO")
LOG_ALL_REQUESTS = os.getenv("DLBB_LOG_REQUESTS", "1") == "1"
LOG_FILE = os.getenv("DLBB_LOG_FILE", "pls-emu.log")


# ---------------------------------------------------------------------------
# Game logic toggles
# ---------------------------------------------------------------------------

# Feature bitmask sent in /dlbb/gameconfig. 3071 = all features on, matches
# what the live Techland server returned in 2017-2018 captures.
ENABLED_FEATURES = int(os.getenv("DLBB_ENABLED_FEATURES", "3071"))

# Starting balance for new accounts.
DEFAULT_SC = int(os.getenv("DLBB_DEFAULT_SC", "30000"))
DEFAULT_HC = int(os.getenv("DLBB_DEFAULT_HC", "0"))
