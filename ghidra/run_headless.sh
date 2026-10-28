#!/usr/bin/env bash
# Import firmware.elf (if needed) and launch the GhidraMCP headless server.
# Run on the machine where Ghidra is installed (tested on Kali / aarch64).
set -euo pipefail

GHIDRA_HOME="${GHIDRA_HOME:-$HOME/ghidra_12.1.4_PUBLIC}"
MCP_VERSION="${GHIDRA_MCP_VERSION:-7.0.0}"
JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-21-openjdk-arm64}"
PROJECT_DIR="${PROJECT_DIR:-$HOME/ghidra_projects}"
PROJECT_NAME="${PROJECT_NAME:-btclock}"
PROGRAM="${1:-firmware.elf}"
PORT="${GHIDRA_MCP_PORT:-8089}"

MCP_JAR="$HOME/.ghidra/.ghidra_12.1.4_PUBLIC/Extensions/GhidraMCP/lib/GhidraMCP-${MCP_VERSION}.jar"

# --- import + auto-analyze if the project doesn't exist yet ---
if [ ! -d "$PROJECT_DIR/$PROJECT_NAME.rep" ]; then
  echo "Project not found; importing $PROGRAM ..."
  mkdir -p "$PROJECT_DIR"
  "$GHIDRA_HOME/support/analyzeHeadless" "$PROJECT_DIR" "$PROJECT_NAME" \
    -import "$PROGRAM" -overwrite -max-cpu 4
fi

# --- build classpath: GhidraMCP jar + all Ghidra jars ---
CP="$MCP_JAR"
for jar in "$GHIDRA_HOME"/Ghidra/Framework/*/lib/*.jar \
           "$GHIDRA_HOME"/Ghidra/Features/*/lib/*.jar \
           "$GHIDRA_HOME"/Ghidra/Processors/*/lib/*.jar; do
  CP="$CP:$jar"
done

exec "$JAVA_HOME/bin/java" -Xmx4g \
  -Dghidra.home="$GHIDRA_HOME" \
  -Dapplication.name=GhidraMCP \
  -classpath "$CP" \
  com.xebyte.headless.GhidraMCPHeadlessServer \
  --port "$PORT" --bind 127.0.0.1 \
  --project "$PROJECT_DIR" --program "$PROGRAM"
