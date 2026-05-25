"""
BadBlood-Revival — PLS Server Emulator

Re-implementation of the proprietary Techland PLS (Profile/Login/Shop) backend
for Dying Light: Bad Blood. Response shape was reverse-engineered by proxying
the live Techland server before its shutdown.

This emulator handles: auth (Steam ticket), profile + inventory, shop catalog
+ purchases, leaderboards (partial), gameconfig, telemetry sink, friends list,
lobby invites. P2P matchmaking is handled by Goldberg Steam Emu — this server
provides only the relay (UDP+TCP on :47584) so peers behind CGNAT can find
each other.

See https://github.com/ThePie88/BadBlood-Revival for project info.
"""

import logging
import json
import os
import struct
from datetime import datetime, timezone
from contextlib import asynccontextmanager
from urllib.parse import parse_qs

from fastapi import FastAPI, Request, Response
from fastapi.responses import JSONResponse

import config
import db

FIXTURES_DIR = os.path.join(os.path.dirname(__file__), "fixtures")
LOG_FILE = os.path.join(os.path.dirname(__file__), "pls-emu.log")

logging.basicConfig(
    level=getattr(logging, config.LOG_LEVEL),
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[
        logging.StreamHandler(),
        logging.FileHandler(LOG_FILE, encoding="utf-8"),
    ],
)
log = logging.getLogger("pls-emu")


def load_fixture(name):
    with open(os.path.join(FIXTURES_DIR, name), encoding="utf-8") as f:
        return json.load(f)


# ===========================================================================
# Goldberg Steam Emu Relay (UDP + TCP on :47584)
# ===========================================================================
# Forwards Steam P2P traffic between clients on different NATs.
# Started automatically with the FastAPI app, runs in background daemon threads.

import socket as _socket
import threading as _threading
import time as _time

GOLDBERG_RELAY_PORT = 47584

# UDP relay state
_udp_clients = {}  # addr -> last_seen timestamp
_udp_lock = _threading.Lock()

# TCP relay state
_tcp_waiting = []  # list of (sock, addr, ts)
_tcp_lock = _threading.Lock()


def _udp_relay_loop():
    sock = _socket.socket(_socket.AF_INET, _socket.SOCK_DGRAM)
    try:
        sock.bind(("0.0.0.0", GOLDBERG_RELAY_PORT))
    except OSError as e:
        log.error(f"[goldberg-udp] bind failed on {GOLDBERG_RELAY_PORT}: {e}")
        return
    log.info(f"[goldberg-udp] relay listening on :{GOLDBERG_RELAY_PORT}")

    while True:
        try:
            data, addr = sock.recvfrom(8192)
            now = _time.time()
            with _udp_lock:
                new_client = addr not in _udp_clients
                _udp_clients[addr] = now
                # Cleanup old (>60s)
                stale = [c for c, t in _udp_clients.items() if now - t > 60]
                for c in stale:
                    del _udp_clients[c]
                others = [c for c in _udp_clients if c != addr]
            if new_client:
                log.info(f"[goldberg-udp] new client {addr}")
            for c in others:
                try:
                    sock.sendto(data, c)
                except Exception:
                    pass
        except Exception as e:
            log.error(f"[goldberg-udp] {e}")


def _tcp_pipe(src, dst, label):
    try:
        while True:
            data = src.recv(8192)
            if not data:
                break
            dst.sendall(data)
    except Exception:
        pass
    try: src.close()
    except Exception: pass
    try: dst.close()
    except Exception: pass


def _tcp_handle(client, addr):
    log.info(f"[goldberg-tcp] connection from {addr}")
    waiting = None
    with _tcp_lock:
        # Cleanup stale waiters (>30s)
        now = _time.time()
        _tcp_waiting[:] = [w for w in _tcp_waiting if now - w[2] < 30]
        if _tcp_waiting:
            waiting = _tcp_waiting.pop(0)
    if waiting:
        other_sock, other_addr, _ts = waiting
        log.info(f"[goldberg-tcp] pair {addr} <-> {other_addr}")
        _threading.Thread(target=_tcp_pipe, args=(client, other_sock, "fwd"), daemon=True).start()
        _threading.Thread(target=_tcp_pipe, args=(other_sock, client, "rev"), daemon=True).start()
    else:
        with _tcp_lock:
            _tcp_waiting.append((client, addr, _time.time()))
        log.info(f"[goldberg-tcp] {addr} waiting (queue={len(_tcp_waiting)})")


