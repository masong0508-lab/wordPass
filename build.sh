#!/usr/bin/env bash
# Build wordPass.nds with Docker, no devkitPro install needed.
#   ./build.sh            builds ./wordPass.nds
# The image is pinned: ROMs from it boot in DeSmuME, melonDS and on hardware.
set -euo pipefail
cd "$(dirname "$0")"
docker run --rm -v "$PWD":/work -w /work devkitpro/devkitarm:20240511 make
ls -l wordPass.nds
