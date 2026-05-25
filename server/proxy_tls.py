"""
Pure-Python TLS termination proxy.

Alternative to stunnel for operators who don't want a separate binary.
Accepts TLS on :443, forwards plaintext to :80. Slower and less battle-tested
than stunnel; recommended only for development. Production deployments should
prefer stunnel + Let's Encrypt.

Configure via env vars or edit constants below.
"""
import os
import ssl
import socket
import threading
import logging

logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[
        logging.StreamHandler(),
        logging.FileHandler("tls-proxy.log", encoding="utf-8"),
    ],
)
log = logging.getLogger("tls-proxy")

LISTEN_PORT = int(os.getenv("DLBB_PORT", "443"))
BACKEND_PORT = int(os.getenv("DLBB_HTTP_PORT", "80"))
LISTEN_HOST = os.getenv("DLBB_HOST", "0.0.0.0")
CERT = os.getenv("DLBB_TLS_CERT", "certs/cert.pem")
KEY = os.getenv("DLBB_TLS_KEY", "certs/key.pem")


def handle_client(client_ssl, addr):
    try:
        # Read request from TLS client
        data = b""
        while True:
            chunk = client_ssl.recv(4096)
            if not chunk:
                break
            data += chunk
            # HTTP request ends with \r\n\r\n
            if b"\r\n\r\n" in data:
                # Check if there's a Content-Length and read body
                headers = data.split(b"\r\n\r\n")[0].decode("utf-8", errors="replace")
                cl = 0
                for line in headers.split("\r\n"):
                    if line.lower().startswith("content-length:"):
                        cl = int(line.split(":")[1].strip())
                body_start = data.find(b"\r\n\r\n") + 4
                body_so_far = len(data) - body_start
                remaining = cl - body_so_far
                if remaining > 0:
                    data += client_ssl.recv(remaining)
                break

        if not data:
            client_ssl.close()
            return

        log.info(">>> From %s: %s", addr, data[:200].decode("utf-8", errors="replace"))

        # Forward to backend
        backend = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        backend.connect(("127.0.0.1", BACKEND_PORT))
        backend.sendall(data)

        # Read response
        response = b""
        while True:
            chunk = backend.recv(4096)
            if not chunk:
                break
            response += chunk
        backend.close()

        log.info("<<< Response: %s", response[:200].decode("utf-8", errors="replace"))

        # Send back to TLS client
        client_ssl.sendall(response)
    except Exception as e:
        log.error("Error handling %s: %s", addr, e)
    finally:
        try:
            client_ssl.close()
        except:
            pass


def main():
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.minimum_version = ssl.TLSVersion.TLSv1
    ctx.maximum_version = ssl.TLSVersion.TLSv1_2
    ctx.load_cert_chain(CERT, KEY)
    ctx.set_ciphers("DEFAULT:@SECLEVEL=1")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((LISTEN_HOST, LISTEN_PORT))
    sock.listen(32)

    log.info("TLS proxy listening on port %d -> localhost:%d", LISTEN_PORT, BACKEND_PORT)

    while True:
        client, addr = sock.accept()
        try:
            client_ssl = ctx.wrap_socket(client, server_side=True)
            log.info("TLS handshake OK from %s", addr)
            threading.Thread(target=handle_client, args=(client_ssl, addr), daemon=True).start()
        except ssl.SSLError as e:
            log.error("TLS handshake FAILED from %s: %s", addr, e)
            client.close()
        except Exception as e:
            log.error("Connection error from %s: %s", addr, e)
            client.close()


if __name__ == "__main__":
    main()