def _tcp_relay_loop():
    sock = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
    sock.setsockopt(_socket.SOL_SOCKET, _socket.SO_REUSEADDR, 1)
    try:
        sock.bind(("0.0.0.0", GOLDBERG_RELAY_PORT))
    except OSError as e:
        log.error(f"[goldberg-tcp] bind failed on {GOLDBERG_RELAY_PORT}: {e}")
        return
    sock.listen(10)
    log.info(f"[goldberg-tcp] relay listening on :{GOLDBERG_RELAY_PORT}")
    while True:
        try:
            client, addr = sock.accept()
            _threading.Thread(target=_tcp_handle, args=(client, addr), daemon=True).start()
        except Exception as e:
            log.error(f"[goldberg-tcp] {e}")


def _start_goldberg_relay():
    _threading.Thread(target=_udp_relay_loop, daemon=True).start()
    _threading.Thread(target=_tcp_relay_loop, daemon=True).start()


@asynccontextmanager
async def lifespan(app: FastAPI):
    db.init_db()
    _start_goldberg_relay()
    log.info("PLS Emulator running — Created by MrPie")
    yield


app = FastAPI(title="DLBB PLS Emulator", lifespan=lifespan)

ENABLED_FEATURES = 3071

# Dynamic test mode - reads from a file to change responses without restart
import pathlib
TEST_FLAG_FILE = pathlib.Path(__file__).parent / "test_flag.txt"


def get_pls_id(request: Request) -> int | None:
    """Extract pls_id from PLS-Authorization header."""
    auth = request.headers.get("pls-authorization", "")
    if auth.startswith("Token "):
        token = auth[6:]
        player = db.get_player_by_token(token)
        if player:
            return player["pls_id"]
    return None


@app.middleware("http")
async def log_requests(request: Request, call_next):
    body = await request.body()
    log.info(">>> %s %s | Body: %s",
             request.method, request.url.path,
             body.decode("utf-8", errors="replace")[:300] if body else "(empty)")
    response = await call_next(request)
    log.info("<<< %s %s -> %d", request.method, request.url.path, response.status_code)
    return response


# ===========================================================================
# LAUNCHER API (register + login → returns session_id for Goldberg)
# ===========================================================================

@app.post("/api/register")
async def api_register(request: Request):
    data = await request.json()
    result = db.register(data.get("username", ""), data.get("password", ""))
    return JSONResponse(result)


@app.post("/api/login")
async def api_login(request: Request):
    data = await request.json()
    result = db.launcher_login(data.get("username", ""), data.get("password", ""))
    return JSONResponse(result)


# ===========================================================================
# FRIENDS API
# ===========================================================================

def _require_auth(request: Request):
    pls_id = get_pls_id(request)
    if not pls_id:
        return None, JSONResponse({"error": "Not authenticated"}, status_code=401)
    return pls_id, None


@app.post("/api/friends/heartbeat")
async def api_friends_heartbeat(request: Request):
    pls_id, err = _require_auth(request)
    if err: return err
    try:
        data = await request.json()
    except Exception:
        data = {}
    state = data.get("state", "online")
    db.heartbeat(pls_id, state)
    return JSONResponse({"ok": True})


@app.get("/api/friends/list")
async def api_friends_list(request: Request):
    pls_id, err = _require_auth(request)
    if err: return err
    return JSONResponse({"friends": db.list_friends(pls_id)})


@app.get("/api/friends/requests")
async def api_friends_requests(request: Request):
    pls_id, err = _require_auth(request)
    if err: return err
    return JSONResponse(db.list_requests(pls_id))


@app.post("/api/friends/add")
async def api_friends_add(request: Request):
    pls_id, err = _require_auth(request)
    if err: return err
    data = await request.json()
    nick = data.get("nick", "").strip()
    if not nick:
        return JSONResponse({"ok": False, "error": "Empty nick"})
    return JSONResponse(db.add_friend_request(pls_id, nick))


@app.post("/api/friends/accept")
async def api_friends_accept(request: Request):
    pls_id, err = _require_auth(request)
    if err: return err
    data = await request.json()
    from_id = int(data.get("from_id", 0))
    if not from_id:
        return JSONResponse({"ok": False, "error": "Missing from_id"})
    return JSONResponse(db.accept_friend_request(pls_id, from_id))


@app.post("/api/friends/decline")
async def api_friends_decline(request: Request):
    pls_id, err = _require_auth(request)
    if err: return err
    data = await request.json()
    from_id = int(data.get("from_id", 0))
    return JSONResponse(db.decline_friend_request(pls_id, from_id))


