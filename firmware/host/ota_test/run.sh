#!/bin/sh
# Starts the scripted server on a free port, runs the OTA test binary against it, stops the server.
set -eu
cd "$(dirname "$0")/.."
PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1]);s.close()')
python3 ota_test/server.py "$PORT" &
SERVER=$!
trap 'kill $SERVER 2>/dev/null || true' EXIT
for i in 1 2 3 4 5 6 7 8 9 10; do
    python3 -c "import socket;socket.create_connection(('127.0.0.1',$PORT),1).close()" 2>/dev/null && break
    sleep 0.2
done
HOST_TIMEOUT_SCALE=0.1 build/ota_test "$PORT"
