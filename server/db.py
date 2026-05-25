"""
SQLite database for DLBB Revival.
Portable: single file at data/dlbb.db
"""

import sqlite3
import os
import hashlib
import secrets
import logging

from config import DB_PATH

log = logging.getLogger("pls-emu")

# 30 default items every new player gets (from Techland capture of fresh account)
# Without these the game crashes at menu
DEFAULT_ITEMS = [
    "5b6b1c619a6d803fa489df21", "5b87d667567a6c606cecc4b6",
    "5b6b1a3d9a6d803fa489df01", "5b87eb41567a6c606cecc4ba",
    "5b6b1dc69a6d803fa489df37", "5b80108b567a6c606cecc4b4",
    "5b6b1cc89a6d803fa489df2b", "5b6b1df49a6d803fa489df3b",
    "5b6b1f359a6d803fa489df51", "5b6b1ebe9a6d803fa489df45",
    "5b801042567a6c606cecc4b0", "5b6c0a3c9a6d803fa489df74",
    "5b6b1ffc9a6d803fa489df62", "5b7ac111567a6c606cecc482",
    "5b801059567a6c606cecc4b1", "5b64576a9a6d803fa489deba",
    "5b6b20499a6d803fa489df68", "5b80106f567a6c606cecc4b2",
    "5b6b1fd09a6d803fa489df5e", "5b6966b69a6d803fa489def7",
    "5b801030567a6c606cecc4af", "5b6b1aa49a6d803fa489df0b",
    "5b6b1b0c9a6d803fa489df15", "5b6b209b9a6d803fa489df6f",
    "5b801099567a6c606cecc4b5", "5b6b1f6f9a6d803fa489df56",
    "5b801009567a6c606cecc4ae", "5b6446df9a6d803fa489de6a",
    "5b6446da9a6d803fa489de69", "5b80107d567a6c606cecc4b3",
]

DEFAULT_CONSUMABLES = [
    {"c": 1, "oid": "5c4880b99823db420cf4edf6"},
    {"c": 1, "oid": "5c51aceeb4acd8494c9ac618"},
    {"c": 1, "oid": "5c51acf4b4acd8494c9ac619"},
    {"c": 1, "oid": "5c51acf9b4acd8494c9ac61a"},
    {"c": 1, "oid": "5c51acffb4acd8494c9ac61b"},
]

DEFAULT_SC = 30000
DEFAULT_HC = 0
DEFAULT_ELO = 1000
DEFAULT_LEVEL = 1
DEFAULT_EXP = 0

# SteamID range for our accounts (above normal Steam range to avoid collisions)
SESSION_STEAMID_BASE = 76561199000000000


def get_db() -> sqlite3.Connection:
    os.makedirs(os.path.dirname(DB_PATH) or ".", exist_ok=True)
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA foreign_keys=ON")
    return conn


