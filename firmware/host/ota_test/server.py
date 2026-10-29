#!/usr/bin/env python3
"""Scripted update server for the host OTA test: a manifest per scenario and firmware downloads that
misbehave on purpose (stall, trickle, close early, redirect, error). Usage: server.py <port>"""
import socketserver
import sys
import time

IMAGE_LEN = 40000


def image(n):
    # first byte 0xE9 is the ESP image magic; the rest is a pattern the driver can regenerate
    return bytes([0xE9] + [(i * 31 + 7) & 0xFF for i in range(1, n)])


FIRMWARE = {"ok", "slow", "stall_mid", "stall_start", "short_close", "no_length", "http404", "no_headers",
            "redirect", "oversize", "write_fail"}


def manifest(name, port):
    base = f"http://127.0.0.1:{port}"
    part = lambda path, off: {"path": path, "offset": off}
    s3 = lambda url, ver: {"chipFamily": "ESP32-S3", "parts": [
        part(f"{base}/other/bootloader.bin", 0), part(f"{base}/other/partitions.bin", 32768), part(url, 65536)]}
    if name in FIRMWARE:
        return 200, {"name": "x", "version": "9.9.9", "builds": [s3(f"{base}/fw/{name}/firmware.bin", "9.9.9")]}
    if name == "m_stock":
        return 200, {"name": "x", "version": "1.9.0", "builds": [s3(f"{base}/fw/ok/firmware.bin", "1.9.0")]}
    if name == "m_same":
        return 200, {"version": "1.8.3", "builds": [s3(f"{base}/fw/ok/firmware.bin", "")]}
    if name == "m_older":
        return 200, {"version": "1.8.2", "builds": [s3(f"{base}/fw/ok/firmware.bin", "")]}
    if name == "m_double_digit":
        return 200, {"version": "1.8.10", "builds": [s3(f"{base}/fw/ok/firmware.bin", "")]}
    if name == "m_wrongchip":
        return 200, {"version": "9.9.9", "builds": [{"chipFamily": "ESP32", "parts": [part(f"{base}/fw/ok/firmware.bin", 0)]}]}
    if name == "m_multi":
        return 200, {"version": "9.9.9", "builds": [
            {"chipFamily": "ESP32", "parts": [part(f"{base}/fw/slow/firmware.bin", 0)]},
            s3(f"{base}/fw/ok/firmware.bin", "")]}
    if name == "m_nobuilds":
        return 200, {"version": "9.9.9"}
    if name == "m_nofirmwarepart":
        return 200, {"version": "9.9.9", "builds": [{"chipFamily": "ESP32-S3", "parts": [part(f"{base}/other/partitions.bin", 0)]}]}
    if name == "m_noversion":
        return 200, {"builds": [s3(f"{base}/fw/ok/firmware.bin", "")]}
    if name == "m_garbage":
        return 200, "<<< this is not json"
    if name == "m_500":
        return 500, "boom"
    return 404, "unknown"


class Handler(socketserver.StreamRequestHandler):
    def send(self, data):
        try:
            self.wfile.write(data)
            self.wfile.flush()
        except OSError:
            raise ConnectionAbortedError

    def head(self, status, extra):
        self.send(f"HTTP/1.1 {status}\r\n{extra}Connection: close\r\n\r\n".encode())

    def handle(self):
        try:
            self.serve()
        except (ConnectionAbortedError, BrokenPipeError, ConnectionResetError):
            pass

    def serve(self):
        line = self.rfile.readline().decode(errors="replace")
        while self.rfile.readline() not in (b"\r\n", b"\n", b""):
            pass
        parts = line.split(" ")[1].strip("/").split("/") if line.count(" ") >= 2 else []
        if len(parts) >= 2 and parts[0] == "m":
            name = parts[1].removesuffix(".json")
            if name == "m_hang":
                time.sleep(12)
                return
            status, body = manifest(name, self.server.server_address[1])
            import json
            payload = (json.dumps(body) if isinstance(body, dict) else body).encode()
            self.head(f"{status} {'OK' if status == 200 else 'ERR'}",
                      f"Content-Type: application/json\r\nContent-Length: {len(payload)}\r\n")
            self.send(payload)
        elif len(parts) >= 2 and parts[0] == "fw":
            self.firmware(parts[1])
        else:
            self.head("404 Not Found", "Content-Length: 0\r\n")

    def firmware(self, name):
        img = image(IMAGE_LEN)
        cl = f"Content-Type: application/octet-stream\r\nContent-Length: {IMAGE_LEN}\r\n"
        if name in ("ok", "write_fail"):
            self.head("200 OK", cl)
            self.send(img)
        elif name == "slow":                               # steady trickle: 2000 bytes every 150 ms
            self.head("200 OK", cl)
            for i in range(0, IMAGE_LEN, 2000):
                self.send(img[i:i + 2000])
                time.sleep(0.15)
        elif name == "stall_mid":                          # 16000 bytes, then silence with the connection open
            self.head("200 OK", cl)
            self.send(img[:16000])
            time.sleep(12)
        elif name == "stall_start":                        # headers, then silence
            self.head("200 OK", cl)
            time.sleep(12)
        elif name == "short_close":                        # promises 40000, sends 12000, hangs up
            self.head("200 OK", cl)
            self.send(img[:12000])
        elif name == "no_length":                          # no Content-Length: body ends when the connection closes
            self.send(b"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n\r\n")
            self.send(img)
        elif name == "http404":
            self.head("404 Not Found", "Content-Length: 9\r\n")
            self.send(b"not found")
        elif name == "no_headers":                         # accepts the request and never answers
            time.sleep(12)
        elif name == "redirect":
            self.head("302 Found", f"Location: /fw/ok/firmware.bin\r\nContent-Length: 0\r\n")
        elif name == "oversize":                           # bigger than an app slot
            self.head("200 OK", "Content-Length: 5242880\r\n")
            self.send(img[:1000])
            time.sleep(3)
        else:
            self.head("404 Not Found", "Content-Length: 0\r\n")


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    with Server(("127.0.0.1", int(sys.argv[1])), Handler) as srv:
        srv.serve_forever()
