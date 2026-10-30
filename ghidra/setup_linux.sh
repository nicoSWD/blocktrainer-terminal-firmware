#!/usr/bin/env bash
# One-shot Ghidra + ghidra-mcp setup for aarch64 (ARM64 Linux).
# Installs: Java 21, Ghidra 12.1.4, the ghidra-mcp extension + bridge, and the
# ARM64 decompiler native binary (Ghidra ships none for linux_arm_64).
#
# Usage:  sudo ./setup_linux.sh     (or run steps manually if you dislike sudo)
set -euo pipefail

GHIDRA_VERSION="12.1.4"
GHIDRA_ZIP="ghidra_12.1.4_PUBLIC_20260921.zip"
GHIDRA_URL="https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_${GHIDRA_VERSION}_build/${GHIDRA_ZIP}"

MCP_VERSION="7.0.0"
MCP_EXT_URL="https://github.com/bethington/ghidra-mcp/releases/download/v${MCP_VERSION}/GhidraMCP-${MCP_VERSION}.zip"
MCP_BRIDGE_URL="https://github.com/bethington/ghidra-mcp/releases/download/v${MCP_VERSION}/ghidra_mcp_bridge-${MCP_VERSION}-py3-none-any.whl"

ARCH="$(uname -m)"

# --- 1. Java 21 (Ghidra 12.1.4 needs JDK 21) ---
if ! command -v java >/dev/null 2>&1 || ! java -version 2>&1 | grep -q '21'; then
  echo "==> Installing OpenJDK 21 ..."
  sudo apt-get update -qq
  sudo apt-get install -y -qq openjdk-21-jdk
fi
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-21-openjdk-${ARCH}}"

# --- 2. uv (installs the MCP bridge) ---
if ! command -v uv >/dev/null 2>&1; then
  echo "==> Installing uv ..."
  curl -LsSf https://astral.sh/uv/install.sh | sh
  export PATH="$HOME/.local/bin:$PATH"
fi

# --- 3. Ghidra 12.1.4 ---
GHIDRA_HOME="$HOME/ghidra_${GHIDRA_VERSION}_PUBLIC"
if [ ! -d "$GHIDRA_HOME" ]; then
  echo "==> Downloading Ghidra ${GHIDRA_VERSION} ..."
  curl -sSL -o /tmp/ghidra.zip "$GHIDRA_URL"
  unzip -q /tmp/ghidra.zip -d "$HOME"
fi

# --- 4. ghidra-mcp extension ---
echo "==> Installing GhidraMCP extension ..."
curl -sSL -o /tmp/GhidraMCP.zip "$MCP_EXT_URL"
EXT_DIR="$HOME/.ghidra/.ghidra_${GHIDRA_VERSION}_PUBLIC/Extensions"
mkdir -p "$EXT_DIR"
unzip -oq /tmp/GhidraMCP.zip -d "$EXT_DIR"

# --- 5. ghidra-mcp bridge ---
echo "==> Installing bridge ..."
uv tool install "$MCP_BRIDGE_URL"

# --- 6. ARM64 decompiler native (the missing piece) ---
DECOMP_OS="$GHIDRA_HOME/Ghidra/Features/Decompiler/os"
DECOMP_SRC="$GHIDRA_HOME/Ghidra/Features/Decompiler/src/decompile/cpp"
if [ ! -f "$DECOMP_OS/linux_${ARCH}/decompile" ]; then
  echo "==> Building decompiler for linux_${ARCH} ..."
  if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    OSDIR="linux_arm_64"; ARCH_FLAG=""
  else
    OSDIR="linux_x86_64"; ARCH_FLAG="-m64"
  fi
  mkdir -p "$DECOMP_OS/$OSDIR"
  make -C "$DECOMP_SRC" install_ghidraopt \
    OSDIR="$OSDIR" ARCH_TYPE="$ARCH_FLAG" GHIDRA_BIN="$GHIDRA_HOME" -j"$(nproc)"
fi

echo
echo "Done. Ghidra at $GHIDRA_HOME, decompiler at $DECOMP_OS/linux_${ARCH}/decompile."
echo "Next: build firmware.elf (scripts/build_elf.py), then ghidra/run_headless.sh firmware.elf"