def init_db():
    conn = get_db()
    conn.executescript("""
        CREATE TABLE IF NOT EXISTS players (
            pls_id      INTEGER PRIMARY KEY AUTOINCREMENT,
            username    TEXT UNIQUE NOT NULL,
            pw_hash     TEXT NOT NULL,
            token       TEXT DEFAULT '',
            session_id  INTEGER DEFAULT 0,
            level       INTEGER DEFAULT 1,
            exp         INTEGER DEFAULT 0,
            elo         INTEGER DEFAULT 1000,
            sc          INTEGER DEFAULT 30000,
            hc          INTEGER DEFAULT 0,
            v           INTEGER DEFAULT 1,
            games_count INTEGER DEFAULT 0,
            game_last   TEXT DEFAULT '',
            tut         INTEGER DEFAULT 0,
            ban         INTEGER DEFAULT 0,
            bantime     INTEGER DEFAULT 0,
            lbc_dn      INTEGER DEFAULT 0,
            lbc_dl      INTEGER DEFAULT 0,
            lbc_de      INTEGER DEFAULT 0,
            created_at  TEXT DEFAULT (datetime('now')),
            last_login  TEXT DEFAULT (datetime('now'))
        );

        CREATE TABLE IF NOT EXISTS player_items (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            pls_id      INTEGER NOT NULL,
            item_uid    TEXT NOT NULL,
            FOREIGN KEY (pls_id) REFERENCES players(pls_id),
            UNIQUE(pls_id, item_uid)
        );

        CREATE TABLE IF NOT EXISTS player_stats (
            pls_id      INTEGER PRIMARY KEY,
            total_score INTEGER DEFAULT 0,
            total_wins  INTEGER DEFAULT 0,
            total_kills INTEGER DEFAULT 0,
            total_zombies INTEGER DEFAULT 0,
            games_played INTEGER DEFAULT 0,
            FOREIGN KEY (pls_id) REFERENCES players(pls_id)
        );

        CREATE TABLE IF NOT EXISTS player_consumables (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            pls_id      INTEGER NOT NULL,
            oid         TEXT NOT NULL,
            count       INTEGER DEFAULT 1,
            FOREIGN KEY (pls_id) REFERENCES players(pls_id),
            UNIQUE(pls_id, oid)
        );

        -- Friend list system
        CREATE TABLE IF NOT EXISTS friends (
            pls_id_a    INTEGER NOT NULL,
            pls_id_b    INTEGER NOT NULL,
            created_at  INTEGER NOT NULL,
            PRIMARY KEY (pls_id_a, pls_id_b),
            CHECK (pls_id_a < pls_id_b)
        );
        CREATE INDEX IF NOT EXISTS idx_friends_a ON friends(pls_id_a);
        CREATE INDEX IF NOT EXISTS idx_friends_b ON friends(pls_id_b);

        CREATE TABLE IF NOT EXISTS friend_requests (
            from_id     INTEGER NOT NULL,
            to_id       INTEGER NOT NULL,
            created_at  INTEGER NOT NULL,
            PRIMARY KEY (from_id, to_id)
        );
        CREATE INDEX IF NOT EXISTS idx_freq_to ON friend_requests(to_id);

        CREATE TABLE IF NOT EXISTS player_sessions (
            pls_id          INTEGER PRIMARY KEY,
            last_heartbeat  INTEGER NOT NULL,
            state           TEXT NOT NULL DEFAULT 'online'  -- 'online' | 'ingame'
        );

        CREATE TABLE IF NOT EXISTS lobby_invites (
            from_id     INTEGER NOT NULL,
            to_id       INTEGER NOT NULL,
            lobby_id    TEXT NOT NULL,
            created_at  INTEGER NOT NULL,
            consumed    INTEGER DEFAULT 0,
            PRIMARY KEY (from_id, to_id)
        );
        CREATE INDEX IF NOT EXISTS idx_invites_to ON lobby_invites(to_id);
    """)
    conn.commit()
    conn.close()
    log.info("Database initialized at %s", DB_PATH)


# ============================================================
# Friend list helpers
# ============================================================

import time as _time

# Heartbeat is considered fresh if seen in the last 30 seconds
HEARTBEAT_TIMEOUT = 30


def _now() -> int:
    return int(_time.time())


def _pair(a: int, b: int):
    """Return tuple sorted ascending — friend pairs are stored canonical."""
    return (a, b) if a < b else (b, a)


def heartbeat(pls_id: int, state: str) -> None:
    """Mark a player as alive. State is 'online' or 'ingame'.
    Also cleans up stale sessions older than 60 seconds."""
    if state not in ("online", "ingame"):
        state = "online"
    conn = get_db()
    now = _now()
    conn.execute(
        "INSERT INTO player_sessions(pls_id, last_heartbeat, state) VALUES (?, ?, ?) "
        "ON CONFLICT(pls_id) DO UPDATE SET last_heartbeat=excluded.last_heartbeat, state=excluded.state",
        (pls_id, now, state),
    )
    conn.execute("DELETE FROM player_sessions WHERE last_heartbeat < ?", (now - 60,))
    conn.commit()
    conn.close()


