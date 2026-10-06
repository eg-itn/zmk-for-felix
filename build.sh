#!/usr/bin/env bash
# Build the firmware locally inside the official ZMK Docker image.
# Targets come from build.yaml (see build.py).
#
#   ./build.sh                 # build all targets
#   ./build.sh left reset      # build targets whose name contains "left" or "reset"
#   ./build.sh -p left         # pristine (clean) build
#   ./build.sh --update        # re-run `west update` (e.g. after editing config/west.yml)
set -euo pipefail

IMAGE="${ZMK_IMAGE:-zmkfirmware/zmk-build-arm:stable}"
REPO="$(cd "$(dirname "$0")" && pwd)"

exec docker run --rm ${TERM:+-t} \
    -v "$REPO":/repo -w /repo \
    -u "$(id -u):$(id -g)" \
    -e HOME=/repo/.west-build/.home \
    "$IMAGE" python3 build.py "$@"
