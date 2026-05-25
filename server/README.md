# server/

FastAPI backend that re-implements Techland's PLS (Profile / Login / Shop)
endpoints for `Dying Light: Bad Blood`. Single-process, SQLite-backed,
fits on the smallest VPS.

## Files

| File                       | Role                                                    |
|----------------------------|---------------------------------------------------------|
| `main.py`                  | FastAPI app — all endpoints + Goldberg P2P relay        |
| `db.py`                    | SQLite layer — accounts, items, friends, sessions, leaderboard |
| `config.py`                | All settings, env-var driven (see `.env.example`)       |
| `proxy_to_techland.py`     | Historical RE tool — capture proxy against the original Techland server. Not used in normal operation. |
| `proxy_tls.py`             | Pure-Python TLS terminator. Alternative to stunnel.     |
| `stunnel.conf.template`    | Stunnel config (copy → `stunnel.conf`, edit paths)      |
| `fixtures/*.json`          | Static response bodies (leaderboards index, shop catalog, lvlrewards, mtxnrequest, playerdata template) |
| `generate-cert.bat`        | One-shot self-signed cert generator (openssl required)  |
| `run.bat` / `run.sh`       | Convenience launchers                                   |
| `requirements.txt`         | `fastapi`, `uvicorn[standard]`, `pydantic`              |

## Configuration

Copy `.env.example` to `.env`, edit. The only setting most operators must
change is `DLBB_DOMAIN`. Everything else has sensible defaults.

```bash
DLBB_DOMAIN=pls.example.it
DLBB_HOST=0.0.0.0
DLBB_HTTP_PORT=80
DLBB_DB_PATH=data/dlbb.db
```

If you want the launcher to download a client patch from your server via
`/api/patch`, also set `DLBB_PATCH_DIR=/path/to/your/patched-client-files`.
This project doesn't ship any Techland-derived patched files — operate your
own private mirror if you provide this endpoint.

## Running

```bash
# Windows
run.bat

# Linux/macOS
./run.sh
```

Both scripts install dependencies on first run, then start uvicorn on the
port configured in `.env` (default 80).

For HTTPS, stunnel must run separately on port 443, decrypting and
forwarding to the backend. See `stunnel.conf.template` and the
[main README](../README.md#quick-start--server-operator-windows).

## Endpoints

Implemented:

| Method | Path                                  | What                                |
|--------|---------------------------------------|-------------------------------------|
| POST   | `/api/register`                       | Launcher: create account            |
| POST   | `/api/login`                          | Launcher: login → returns session   |
| POST   | `/auth/login/steam/`                  | Game: Steam ticket → PLS token      |
| POST   | `/auth/logout/`                       | Game: logout                        |
| GET    | `/dlbb/gameconfig`                    | Game: feature bitmask, exp table    |
| GET/POST | `/dlbb/playerdata`                  | Game: profile + inventory           |
| GET    | `/dlbb/shop/{id}`                     | Game: shop catalog                  |
| POST   | `/dlbb/shop/itempurchase`             | Game: buy item                      |
| POST   | `/dlbb/shop/loot`                     | Game: loot box                      |
| GET    | `/dlbb/leaderboards`                  | Game: leaderboard index             |
| GET    | `/dlbb/leaderboards/{b}/{c}`          | Game: leaderboard rows              |
| GET/POST | `/dlbb/lvlrewards`                  | Game: level-up rewards              |
| GET/POST | `/dlbb/tutorial`                    | Game: tutorial step                 |
| POST   | `/dlbb/registergame`                  | Game: register a new match          |
| POST   | `/dlbb/startgame`                     | Game: match started                 |
| POST   | `/dlbb/gameresults`                   | Game: match results                 |
| GET    | `/dlbb/gameresults/{guid}`            | Game: match results (read-back)     |
| GET    | `/events/current/{lang}/`             | Game: live events                   |
| GET    | `/messages/motd/current/{lang}/`      | Game: MOTD                          |
| POST   | `/telemetry/events/{event_id}`        | Game: telemetry sink                |
| GET    | `/rewards/currentuser/`               | Game: rewards                       |
| GET    | `/api/version`                        | Launcher: current patch version     |
| GET    | `/api/patch`                          | Launcher: zip of patched client files (only if `DLBB_PATCH_DIR` is set) |
| POST   | `/api/friends/heartbeat`              | Launcher: keepalive                 |
| GET    | `/api/friends/list`                   | Launcher: list friends + status     |
| GET    | `/api/friends/requests`               | Launcher: incoming/outgoing reqs    |
| POST   | `/api/friends/add`                    | Launcher: send friend request       |
| POST   | `/api/friends/accept`                 | Launcher: accept request            |
| POST   | `/api/friends/decline`                | Launcher: decline                   |
| POST   | `/api/friends/cancel`                 | Launcher: cancel outgoing           |
| POST   | `/api/friends/remove`                 | Launcher: unfriend                  |
| POST   | `/api/lobby/invite`                   | Launcher: send lobby invite         |
| GET    | `/api/lobby/invites/poll`             | Launcher: pull pending invites      |
| any    | `/{path:path}`                        | Catch-all: returns `{"pls_result":0}` and logs the URL. Useful for discovering endpoints we haven't implemented yet. |

## Goldberg P2P relay

The server also opens a UDP + TCP relay on `DLBB_RELAY_PORT` (default 47584).
Goldberg writes the public IP of this relay into each client's
`steam_settings/custom_broadcasts.txt`, and uses it to advertise lobbies
across NATs. Without this, players behind CGNAT can't see each other.

Relay logic in `main.py` under "Goldberg Steam Emu Relay".

## Database

SQLite file at `DLBB_DB_PATH` (default `data/dlbb.db`). Auto-created on
first start. Tables: `players`, `player_items`, `player_consumables`,
`friends`, `friend_requests`, `player_sessions`, `lobby_invites`.

A new player gets 30 default items + 30,000 SC (from `DEFAULT_ITEMS` /
`DEFAULT_SC` in `db.py`). Without the default items, the game crashes
at the main menu — Techland-side fresh accounts had them, so we do too.

## Reverse-engineering notes

The response formats here are not invented. They were captured from the
**real** Techland server at `pls.dlbb.com` (still up at the time, 2026,
returning 404 on `/` and 500 on `/auth/login/steam/`, but other endpoints
echoing back valid envelopes). See `proxy_to_techland.py` for the capture
proxy and [`docs/recon-findings.md`](../docs/recon-findings.md) for the
full investigation.

The auth format specifically (`{"token":"<24hex>","enabled":3071}`) was
discovered this way. The `enabled:3071` bitmask is the same number their
server has returned to every account since at least 2017.

## Testing

```bash
# Basic auth check
curl -sk https://pls.example.it/auth/login/steam/ \
    -X POST -d "auth_session_ticket=test123&bb_purchase"
# expect: {"token":"<some 24-char hex>","enabled":3071}

# Player data
curl -sk -X POST https://pls.example.it/dlbb/playerdata \
    -H "PLS-Authorization: Token <token-from-above>"
# expect: full player profile JSON
```

The catch-all logs everything else, so the easiest debugging loop is:
launch the game, watch `pls-emu.log`, see which endpoint returned 200
catch-all (= we faked OK but didn't implement it properly), and decide
whether to implement it for real.