def _get_status(conn, pls_id: int) -> str:
    """Returns 'online', 'ingame', or 'offline'."""
    now = _now()
    row = conn.execute(
        "SELECT state, last_heartbeat FROM player_sessions WHERE pls_id = ?",
        (pls_id,),
    ).fetchone()
    if not row:
        return "offline"
    if now - row["last_heartbeat"] > HEARTBEAT_TIMEOUT:
        return "offline"
    return row["state"] or "online"


def list_friends(pls_id: int) -> list:
    """Return all friends of pls_id with their current status and nick."""
    conn = get_db()
    rows = conn.execute(
        """
        SELECT p.pls_id, p.username
        FROM friends f
        JOIN players p ON p.pls_id = (CASE WHEN f.pls_id_a = ? THEN f.pls_id_b ELSE f.pls_id_a END)
        WHERE f.pls_id_a = ? OR f.pls_id_b = ?
        """,
        (pls_id, pls_id, pls_id),
    ).fetchall()
    out = []
    for r in rows:
        out.append({
            "pls_id": r["pls_id"],
            "nick": r["username"],
            "status": _get_status(conn, r["pls_id"]),
        })
    conn.close()
    return out


def list_requests(pls_id: int) -> dict:
    """Return incoming and outgoing friend requests for pls_id."""
    conn = get_db()
    incoming_rows = conn.execute(
        "SELECT fr.from_id AS pls_id, p.username AS nick "
        "FROM friend_requests fr JOIN players p ON p.pls_id = fr.from_id "
        "WHERE fr.to_id = ? ORDER BY fr.created_at DESC",
        (pls_id,),
    ).fetchall()
    outgoing_rows = conn.execute(
        "SELECT fr.to_id AS pls_id, p.username AS nick "
        "FROM friend_requests fr JOIN players p ON p.pls_id = fr.to_id "
        "WHERE fr.from_id = ? ORDER BY fr.created_at DESC",
        (pls_id,),
    ).fetchall()
    conn.close()
    return {
        "incoming": [{"pls_id": r["pls_id"], "nick": r["nick"]} for r in incoming_rows],
        "outgoing": [{"pls_id": r["pls_id"], "nick": r["nick"]} for r in outgoing_rows],
    }


def add_friend_request(from_id: int, target_nick: str) -> dict:
    """Send a friend request from from_id to player by nick.
    If reciprocal request already exists, becomes friendship instantly."""
    conn = get_db()

    # Resolve nick -> pls_id
    target = conn.execute(
        "SELECT pls_id FROM players WHERE username = ? COLLATE NOCASE",
        (target_nick,),
    ).fetchone()
    if not target:
        conn.close()
        return {"ok": False, "error": "Player not found"}
    to_id = target["pls_id"]

    if to_id == from_id:
        conn.close()
        return {"ok": False, "error": "You cannot add yourself"}

    # Already friends?
    a, b = _pair(from_id, to_id)
    if conn.execute(
        "SELECT 1 FROM friends WHERE pls_id_a = ? AND pls_id_b = ?", (a, b)
    ).fetchone():
        conn.close()
        return {"ok": False, "error": "Already friends"}

    # Reciprocal request? -> create friendship
    reciprocal = conn.execute(
        "SELECT 1 FROM friend_requests WHERE from_id = ? AND to_id = ?",
        (to_id, from_id),
    ).fetchone()
    if reciprocal:
        conn.execute(
            "INSERT INTO friends(pls_id_a, pls_id_b, created_at) VALUES (?, ?, ?)",
            (a, b, _now()),
        )
        conn.execute(
            "DELETE FROM friend_requests WHERE (from_id = ? AND to_id = ?) OR (from_id = ? AND to_id = ?)",
            (from_id, to_id, to_id, from_id),
        )
        conn.commit()
        conn.close()
        return {"ok": True, "friend": True}

    # Already requested?
    existing = conn.execute(
        "SELECT 1 FROM friend_requests WHERE from_id = ? AND to_id = ?",
        (from_id, to_id),
    ).fetchone()
    if existing:
        conn.close()
        return {"ok": False, "error": "Request already sent"}

    conn.execute(
        "INSERT INTO friend_requests(from_id, to_id, created_at) VALUES (?, ?, ?)",
        (from_id, to_id, _now()),
    )
    conn.commit()
    conn.close()
    return {"ok": True, "friend": False}


