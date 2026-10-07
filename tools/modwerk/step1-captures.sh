#!/usr/bin/env bash
# Play Modes for modwerk, step 1 on the author's Mac: clone the PR branch,
# build octabam's emulator, capture the popup screens from YOUR tested image.
# Nothing here uploads anything; the image stays on this Mac.
#   bash step1-captures.sh ~/Downloads/octamod/sdk/octabam/out/mainos_bus.bin
set -euo pipefail
IMAGE="${1:?pass the MAIN OS image you flashed (out/mainos_bus.bin of that build)}"
WORK="$HOME/Downloads/modwerk-pr"
HERE="$(cd "$(dirname "$0")" && pwd)"
IMAGE_SHA=$(shasum -a 256 "$IMAGE" | cut -d' ' -f1)
echo "image SHA-256: $IMAGE_SHA"

if [ ! -d "$WORK/.git" ]; then
  git clone --branch add-playmodes https://github.com/devilfish707/octamod "$WORK"
fi
cd "$WORK" && git pull --ff-only

# the emulator: octabam's pinned cores, then ot_emu
cd "$WORK/sdk/octabam"
bash scripts/vendor.sh mc68k dsp56300
source scripts/vendor.sh && stage_dsp_host
cmake -B "$WORK/sdk/octabam/out/emu" -S tools/emu/ot_emu -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$WORK/sdk/octabam/out/emu" -j8 --target ot_emu

cd "$WORK"
rm -rf "$WORK/sdk/octabam/out/captures"
python3 -B scripts/capture-module-ui.py \
  --emulator "$WORK/sdk/octabam/out/emu/ot_emu" \
  --image "$IMAGE" --image-sha256 "$IMAGE_SHA" \
  --plan "$HERE/capture-plan.json" \
  --output "$WORK/sdk/octabam/out/captures"
open "$WORK/sdk/octabam/out/captures"
echo "Done. Look at the two PNGs, then tell Claude the image SHA-256 above."