@app.post("/api/friends/cancel")
async def api_friends_cancel(request: Request):
    pls_id, err = _require_auth(request)
    if err: return err
    data = await request.json()
    to_id = int(data.get("to_id", 0))
    return JSONResponse(db.cancel_friend_request(pls_id, to_id))


@app.post("/api/friends/remove")
async def api_friends_remove(request: Request):
    pls_id, err = _require_auth(request)
    if err: return err
    data = await request.json()
    other_id = int(data.get("pls_id", 0))
    return JSONResponse(db.remove_friend(pls_id, other_id))


@app.post("/api/lobby/invite")
async def api_lobby_invite(request: Request):
    pls_id, err = _require_auth(request)
    if err: return err
    data = await request.json()
    to_id = int(data.get("to_id", 0))
    lobby_id = data.get("lobby_id", "").strip()
    if not to_id or not lobby_id:
        return JSONResponse({"ok": False, "error": "Missing to_id or lobby_id"})
    return JSONResponse(db.create_lobby_invite(pls_id, to_id, lobby_id))


@app.get("/api/lobby/invites/poll")
async def api_lobby_invites_poll(request: Request):
    pls_id, err = _require_auth(request)
    if err: return err
    return JSONResponse({"invites": db.poll_lobby_invites(pls_id)})


# ===========================================================================
# PATCH API (launcher downloads patch from server)
# ===========================================================================

# Patch dir lookup: try sibling first (production layout: /opt/pie-server/PATCH-PIE),
# then parent (dev layout: repo_root/PATCH-PIE next to repo_root/Server/).
_here = os.path.dirname(os.path.abspath(__file__))
PATCH_DIR = os.path.join(_here, "PATCH-PIE")
if not os.path.isdir(PATCH_DIR):
    PATCH_DIR = os.path.join(_here, "..", "PATCH-PIE")
PATCH_VERSION = "0.4.1"


@app.api_route("/api/version", methods=["GET", "POST"])
async def api_version():
    """Returns current patch version."""
    return JSONResponse({"version": PATCH_VERSION})


@app.get("/api/patch")
async def api_patch():
    """Serves the patch as a zip file. PATCH-PIE folder gets zipped on the fly."""
    import zipfile
    import io

    if not os.path.isdir(PATCH_DIR):
        return JSONResponse({"error": "Patch not available"}, status_code=404)

    # Create zip in memory
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED) as zf:
        for root, dirs, files in os.walk(PATCH_DIR):
            for fname in files:
                fpath = os.path.join(root, fname)
                arcname = os.path.relpath(fpath, PATCH_DIR)
                zf.write(fpath, arcname)

    buf.seek(0)
    log.info(f"Serving patch v{PATCH_VERSION} ({buf.getbuffer().nbytes} bytes)")
    return Response(
        content=buf.read(),
        media_type="application/zip",
        headers={
            "Content-Disposition": f"attachment; filename=patch_{PATCH_VERSION}.zip",
            "X-Patch-Version": PATCH_VERSION,
        }
    )


# ===========================================================================
# AUTH (game calls this with Steam ticket — session_id is inside the ticket)
# ===========================================================================

@app.post("/auth/login/steam/")
async def auth_login_steam(request: Request):
    body = await request.body()
    params = parse_qs(body.decode("utf-8", errors="replace"))
    ticket = params.get("auth_session_ticket", [""])[0]

    steam_id_int = 76561198000000001
    if ticket and len(ticket) >= 40:
        try:
            ticket_bytes = bytes.fromhex(ticket)
            if len(ticket_bytes) >= 20:
                steam_id_int = struct.unpack_from("<Q", ticket_bytes, 12)[0]
        except (ValueError, struct.error):
            pass

    pls_id, token = db.game_auth(str(steam_id_int))
    log.info("AUTH: steam_id=%s pls_id=%d token=%s", steam_id_int, pls_id, token)

    return JSONResponse({"token": token, "enabled": ENABLED_FEATURES})


@app.api_route("/auth/logout/", methods=["POST", "GET"])
async def auth_logout():
    return JSONResponse({"pls_result": 0, "pls_desc": ""})


# ===========================================================================
# GAMECONFIG (static)
# ===========================================================================

@app.get("/dlbb/gameconfig")
async def get_gameconfig():
    # Dynamic test: read overrides from test_flag.txt
    pls_result = 0
    enabled = ENABLED_FEATURES
    try:
        if TEST_FLAG_FILE.exists():
            parts = TEST_FLAG_FILE.read_text().strip().split(",")
            if len(parts) >= 2:
                enabled = int(parts[0])
                pls_result = int(parts[1])
                log.info("TEST: enabled=%d pls_result=%d", enabled, pls_result)
    except: pass
    return JSONResponse({
        "pls_result": pls_result,
        "enabled": enabled,
        "exp_inf_increase": 17000,
        "exp_table": [1500, 3500, 6000, 9000, 13500, 18500, 24000, 30000,
                      36500, 43500, 51000, 59500, 69000, 79500, 91000,
                      103500, 117000, 131500, 147000, 164000],
        "time": datetime.now(timezone.utc).isoformat(),
        "pls_desc": "",
    })


