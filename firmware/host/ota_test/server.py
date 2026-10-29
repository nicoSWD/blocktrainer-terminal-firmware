#!/usr/bin/env python3
"""Scripted update server for the host OTA test: a GitHub-style 'latest release' document per scenario and firmware downloads that
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


def release(tag, assets):
    """A trimmed-down GitHub 'latest release' object; the noise fields (and a long body) are what
    the firmware's JSON filter has to skip."""
    return {
        "url": "https://api.github.com/repos/x/y/releases/1",
        "id": 1,
        "tag_name": tag,
        "name": f"Firmware {tag}",
        "draft": False,
        "prerelease": False,
        "author": {"login": "someone", "id": 2, "avatar_url": "https://example.invalid/a.png"},
        "body": "Release notes. " * 400,
        "assets": [{"id": i, "name": n, "size": 40000, "state": "uploaded", "browser_download_url": u,
                    "uploader": {"login": "someone"}} for i, (n, u) in enumerate(assets)],
    }


def manifest(name, port):
    base = f"http://127.0.0.1:{port}"
    fw = lambda ver, path="/fw/ok/firmware.bin": [
        (f"bt-terminal-firmware-v{ver}-debug.bin", f"{base}/fw/slow/firmware.bin"),
        (f"bt-terminal-firmware-v{ver}-safe.bin", f"{base}/fw/slow/firmware.bin"),
        (f"bt-terminal-firmware-v{ver}.bin", f"{base}{path}"),
        ("SHA256SUMS.txt", f"{base}/other/SHA256SUMS.txt"),
        ("LICENSE", f"{base}/other/LICENSE"),
    ]
    if name in FIRMWARE:                                   # the download scenarios start from a newer release
        return 200, release("v9.9.9", fw("9.9.9", f"/fw/{name}/firmware.bin"))
    if name == "m_newer":
        return 200, release("v1.9.0", fw("1.9.0"))
    if name == "m_double_digit":
        return 200, release("v0.10.0", fw("0.10.0"))
    if name == "m_same":
        return 200, release("v0.1.0", fw("0.1.0"))
    if name == "m_older":
        return 200, release("v0.0.9", fw("0.0.9"))
    if name == "m_no_v":
        return 200, release("2.0.0", fw("2.0.0"))
    if name == "m_asset_order":                            # the firmware asset is last, after the decoys
        return 200, release("v3.0.0", list(reversed(fw("3.0.0"))))
    if name == "m_wrongasset":                             # asset names a different version than the tag
        return 200, release("v9.9.9", fw("9.9.8"))
    if name == "m_onlydecoys":
        return 200, release("v9.9.9", [a for a in fw("9.9.9") if a[0] != "bt-terminal-firmware-v9.9.9.bin"])
    if name == "m_noassets":
        return 200, release("v9.9.9", [])
    if name == "m_notag":
        r = release("v9.9.9", fw("9.9.9"))
        del r["tag_name"]
        return 200, r
    if name == "m_garbage":
        return 200, "<<< this is not json"
    if name == "m_500":
        return 500, "boom"
    if name == "m_404":                                    # repository without a release yet
        return 404, {"message": "Not Found"}
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
        headers = {}
        while True:
            h = self.rfile.readline()
            if h in (b"\r\n", b"\n", b""):
                break
            k, _, v = h.decode(errors="replace").partition(":")
            headers[k.strip().lower()] = v.strip()
        parts = line.split(" ")[1].strip("/").split("/") if line.count(" ") >= 2 else []
        if len(parts) >= 2 and parts[0] == "m":
            name = parts[1].removesuffix(".json")
            if name == "m_hang":
                time.sleep(12)
                return
            status, body = manifest(name, self.server.server_address[1])
            import json
            # like the real API: no User-Agent is refused outright, and the vendor Accept type is expected
            if not headers.get("user-agent", "").startswith("bt-terminal-firmware/"):
                status, body = 403, {"message": "Request forbidden by administrative rules. Please make sure your request has a User-Agent header."}
            elif "application/vnd.github+json" not in headers.get("accept", ""):
                status, body = 415, {"message": "Unsupported Accept"}
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
