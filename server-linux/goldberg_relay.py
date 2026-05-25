#!/usr/bin/env python3
"""
Goldberg Steam Emu Relay — forwards TCP+UDP between peers
Run on VPS: python3 goldberg_relay.py
Clients set custom_broadcasts.txt to VPS IP
"""
import socket
import threading
import time
import sys

PORT = 47584

# ============================================================
# UDP Relay
# ============================================================
udp_clients = {}  # addr -> last_seen timestamp

def udp_relay():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', PORT))
    print(f"[UDP] Relay listening on :{PORT}")

    while True:
        try:
            data, addr = sock.recvfrom(8192)
            now = time.time()
            udp_clients[addr] = now

            # Clean old clients (>60s)
            for c in list(udp_clients):
                if now - udp_clients[c] > 60:
                    del udp_clients[c]

            # Forward to all other clients
            for c in list(udp_clients):
                if c != addr:
                    try:
                        sock.sendto(data, c)
                    except:
                        pass

            if len(data) > 0 and addr not in udp_clients:
                print(f"[UDP] New client: {addr}")

        except Exception as e:
            print(f"[UDP] Error: {e}")

# ============================================================
# TCP Relay (pairs connections)
# ============================================================
tcp_clients = []  # list of connected sockets
tcp_lock = threading.Lock()

def tcp_pipe(src, dst, label):
    """Forward data from src to dst"""
    try:
        while True:
            data = src.recv(8192)
            if not data:
                break
            dst.sendall(data)
    except:
        pass
    try: src.close()
    except: pass
    try: dst.close()
    except: pass
    print(f"[TCP] {label} pipe closed")

def tcp_handler(client_sock, client_addr):
    """Handle a new TCP connection — pair with another waiting client"""
    print(f"[TCP] New connection from {client_addr}")

    with tcp_lock:
        # Check if there's a waiting client to pair with
        waiting = None
        for i, (sock, addr, t) in enumerate(tcp_clients):
            # Check if socket is still alive and not too old
            if time.time() - t < 30:
                waiting = tcp_clients.pop(i)
                break
            else:
                try: sock.close()
                except: pass
                tcp_clients.pop(i)
                break

    if waiting:
        other_sock, other_addr, _ = waiting
        print(f"[TCP] Pairing {client_addr} <-> {other_addr}")
        # Start bidirectional pipe
        t1 = threading.Thread(target=tcp_pipe, args=(client_sock, other_sock, f"{client_addr}->{other_addr}"), daemon=True)
        t2 = threading.Thread(target=tcp_pipe, args=(other_sock, client_sock, f"{other_addr}->{client_addr}"), daemon=True)
        t1.start()
        t2.start()
    else:
        # No one waiting — add to queue
        with tcp_lock:
            tcp_clients.append((client_sock, client_addr, time.time()))
        print(f"[TCP] {client_addr} waiting for peer... ({len(tcp_clients)} in queue)")

def tcp_relay():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', PORT))
    sock.listen(10)
    print(f"[TCP] Relay listening on :{PORT}")

    while True:
        try:
            client, addr = sock.accept()
            threading.Thread(target=tcp_handler, args=(client, addr), daemon=True).start()
        except Exception as e:
            print(f"[TCP] Error: {e}")

# ============================================================
# Main
# ============================================================
if __name__ == '__main__':
    print(f"=== Goldberg Relay on port {PORT} ===")
    threading.Thread(target=udp_relay, daemon=True).start()
    threading.Thread(target=tcp_relay, daemon=True).start()

    try:
        while True:
            time.sleep(10)
            active = len([c for c in udp_clients.values() if time.time() - c < 30])
            if active > 0:
                print(f"[STATUS] {active} UDP clients active, {len(tcp_clients)} TCP waiting")
    except KeyboardInterrupt:
        print("\nShutting down")