def accept_friend_request(pls_id: int, from_id: int) -> dict:
    """Accept incoming request: pls_id accepts request from from_id."""
    conn = get_db()
    row = conn.execute(
        "SELECT 1 FROM friend_requests WHERE from_id = ? AND to_id = ?",
        (from_id, pls_id),
    ).fetchone()
    if not row:
        conn.close()
        return {"ok": False, "error": "Request not found"}

    a, b = _pair(pls_id, from_id)
    conn.execute(
        "INSERT OR IGNORE INTO friends(pls_id_a, pls_id_b, created_at) VALUES (?, ?, ?)",
        (a, b, _now()),
    )
    conn.execute(
        "DELETE FROM friend_requests WHERE from_id = ? AND to_id = ?",
        (from_id, pls_id),
    )
    conn.commit()
    conn.close()
    return {"ok": True}


def decline_friend_request(pls_id: int, from_id: int) -> dict:
    conn = get_db()
    conn.execute(
        "DELETE FROM friend_requests WHERE from_id = ? AND to_id = ?",
        (from_id, pls_id),
    )
    conn.commit()
    conn.close()
    return {"ok": True}


def cancel_friend_request(pls_id: int, to_id: int) -> dict:
    conn = get_db()
    conn.execute(
        "DELETE FROM friend_requests WHERE from_id = ? AND to_id = ?",
        (pls_id, to_id),
    )
    conn.commit()
    conn.close()
    return {"ok": True}


def remove_friend(pls_id: int, other_id: int) -> dict:
    conn = get_db()
    a, b = _pair(pls_id, other_id)
    conn.execute(
        "DELETE FROM friends WHERE pls_id_a = ? AND pls_id_b = ?", (a, b)
    )
    conn.commit()
    conn.close()
    return {"ok": True}


def create_lobby_invite(from_id: int, to_id: int, lobby_id: str) -> dict:
    """Send a lobby invite from from_id to to_id. Requires existing friendship."""
    conn = get_db()
    a, b = _pair(from_id, to_id)
    friendship = conn.execute(
        "SELECT 1 FROM friends WHERE pls_id_a = ? AND pls_id_b = ?", (a, b)
    ).fetchone()
    if not friendship:
        conn.close()
        return {"ok": False, "error": "Not friends"}
    conn.execute(
        "INSERT INTO lobby_invites(from_id, to_id, lobby_id, created_at, consumed) "
        "VALUES (?, ?, ?, ?, 0) "
        "ON CONFLICT(from_id, to_id) DO UPDATE SET lobby_id=excluded.lobby_id, "
        "created_at=excluded.created_at, consumed=0",
        (from_id, to_id, lobby_id, _now()),
    )
    conn.commit()
    conn.close()
    return {"ok": True}


