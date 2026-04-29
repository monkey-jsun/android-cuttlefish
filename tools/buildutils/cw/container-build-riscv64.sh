#!/bin/bash
set -x

# Resolve the repository root (three levels up from this script).
REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"

CARGO_MOUNT=""
if [ -f "$HOME/.cargo/config.toml" ]; then
    CARGO_MOUNT="-v=$HOME/.cargo/config.toml:/root/.cargo/config.toml:ro"
fi

docker run --network=host \
        -v="$HOME/.cache/bazel:/root/.cache/bazel" \
        -v="$REPO_ROOT:/mnt/build" \
        $CARGO_MOUNT \
        -w /mnt/build \
        android-cuttlefish-build:riscv64 base
