#!/usr/bin/env bash
#
# Build cuttlefish artifacts for riscv64 inside a hermetic docker
# container.  Produces, at the source-tree root:
#   - cuttlefish-base_*_riscv64.deb (and 4 sibling .debs)
#   - cvd_host_package_riscv64.tar.gz
#
# Usage:
#   tools/buildutils/build-cf-riscv64.sh             # build both artifacts
#   tools/buildutils/build-cf-riscv64.sh --shell     # interactive shell in
#                                                    # the same container
#                                                    # for ad-hoc dev work
#
# The container image (android-cuttlefish-build:riscv64) is rebuilt on
# every invocation.  Docker's layer cache makes this a no-op when nothing
# has changed in tools/buildutils/cw/Containerfile.riscv64 or its inputs.
#
# Runtime user: the container runs as $(id -u):$(id -g) with the host's
# /etc/passwd + /etc/group bind-mounted in, so the process appears as the
# calling host user (including username and home dir).  Files written
# into bind mounts land owned by the caller on the host.  One image works
# for any host user.
#
# Bazel's action cache lives at <repo>/.bazel-cache/ on the host,
# bind-mounted into the container at the bazel default location
# ($HOME/.cache/bazel inside the container).  Already gitignored via the
# top-level .* rule.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

IMAGE_NAME="android-cuttlefish-build:riscv64"
DOCKERFILE="$REPO_ROOT/tools/buildutils/cw/Containerfile.riscv64"
CACHE_DIR="$REPO_ROOT/.bazel-cache"

SHELL_MODE=0
for arg in "$@"; do
    case "$arg" in
        --shell) SHELL_MODE=1 ;;
        -h|--help)
            sed -n '/^# Usage:/,/^#$/p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *)
            echo "unknown argument: $arg" >&2
            echo "run with --help for usage." >&2
            exit 1
            ;;
    esac
done

echo "[build-cf-riscv64] building/refreshing image $IMAGE_NAME..."
docker build \
    -f "$DOCKERFILE" \
    -t "$IMAGE_NAME" \
    "$(dirname "$DOCKERFILE")"

mkdir -p "$CACHE_DIR"

# Runtime: run as the calling host user.  Bind-mount /etc/passwd and
# /etc/group (read-only) so the process is recognized inside the
# container with the right username and home dir.  Bind-mount the repo
# at the same path inside and outside the container so cwd-based and
# absolute paths stay stable.  Remap the persistent bazel cache to the
# bazel default location ($HOME/.cache/bazel) inside the container, so
# no --output_user_root threading is needed.
DOCKER_RUN_ARGS=(
    --rm
    --user "$(id -u):$(id -g)"
    -v /etc/passwd:/etc/passwd:ro
    -v /etc/group:/etc/group:ro
    -v "$REPO_ROOT:$REPO_ROOT"
    -v "$CACHE_DIR:$HOME/.cache/bazel"
    -e HOME="$HOME"
    -w "$REPO_ROOT"
)

if [ "$SHELL_MODE" -eq 1 ]; then
    echo "[build-cf-riscv64] entering interactive shell in $IMAGE_NAME..."
    exec docker run -it "${DOCKER_RUN_ARGS[@]}" "$IMAGE_NAME" bash
fi

echo "[build-cf-riscv64] running cuttlefish-base deb + cvd_host_riscv64 tarball..."
exec docker run -i "${DOCKER_RUN_ARGS[@]}" "$IMAGE_NAME" bash -c '
    set -e
    tools/buildutils/build_package.sh base
    tools/buildutils/cf-bazel-build.sh \
        //cuttlefish/package/cvd_host_riscv64:cvd_host_package_riscv64
'