def poll_lobby_invites(pls_id: int) -> list:
    """Return non-consumed invites for pls_id and mark them consumed.
    Drops invites older than 5 minutes."""
    conn = get_db()
    now = _now()
    conn.execute("DELETE FROM lobby_invites WHERE created_at < ?", (now - 300,))
    rows = conn.execute(
        "SELECT li.from_id, li.lobby_id, p.username AS from_nick "
        "FROM lobby_invites li JOIN players p ON p.pls_id = li.from_id "
        "WHERE li.to_id = ? AND li.consumed = 0",
        (pls_id,),
    ).fetchall()
    if rows:
        conn.execute(
            "UPDATE lobby_invites SET consumed = 1 WHERE to_id = ? AND consumed = 0",
            (pls_id,),
        )
        conn.commit()
    conn.close()
    return [
        {"from_id": r["from_id"], "from_nick": r["from_nick"], "lobby_id": r["lobby_id"]}
        for r in rows
    ]


def _hash_password(password: str) -> str:
    return hashlib.sha256(password.encode()).hexdigest()


# ---- Launcher auth (username + password) ----

def register(username: str, password: str) -> dict:
    conn = get_db()
    existing = conn.execute("SELECT 1 FROM players WHERE username = ?",
                            (username,)).fetchone()
    if existing:
        conn.close()
        return {"error": "Username already exists"}

    pw_hash = _hash_password(password)
    cur = conn.execute(
        "INSERT INTO players (username, pw_hash, sc, hc, elo, level, exp) VALUES (?, ?, ?, ?, ?, ?, ?)",
        (username, pw_hash, DEFAULT_SC, DEFAULT_HC, DEFAULT_ELO, DEFAULT_LEVEL, DEFAULT_EXP))
    pls_id = cur.lastrowid

    for uid in DEFAULT_ITEMS:
        conn.execute("INSERT OR IGNORE INTO player_items (pls_id, item_uid) VALUES (?, ?)",
                     (pls_id, uid))
    for cons in DEFAULT_CONSUMABLES:
        conn.execute(
            "INSERT OR IGNORE INTO player_consumables (pls_id, oid, count) VALUES (?, ?, ?)",
            (pls_id, cons["oid"], cons["c"]))

    conn.commit()
    conn.close()
    log.info("REGISTER: %s → pls_id=%d", username, pls_id)
    return {"pls_id": pls_id, "username": username}


def launcher_login(username: str, password: str) -> dict:
    conn = get_db()
    row = conn.execute("SELECT pls_id, pw_hash FROM players WHERE username = ?",
                       (username,)).fetchone()
    if not row:
        conn.close()
        return {"error": "User not found"}

    if row["pw_hash"] != _hash_password(password):
        conn.close()
        return {"error": "Wrong password"}

    pls_id = row["pls_id"]
    # Generate a unique session_id (used as fake SteamID)
    session_id = SESSION_STEAMID_BASE + pls_id
    token = secrets.token_hex(12)

    conn.execute("UPDATE players SET token = ?, session_id = ?, last_login = datetime('now') WHERE pls_id = ?",
                 (token, session_id, pls_id))
    conn.commit()
    conn.close()

    log.info("LOGIN: %s → pls_id=%d session_id=%d", username, pls_id, session_id)
    return {"pls_id": pls_id, "username": username, "session_id": session_id, "token": token}


# ---- Game auth (called when game sends Steam ticket) ----

