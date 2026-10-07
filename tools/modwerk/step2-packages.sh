#!/usr/bin/env bash
# Play Modes for modwerk, step 2 on the author's Mac: the firmware packages
# (Docker toolchain) and the comparison with native octabam (your own OS).
# Needs: Node 24, Docker running (colima start), the official OS update file.
#   bash step2-packages.sh ~/Downloads/OCTATRACK_OS1.40C_dist/OCTATRACK_OS1.40C.bin
set -euo pipefail
OS_FILE="${1:?pass your official OCTATRACK_OS1.40C.bin}"
WORK="$HOME/Downloads/modwerk-pr"
OUT="$HOME/Downloads/modwerk-pr-packages"
cd "$WORK"
git fetch -q origin && git reset -q --hard origin/add-playmodes

node -v | grep -q '^v2[4-9]' || { echo "Node 24 needed: brew install node@24 && export PATH=\"$(brew --prefix)/opt/node@24/bin:\$PATH\""; exit 1; }
docker info > /dev/null 2>&1 || { echo "Docker is not running: colima start"; exit 1; }
npm ci --no-audit --no-fund

# 1. the pinned toolchain image (once; a few minutes)
docker image inspect modwerk-source-tools > /dev/null 2>&1 \
  || docker build --file sdk/build/Dockerfile --tag modwerk-source-tools .
image=$(docker image inspect modwerk-source-tools --format '{{.Id}}')

# 2. packages from this clean commit, imported
rm -rf "$OUT"
bash scripts/build-modules-isolated.sh . "$OUT" "$image"
npm run modules:import -- "$OUT/packages" --development
npm run modules:generate

# 3. native comparison on your own OS file (stays in ~/.cache/modwerk-native)
npm run module:verify -- playmodes --os "$OS_FILE"

# 4. checks, and a bundle of what changed for Claude to commit
npm run check || true
npm run module:doctor -- playmodes || true
git status --porcelain --untracked-files=all | awk '{print $2}' > /tmp/playmodes-step2-files.txt
tar -czf "$HOME/Downloads/playmodes-step2.tgz" -T /tmp/playmodes-step2-files.txt
echo "Done: ~/Downloads/playmodes-step2.tgz ($(wc -l < /tmp/playmodes-step2-files.txt) files). Tell Claude."
