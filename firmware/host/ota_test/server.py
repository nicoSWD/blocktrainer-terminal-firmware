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
            "redirect", "oversize", "write_fail", "redirect_abs", "redirect_chain", "redirect_loop", "empty_body",
            "html_page", "extra_bytes", "end_fail", "one_task"}


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
    if name == "m_lookalikes":                             # near misses first, the real asset last
        return 200, release("v5.0.0", [
            ("bt-terminal-firmware-v5.0.0.bin.sig", f"{base}/fw/slow/firmware.bin"),
            ("x-bt-terminal-firmware-v5.0.0.bin", f"{base}/fw/slow/firmware.bin"),
            ("BT-TERMINAL-FIRMWARE-V5.0.0.BIN", f"{base}/fw/slow/firmware.bin"),
            ("bt-terminal-firmware-v5.0.0-debug.bin", f"{base}/fw/slow/firmware.bin"),
            ("bt-terminal-firmware-v5.0.00.bin", f"{base}/fw/slow/firmware.bin"),
            ("bt-terminal-firmware-v5.0.0.bin", f"{base}/fw/ok/firmware.bin")])
    if name == "m_lookalikes_only":
        r = manifest("m_lookalikes", port)[1]
        r["assets"] = r["assets"][:-1]
        return 200, r
    if name == "m_many_assets":                            # 60 assets, the firmware one last
        extra = [(f"extra-{i}.txt", f"https://github.com/owner/repo/releases/download/v4.0.0/extra-{i}.txt") for i in range(60)]
        return 200, release("v4.0.0", extra + fw("4.0.0"))
    if name == "m_huge_body":                              # release notes at the size GitHub allows
        r = release("v6.0.0", fw("6.0.0"))
        r["body"] = "x" * 300000
        return 200, r
    if name == "m_tag_upper":
        return 200, release("V2.0.0", fw("2.0.0"))
    if name == "m_tag_number":
        r = release("v9.9.9", fw("9.9.9"))
        r["tag_name"] = 123
        return 200, r
    if name == "m_tag_null":
        r = release("v9.9.9", fw("9.9.9"))
        r["tag_name"] = None
        return 200, r
    if name == "m_assets_null":
        r = release("v9.9.9", [])
        r["assets"] = None
        return 200, r
    if name == "m_assets_object":                          # wrong type
        r = release("v9.9.9", [])
        r["assets"] = {"name": "bt-terminal-firmware-v9.9.9.bin"}
        return 200, r
    if name == "m_null_entries":                           # null / empty / mistyped entries around the real one
        r = release("v7.0.0", fw("7.0.0"))
        r["assets"] = [None, {}, {"name": None}, {"name": 5, "browser_download_url": 6},
                       {"name": "bt-terminal-firmware-v7.0.0.bin", "browser_download_url": None}] + r["assets"]
        return 200, r
    if name == "m_no_url":
        r = release("v9.9.9", fw("9.9.9"))
        for a in r["assets"]:
            a.pop("browser_download_url", None)
        return 200, r
    if name == "m_empty_url":
        r = release("v9.9.9", fw("9.9.9"))
        for a in r["assets"]:
            a["browser_download_url"] = ""
        return 200, r
    if name == "m_empty_object":
        return 200, {}
    if name == "m_json_array":
        return 200, []
    if name == "m_json_null":
        return 200, "null"
    if name == "m_empty":
        return 200, ""
    if name == "m_garbage":
        return 200, "<<< this is not json"
    if name == "m_500":
        return 500, "boom"
    if name == "m_403_ratelimit":                          # what an exhausted unauthenticated quota looks like
        return 403, {"message": "API rate limit exceeded for 1.2.3.4.", "documentation_url": "https://docs.github.com/rest"}
    if name == "m_429":
        return 429, {"message": "Too many requests"}
    if name == "m_503":
        return 503, "<html>Service Unavailable</html>"
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
            port = self.server.server_address[1]
            if self.raw_release(name, port):
                return
            status, body = manifest(name, port)
            import json
            # like the real API: no User-Agent is refused outright, and the vendor Accept type is expected
            if not headers.get("user-agent", "").startswith("bt-terminal-firmware/"):
                status, body = 403, {"message": "Request forbidden by administrative rules. Please make sure your request has a User-Agent header."}
            elif "application/vnd.github+json" not in headers.get("accept", ""):
                status, body = 415, {"message": "Unsupported Accept"}
            payload = (json.dumps(body) if isinstance(body, (dict, list)) else body).encode()
            self.head(f"{status} {'OK' if status == 200 else 'ERR'}",
                      f"Content-Type: application/json\r\nContent-Length: {len(payload)}\r\n")
            self.send(payload)
        elif len(parts) >= 2 and parts[0] == "fw":
            self.firmware(parts[1])
        else:
            self.head("404 Not Found", "Content-Length: 0\r\n")

    def raw_release(self, name, port):
        """Release lookups that need control over the framing; True when handled."""
        import json
        full = json.dumps(manifest("m_newer", port)[1]).encode()
        json_h = "Content-Type: application/json\r\n"
        if name == "m_redirect":                           # a renamed repository: 301 to the new API path
            self.head("301 Moved Permanently", f"Location: http://127.0.0.1:{port}/m/m_newer.json\r\nContent-Length: 0\r\n")
        elif name == "m_redirect_loop":
            self.head("302 Found", f"Location: http://127.0.0.1:{port}/m/m_redirect_loop.json\r\nContent-Length: 0\r\n")
        elif name == "m_no_length":                        # body ends when the connection closes
            self.send(b"HTTP/1.1 200 OK\r\n" + json_h.encode() + b"Connection: close\r\n\r\n")
            self.send(full)
        elif name == "m_truncated":                        # promises everything, sends the first half, hangs up
            self.head("200 OK", json_h + f"Content-Length: {len(full)}\r\n")
            self.send(full[:len(full) // 2])
        elif name == "m_stall_body":                       # half the body, then silence with the connection open
            self.head("200 OK", json_h + f"Content-Length: {len(full)}\r\n")
            self.send(full[:len(full) // 2])
            time.sleep(12)
        elif name == "m_slow_body":                        # trickles in, but never stalls
            self.head("200 OK", json_h + f"Content-Length: {len(full)}\r\n")
            for i in range(0, len(full), 2000):
                self.send(full[i:i + 2000])
                time.sleep(0.05)
        elif name == "m_early_close":                      # accepts the request and hangs up without answering
            pass
        else:
            return False
        return True

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
        elif name == "redirect_abs":                       # GitHub answers with an absolute URL on another host
            self.head("302 Found", f"Location: http://127.0.0.1:{self.server.server_address[1]}/fw/ok/firmware.bin\r\nContent-Length: 0\r\n")
        elif name == "redirect_chain":
            self.head("302 Found", "Location: /fw/redirect_hop/firmware.bin\r\nContent-Length: 0\r\n")
        elif name == "redirect_hop":
            self.head("302 Found", f"Location: http://127.0.0.1:{self.server.server_address[1]}/fw/ok/firmware.bin\r\nContent-Length: 0\r\n")
        elif name == "redirect_loop":
            self.head("302 Found", "Location: /fw/redirect_loop/firmware.bin\r\nContent-Length: 0\r\n")
        elif name == "empty_body":
            self.head("200 OK", "Content-Type: application/octet-stream\r\nContent-Length: 0\r\n")
        elif name == "html_page":                          # a captive portal / error page served as 200
            page = b"<html><body>Please log in to continue</body></html>" * 60
            self.head("200 OK", f"Content-Type: text/html\r\nContent-Length: {len(page)}\r\n")
            self.send(page)
        elif name == "extra_bytes":                        # more bytes than Content-Length promised
            self.head("200 OK", cl)
            self.send(img + b"\xff" * 5000)
        elif name == "end_fail":                           # a good-looking image; the test makes the final validation fail
            self.head("200 OK", cl)
            self.send(img)
        elif name == "one_task":                           # slow enough for a second start to overlap
            self.head("200 OK", cl)
            for i in range(0, IMAGE_LEN, 4000):
                self.send(img[i:i + 4000])
                time.sleep(0.1)
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
