import socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('0.0.0.0', 47584))
clients = {}
print("UDP relay on :47584")
while True:
    data, addr = sock.recvfrom(4096)
    clients[addr] = True
    for c in list(clients):
        if c != addr:
            sock.sendto(data, c)