# ===========================================================================
# PLAYERDATA (from DB)
# ===========================================================================

@app.post("/dlbb/playerdata")
@app.get("/dlbb/playerdata")
async def get_playerdata(request: Request):
    pls_id = get_pls_id(request)
    if pls_id:
        return JSONResponse(db.get_playerdata(pls_id))
    # Fallback for unauthenticated requests
    return JSONResponse(db.get_playerdata(1))


# ===========================================================================
# SHOP
# ===========================================================================

@app.get("/dlbb/shop/{shop_id}")
async def get_shop(shop_id: int):
    return JSONResponse(load_fixture("shop.json"))


@app.post("/dlbb/shop/itempurchase")
async def shop_purchase(request: Request):
    pls_id = get_pls_id(request)
    if not pls_id:
        return JSONResponse({"pls_result": 1, "pls_desc": "Not authenticated"})

    body = await request.body()
    try:
        data = json.loads(body)
    except json.JSONDecodeError:
        data = {}

    uid = data.get("uid", "")
    hc_cost = data.get("hc", 0)
    sc_cost = data.get("sc", 0)

    result = db.purchase_item(pls_id, uid, sc_cost, hc_cost)
    if result:
        return JSONResponse(result)
    return JSONResponse({"pls_result": 1, "pls_desc": "Purchase failed"})


@app.post("/dlbb/shop/loot")
async def shop_loot(request: Request):
    return JSONResponse({"pls_result": 0, "pls_desc": "", "items": []})


# ===========================================================================
# LEADERBOARDS (static fixtures)
# ===========================================================================

@app.get("/dlbb/leaderboards")
async def get_leaderboards_list():
    return JSONResponse(load_fixture("leaderboards.json"))


@app.get("/dlbb/leaderboards/{board_id}/{category_id}")
async def get_leaderboard(board_id: int, category_id: int,
                          skip: int = 0, limit: int = 30,
                          my: int = 0, top: int = 0):
    players = db.get_leaderboard(category_id, limit=limit)
    return JSONResponse({
        "pls_result": 0, "pls_desc": "",
        "players": players,
    })


# ===========================================================================
# EVENTS / MOTD (static)
# ===========================================================================

@app.get("/events/current/{lang}/")
async def get_events_current(lang: str, release_level: int = 0):
    import time
    now_ms = int(time.time() * 1000)
    day_ms = 86400000

    # Halloween event — PlsId 2051 from bbevents.scr
    return JSONResponse([{
        "_id": {"$oid": "aaa000000000000000002051"},
        "id": "halloween",
        "pls_id": 2051,
        "date_from": {"$date": now_ms - 30 * day_ms},
        "date_to": {"$date": now_ms + 30 * day_ms},
        "reset_period": 0,
        "stats": [],
        "required_stats": [],
        "rewards": [
            {
                "score": 1,
                "item_id": {"$oid": "5bcdc0e93dd65e1f1cb630d2"},
            },
            {
                "score": 10,
                "item_id": {"$oid": "5bcdc0d83dd65e1f1cb630d1"},
            },
            {
                "score": 30,
                "item_id": {"$oid": "5bcdc1263dd65e1f1cb630d5"},
            },
        ],
    }])


@app.get("/events/{event_id}/{lang}")
async def get_event_detail(event_id: str, lang: str):
    """Event detail — returns downloadable content/files for the event."""
    log.info("EVENT DETAIL: %s/%s", event_id, lang)
    return JSONResponse({
        "pls_result": 0,
        "pls_desc": "",
        "content_types": [],
        "script_files": [],
        "script_commands": [],
        "script_level_commands": [],
        "snippet_id": "",
        "number": 0,
        "format": 0,
    })


@app.get("/messages/motd/current/{lang}/")
async def get_motd(lang: str):
    return JSONResponse([{
        "message": "Welcome to DLBB Revival by Mr.Pie! Join our Discord for updates.",
        "font": "",
        "duration": 10,
    }])


# ===========================================================================
# GAME SESSION
# ===========================================================================

