#!/usr/bin/env bash
# Apply the XNU-loader embedding to a Mu-Silicium checkout.
#   ./apply.sh <mu_root> <BOOTAA64.EFI> <kernel.bin>
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec python3 "$HERE/embed.py" "$@"