def game_auth(steam_id_from_ticket: str) -> tuple[int, str]:
    """Game sends a ticket with a SteamID. If it matches a session_id, use that player."""
    conn = get_db()

    # Try to find by session_id first (launcher flow)
    row = conn.execute("SELECT pls_id, token FROM players WHERE session_id = ?",
                       (int(steam_id_from_ticket),)).fetchone()
    if row:
        pls_id = row["pls_id"]
        token = row["token"]
        conn.close()
        return pls_id, token

    # Fallback: legacy flow (direct SteamID, for testing)
    row = conn.execute("SELECT pls_id, token FROM players WHERE username = ?",
                       (steam_id_from_ticket,)).fetchone()
    if row:
        pls_id = row["pls_id"]
        token = row["token"] or secrets.token_hex(12)
        conn.execute("UPDATE players SET token = ? WHERE pls_id = ?", (token, pls_id))
        conn.commit()
        conn.close()
        return pls_id, token

    # Auto-create for backwards compatibility (no launcher)
    pw_hash = _hash_password("default")
    cur = conn.execute(
        "INSERT INTO players (username, pw_hash, sc, hc) VALUES (?, ?, ?, ?)",
        (steam_id_from_ticket, pw_hash, DEFAULT_SC, DEFAULT_HC))
    pls_id = cur.lastrowid
    for uid in DEFAULT_ITEMS:
        conn.execute("INSERT OR IGNORE INTO player_items (pls_id, item_uid) VALUES (?, ?)",
                     (pls_id, uid))
    for cons in DEFAULT_CONSUMABLES:
        conn.execute(
            "INSERT OR IGNORE INTO player_consumables (pls_id, oid, count) VALUES (?, ?, ?)",
            (pls_id, cons["oid"], cons["c"]))

    token = secrets.token_hex(12)
    conn.execute("UPDATE players SET token = ? WHERE pls_id = ?", (token, pls_id))
    conn.commit()
    conn.close()
    log.info("AUTO-CREATE: %s → pls_id=%d", steam_id_from_ticket, pls_id)
    return pls_id, token


# ---- Existing functions ----

def get_player_by_token(token: str) -> dict | None:
    conn = get_db()
    row = conn.execute("SELECT * FROM players WHERE token = ?", (token,)).fetchone()
    conn.close()
    return dict(row) if row else None


def get_playerdata(pls_id: int) -> dict:
    conn = get_db()
    p = conn.execute("SELECT * FROM players WHERE pls_id = ?", (pls_id,)).fetchone()
    if not p:
        conn.close()
        return {}

    items = [r["item_uid"] for r in
             conn.execute("SELECT item_uid FROM player_items WHERE pls_id = ?",
                          (pls_id,)).fetchall()]

    consumables = [{"c": r["count"], "id": {"$oid": r["oid"]}} for r in
                   conn.execute("SELECT oid, count FROM player_consumables WHERE pls_id = ?",
                                (pls_id,)).fetchall()]

    # Build stats array from player_stats table
    # game_stats layout: [0:games, 1:kills, 2:?, 3:?, 4:zombies, 5:?, 6:wins, 7:score, ...]
    stats_row = conn.execute(
        "SELECT * FROM player_stats WHERE pls_id = ?", (pls_id,)).fetchone()
    if stats_row:
        stats = [
            stats_row["games_played"],   # 0
            stats_row["total_kills"],    # 1
            0,                           # 2
            0,                           # 3
            stats_row["total_zombies"],  # 4
            0,                           # 5
            stats_row["total_wins"],     # 6
            stats_row["total_score"],    # 7
            0, 0, 0, 0, 0, 0,           # 8-13
        ]
    else:
        stats = []

    conn.close()

    return {
        "pls_result": 0,
        "pls_desc": "",
        "games_count": p["games_count"],
        "v": p["v"],
        "stats": stats,
        "elo": p["elo"],
        "level": p["level"],
        "items": items,
        "consumables": consumables,
        "tut": p["tut"],
        "events": [],
        "lbc": {"dn": p["lbc_dn"], "dl": p["lbc_dl"], "de": p["lbc_de"]},
        "game_last": p["game_last"],
        "exp": p["exp"],
        "sc": p["sc"],
        "hc": p["hc"],
    }