@app.post("/dlbb/registergame")
async def register_game(request: Request):
    import secrets
    game_id = secrets.token_hex(12)  # 24 char hex like MongoDB ObjectId
    log.info("REGISTERGAME: id=%s", game_id)
    return JSONResponse({
        "pls_result": 0,
        "pls_desc": "",
        "id": game_id,
    })

@app.post("/dlbb/startgame")
async def start_game(request: Request):
    body = await request.body()
    log.info("STARTGAME: %s", body.decode("utf-8", errors="replace")[:200])
    return JSONResponse({"pls_result": 0, "pls_desc": "", "sp": None})

@app.post("/dlbb/gameresults")
async def post_game_results(request: Request):
    body = await request.body()
    try:
        data = json.loads(body)
        game_guid = data.get("game_guid", "")
        game_stats = data.get("game_stats", [])
        inv_items = data.get("inv_items", {})
        log.info("GAMERESULTS POST: guid=%s stats=%s items=%d", game_guid, game_stats, len(inv_items))

        # Calculate rewards based on stats
        total_score = sum(game_stats) if game_stats else 0
        sc_reward = max(50, total_score // 10)

        # Store game results for GET retrieval
        if not hasattr(app, '_game_results'):
            app._game_results = {}
        app._game_results[game_guid] = {
            "xmult": 1.0,
            "v": 1,
            "m": len(inv_items),
            "p": total_score,
            "s": game_stats,
            "smult": 1.0,
            "pls_result": 0,
            "sc": sc_reward,
            "pls_desc": "",
        }

        # Award SC and update stats
        pls_id = get_pls_id(request)
        if pls_id:
            db.add_credits(pls_id, sc_reward, 0)
            db.update_stats(pls_id, game_stats)
            log.info("GAMERESULTS: awarded %d SC to pls_id=%d, stats updated", sc_reward, pls_id)
    except Exception as e:
        log.error("GAMERESULTS error: %s", e)

    return JSONResponse({"pls_result": 0, "pls_desc": ""})

@app.get("/dlbb/gameresults/{game_guid}")
async def get_game_results(game_guid: str):
    if hasattr(app, '_game_results') and game_guid in app._game_results:
        return JSONResponse(app._game_results[game_guid])
    return JSONResponse({
        "xmult": 1.0, "v": 1, "m": 0, "p": 0, "s": [],
        "smult": 1.0, "pls_result": 0, "sc": 0, "pls_desc": "",
    })


# ===========================================================================
# REWARDS / TUTORIAL / MISC
# ===========================================================================

@app.get("/dlbb/lvlrewards")
@app.post("/dlbb/lvlrewards")
async def get_lvl_rewards():
    return JSONResponse(load_fixture("lvlrewards.json"))

@app.get("/dlbb/tutorial")
@app.post("/dlbb/tutorial")
async def tutorial():
    return JSONResponse({"pls_result": 0, "pls_desc": ""})

@app.post("/dlbb/mtxnrequest")
async def mtxn_request(request: Request):
    return JSONResponse({"pls_result": 0, "pls_desc": ""})

@app.post("/dlbb/mtxnfinalize")
async def mtxn_finalize(request: Request):
    return JSONResponse({"pls_result": 0, "pls_desc": ""})

@app.post("/dlbb/reportplayer")
async def report_player(request: Request):
    return JSONResponse({"pls_result": 0, "pls_desc": ""})

@app.get("/rewards/currentuser/")
async def get_rewards(release_level: int = 0):
    return JSONResponse({"pls_result": 0, "pls_desc": "", "rewards": []})

@app.get("/communityeventprogress")
async def community_event():
    return JSONResponse({"pls_result": 0, "pls_desc": ""})

@app.post("/telemetry/events/{event_id}")
async def telemetry(event_id: int, request: Request):
    return Response(status_code=200)

@app.get("/telemetry/stats/{player_id}/{stat_id}")
async def telemetry_stats(player_id: int, stat_id: int):
    return JSONResponse({"pls_result": 0, "pls_desc": ""})


# ===========================================================================
# CATCH-ALL
# ===========================================================================

@app.api_route("/{path:path}", methods=["GET", "POST", "PUT", "DELETE", "PATCH", "OPTIONS"])
async def catch_all(request: Request, path: str):
    body = await request.body()
    log.warning("UNKNOWN: %s /%s | Body: %s", request.method, path,
                body.decode("utf-8", errors="replace")[:300] if body else "(empty)")
    return JSONResponse({"pls_result": 0, "pls_desc": ""})


if __name__ == "__main__":
    import uvicorn
    uvicorn.run("main:app", host=config.HOST, port=config.HTTP_PORT,
                log_level=config.LOG_LEVEL.lower())
