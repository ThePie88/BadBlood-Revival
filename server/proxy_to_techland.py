"""
Transparent reverse-engineering proxy.

Historical / research tool. Captured every request the game makes, forwarded
it to the (still-up-but-broken) Techland PLS server at the time, then logged
both sides into fixtures_captured/. This is how the response shapes in
fixtures/*.json were discovered.

NOT used in normal operation. Kept here for transparency about how the
emulator was built. The original Techland endpoint may or may not still
respond — last verified IPs are documented in docs/reverse-engineering.md.

Override the upstream via env var DLBB_TECHLAND_URL.
"""
import logging
import os
import json
from fastapi import FastAPI, Request
from fastapi.responses import Response
import httpx
import config

logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[
        logging.StreamHandler(),
        logging.FileHandler("proxy-techland.log", encoding="utf-8"),
    ],
)
log = logging.getLogger("proxy")

# Last known Techland PLS server IP (AWS, returns 500 on /auth/login/steam/
# as of late 2026 but DNS A record for pls.dlbb.com still resolves here).
# Override if you find a different upstream.
TECHLAND_URL = os.getenv("DLBB_TECHLAND_URL", "https://23.23.84.188")

app = FastAPI(title="DLBB Proxy to Techland")

@app.api_route("/{path:path}", methods=["GET", "POST", "PUT", "DELETE", "PATCH", "OPTIONS"])
async def proxy(request: Request, path: str):
    body = await request.body()

    # Log request
    log.info("=" * 60)
    log.info(">>> %s /%s", request.method, path)
    log.info(">>> Headers: %s", dict(request.headers))
    if body:
        log.info(">>> Body (%d bytes): %s", len(body), body.decode("utf-8", errors="replace")[:500])

    # Forward to Techland
    target_url = f"{TECHLAND_URL}/{path}"
    if request.query_params:
        target_url += f"?{request.query_params}"

    # Copy headers, change Host
    headers = dict(request.headers)
    headers["host"] = "pls.dlbb.com"
    headers.pop("transfer-encoding", None)

    try:
        async with httpx.AsyncClient(verify=False, timeout=30.0) as client:
            resp = await client.request(
                method=request.method,
                url=target_url,
                headers=headers,
                content=body,
            )

        # Log response
        log.info("<<< Status: %d", resp.status_code)
        log.info("<<< Headers: %s", dict(resp.headers))
        resp_body = resp.content
        log.info("<<< Body (%d bytes)", len(resp_body))

        # Save full response to fixture file
        import os, re
        fixture_dir = os.path.join(os.path.dirname(__file__), "fixtures_captured")
        os.makedirs(fixture_dir, exist_ok=True)
        safe_name = re.sub(r'[^a-zA-Z0-9_]', '_', path.strip('/')) or "root"
        fixture_path = os.path.join(fixture_dir, f"{request.method}_{safe_name}.json")
        with open(fixture_path, "wb") as f:
            f.write(resp_body)
        log.info("<<< Saved to %s", fixture_path)

        # Return response to client
        response_headers = dict(resp.headers)
        response_headers.pop("transfer-encoding", None)
        response_headers.pop("content-encoding", None)
        response_headers.pop("content-length", None)

        return Response(
            content=resp_body,
            status_code=resp.status_code,
            headers=response_headers,
        )
    except Exception as e:
        log.error("<<< PROXY ERROR: %s", e)
        return Response(content=str(e), status_code=502)


if __name__ == "__main__":
    import uvicorn
    uvicorn.run("proxy_to_techland:app", host=config.HOST, port=config.HTTP_PORT, log_level="info")