def purchase_item(pls_id: int, item_uid: str, sc_cost: int, hc_cost: int) -> dict | None:
    conn = get_db()
    p = conn.execute("SELECT sc, hc, v FROM players WHERE pls_id = ?",
                     (pls_id,)).fetchone()
    if not p:
        conn.close()
        return None

    existing = conn.execute(
        "SELECT 1 FROM player_items WHERE pls_id = ? AND item_uid = ?",
        (pls_id, item_uid)).fetchone()
    if existing:
        conn.close()
        return {"v": p["v"], "pls_result": 0, "pls_desc": ""}

    if p["sc"] < sc_cost or p["hc"] < hc_cost:
        conn.close()
        return {"v": p["v"], "pls_result": 1, "pls_desc": "Insufficient funds"}

    new_sc = p["sc"] - sc_cost
    new_hc = p["hc"] - hc_cost
    new_v = p["v"] + 1

    conn.execute("INSERT INTO player_items (pls_id, item_uid) VALUES (?, ?)",
                 (pls_id, item_uid))
    conn.execute("UPDATE players SET sc = ?, hc = ?, v = ? WHERE pls_id = ?",
                 (new_sc, new_hc, new_v, pls_id))
    conn.commit()
    conn.close()

    log.info("PURCHASE: pls_id=%d item=%s sc_cost=%d hc_cost=%d → sc=%d hc=%d v=%d",
             pls_id, item_uid, sc_cost, hc_cost, new_sc, new_hc, new_v)

    return {"v": new_v, "pls_result": 0, "pls_desc": ""}


def add_credits(pls_id: int, sc: int, hc: int):
    conn = get_db()
    conn.execute("UPDATE players SET sc = sc + ?, hc = hc + ? WHERE pls_id = ?",
                 (sc, hc, pls_id))
    conn.commit()
    conn.close()


def update_stats(pls_id: int, game_stats: list):
    """Update player stats from game results.
    game_stats array from client: [?, kills, ?, ?, zombies, ?, ?, score, ?, ?, ?, ?, ?, ?]
    """
    conn = get_db()
    # Ensure stats row exists
    conn.execute("INSERT OR IGNORE INTO player_stats (pls_id) VALUES (?)", (pls_id,))

    kills = game_stats[1] if len(game_stats) > 1 else 0
    zombies = game_stats[4] if len(game_stats) > 4 else 0
    score = game_stats[7] if len(game_stats) > 7 else 0

    # Check if this player won (position 0 or extracted)
    # For now, count every completed game. Wins need more analysis.
    conn.execute("""
        UPDATE player_stats SET
            total_score = total_score + ?,
            total_kills = total_kills + ?,
            total_zombies = total_zombies + ?,
            games_played = games_played + 1
        WHERE pls_id = ?
    """, (score, kills, zombies, pls_id))
    conn.commit()
    conn.close()


def get_leaderboard(category: int, limit: int = 30) -> list:
    """Get leaderboard data for a category.
    Categories: 0=Game Score(m), 1=Wins(w), 2=Kills(k), 3=Zombies(z)
    """
    conn = get_db()

    col_map = {0: "total_score", 1: "total_wins", 2: "total_kills", 3: "total_zombies"}
    field_map = {0: "m", 1: "w", 2: "k", 3: "z"}
    rank_map = {0: "ml", 1: "wl", 2: "kl", 3: "zl"}
    idx_map = {0: "mi", 1: "wi", 2: "ki", 3: "zi"}

    col = col_map.get(category, "total_score")
    field = field_map.get(category, "m")
    rank_key = rank_map.get(category, "ml")
    idx_key = idx_map.get(category, "mi")

    rows = conn.execute(f"""
        SELECT p.pls_id, p.username, p.level, s.{col} as val
        FROM player_stats s
        JOIN players p ON p.pls_id = s.pls_id
        WHERE s.{col} > 0
        ORDER BY s.{col} DESC
        LIMIT ?
    """, (limit,)).fetchall()
    conn.close()

    players = []
    for i, r in enumerate(rows):
        players.append({
            "plr": {"a": r["pls_id"], "l": r["level"]},
            "usr": r["username"],
            "_id": {"$oid": f"{'0' * 18}{r['pls_id']:06d}"},
            rank_key: i,
            idx_key: i,
            field: r["val"],
        })

    return players
