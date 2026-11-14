import socket
import threading
import serial

PORT = "COM7"
BAUD = 115200
TCP_PORT = 8888

ser = serial.Serial(PORT, BAUD, timeout=0.01, write_timeout=1)
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(("192.168.64.1", TCP_PORT))
server.listen(1)
print(f"COM7 bridge listening on TCP {TCP_PORT}", flush=True)
conn, addr = server.accept()
print(f"WSL connected from {addr}", flush=True)

def serial_to_tcp():
    while True:
        data = ser.read(4096)
        if data:
            conn.sendall(data)

threading.Thread(target=serial_to_tcp, daemon=True).start()
try:
    while True:
        data = conn.recv(4096)
        if not data:
            break
        ser.write(data)
finally:
    conn.close()
    ser.close()
    server.close()
