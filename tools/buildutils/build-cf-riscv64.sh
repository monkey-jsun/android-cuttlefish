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
#   tools/buildutils/build-cf-riscv64.sh //a:target [@ext//b:target ...]
#                                                    # build just those bazel
#                                                    # targets
#   tools/buildutils/build-cf-riscv64.sh --dev //a:target
#                                                    # same, but in a reusable
#                                                    # container so bazel keeps
#                                                    # its analysis cache warm
#   tools/buildutils/build-cf-riscv64.sh --dev-stop  # discard that container
#
# --dev is for iteration only.  Release artifacts come from the no-argument
# form, which always runs in a fresh --rm container so nothing accumulated
# during development can leak into them.
#
# The image (android-cuttlefish-build:riscv64) is a riscv64 Debian trixie
# environment -- runs natively on a riscv64 host and under qemu-user
# binfmt emulation on a non-riscv64 host.  bazel and clang execute as
# riscv64 ELF either way, so this is a native build (slow on x86_64 due
# to qemu, fast on riscv64 silicon).  Mirrors the rv-build workflow.
#
# The image is rebuilt on every invocation.  Docker's layer cache makes
# this a no-op when nothing has changed in Containerfile.riscv64.
#
# Host prereq on non-riscv64 hosts: qemu-user binfmt for riscv64 must be
# registered with the 'C' (credentials/setuid) flag so sudo and other
# setuid binaries inside the container can elevate.  The script checks
# this and refuses to start if missing.
#
# Runtime user: the container runs as $(id -u):$(id -g) with the host's
# /etc/passwd + /etc/group bind-mounted in, so the process appears as
# the calling host user.  Files written into bind mounts land owned by
# the caller on the host.
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
DEV_MODE=0
DEV_STOP=0
DEV_CONTAINER=cf-build
TARGETS=()
for arg in "$@"; do
    case "$arg" in
        --shell) SHELL_MODE=1 ;;
        --dev) DEV_MODE=1 ;;
        --dev-stop) DEV_STOP=1 ;;
        //*|@*) TARGETS+=("$arg") ;;
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

# Non-riscv64 hosts run the riscv64 image under qemu-user.  Verify
# qemu-riscv64 binfmt is registered with the 'C' flag (without it,
# sudo/setuid inside the container can't elevate -- mk-build-deps
# fails immediately).  riscv64 hosts run native and skip the check.
HOST_ARCH=$(uname -m)
if [ "$HOST_ARCH" != "riscv64" ]; then
    BINFMT=/proc/sys/fs/binfmt_misc/qemu-riscv64
    if [ ! -f "$BINFMT" ]; then
        cat >&2 <<EOF
ERROR: qemu-user binfmt for riscv64 not registered.
  Install qemu-user-static and binfmt-support on this host (e.g.
  \`apt install qemu-user-static binfmt-support\`), then re-run.
EOF
        exit 1
    fi
    if ! grep -q '^flags:.*C' "$BINFMT"; then
        cat >&2 <<EOF
ERROR: qemu-riscv64 binfmt is registered but missing the 'C' flag.
  Without 'C', sudo and other setuid binaries inside the riscv64
  container can't elevate -- mk-build-deps fails immediately.  Add
  the flag (e.g. via an override in /etc/binfmt.d/qemu-riscv64.conf)
  and re-run.  Current flags:
$(grep '^flags:' "$BINFMT" | sed 's/^/    /')
EOF
        exit 1
    fi
fi

echo "[build-cf-riscv64] building/refreshing image $IMAGE_NAME..."
# --platform=linux/riscv64 forces docker to pull the riscv64 manifest
# regardless of host arch.  On x86_64 hosts this is necessary (without
# it, docker looks for an amd64 manifest in riscv64/debian and errors).
# On riscv64 hosts it's a no-op (already native).
docker build \
    --platform=linux/riscv64 \
    -f "$DOCKERFILE" \
    -t "$IMAGE_NAME" \
    "$(dirname "$DOCKERFILE")"

mkdir -p "$CACHE_DIR"

# Runtime: pass UID/GID/name + HOME to the entrypoint, which creates
# the matching user inside the container and drops privileges via gosu.
# Bind-mount the repo at the same path inside and outside the container
# so cwd-based and absolute paths stay stable.  Remap the persistent
# bazel cache to the bazel default location ($HOME/.cache/bazel inside
# the container) so no --output_user_root threading is needed.
DOCKER_RUN_ARGS=(
    --rm
    --platform=linux/riscv64
    -e USER_UID="$(id -u)"
    -e USER_GID="$(id -g)"
    -e USER_NAME="$USER"
    -e HOME="$HOME"
    -v "$REPO_ROOT:$REPO_ROOT"
    -v "$CACHE_DIR:$HOME/.cache/bazel"
    -w "$REPO_ROOT"
)

if [ "$DEV_STOP" -eq 1 ]; then
    if docker rm -f "$DEV_CONTAINER" >/dev/null 2>&1; then
        echo "[build-cf-riscv64] removed $DEV_CONTAINER"
    else
        echo "[build-cf-riscv64] no $DEV_CONTAINER container"
    fi
    exit 0
fi

if [ "$DEV_MODE" -eq 1 ]; then
    if [ "${#TARGETS[@]}" -eq 0 ]; then
        echo "--dev builds specific targets; pass at least one //target." >&2
        echo "Release artifacts come from the no-argument form." >&2
        exit 1
    fi

    # Recreate the container whenever the image content it was started from
    # has changed, so a Containerfile edit is never silently ignored.
    # Compare layer digests rather than image IDs: buildkit stamps a fresh
    # creation time on every build, so the ID differs even when every layer
    # was a cache hit.
    want_image=$(docker image inspect --format '{{.RootFS.Layers}}' "$IMAGE_NAME")
    have_image=$(docker inspect --format '{{.Image}}' "$DEV_CONTAINER" 2>/dev/null \
        | xargs -r docker image inspect --format '{{.RootFS.Layers}}' 2>/dev/null || true)
    if [ "$want_image" != "$have_image" ]; then
        if [ -n "$have_image" ]; then
            echo "[build-cf-riscv64] image changed, recreating $DEV_CONTAINER..."
        fi
        docker rm -f "$DEV_CONTAINER" >/dev/null 2>&1 || true
        # Same arguments as the ephemeral path, without --rm so the bazel
        # server survives between builds.
        DEV_RUN_ARGS=()
        for a in "${DOCKER_RUN_ARGS[@]}"; do
            [ "$a" = "--rm" ] || DEV_RUN_ARGS+=("$a")
        done
        docker run -d --name "$DEV_CONTAINER" "${DEV_RUN_ARGS[@]}" \
            "$IMAGE_NAME" sleep infinity >/dev/null
        # The entrypoint creates the user after the container is up, and under
        # qemu that takes a moment.  Exec'ing before it lands gives
        # "sudo: you do not exist in the passwd database".
        for _ in $(seq 1 120); do
            if docker exec "$DEV_CONTAINER" getent passwd "$(id -u)" \
                >/dev/null 2>&1; then
                break
            fi
            sleep 1
        done
        echo "[build-cf-riscv64] started $DEV_CONTAINER"
    fi

    echo "[build-cf-riscv64] building ${TARGETS[*]} in $DEV_CONTAINER..."
    exec docker exec -i -u "$(id -u):$(id -g)" -w "$REPO_ROOT" -e HOME="$HOME" \
        "$DEV_CONTAINER" bash -c '
        set -e
        . /etc/cargo-bazel-env
        # Build-deps persist with the container, so install them only once.
        if [ ! -e "$HOME/.cf-build-deps-installed" ]; then
            cd base
            sudo mk-build-deps -i -t "apt-get -o Debug::pkgProblemResolver=yes --no-install-recommends -y"
            cd ..
            touch "$HOME/.cf-build-deps-installed"
        fi
        tools/buildutils/cf-bazel-build.sh "$@"
    ' _ "${TARGETS[@]}"
fi

if [ "$SHELL_MODE" -eq 1 ]; then
    echo "[build-cf-riscv64] entering interactive shell in $IMAGE_NAME..."
    exec docker run -it "${DOCKER_RUN_ARGS[@]}" "$IMAGE_NAME" bash
fi

if [ "${#TARGETS[@]}" -gt 0 ]; then
    echo "[build-cf-riscv64] building ${TARGETS[*]}..."
    exec docker run -i "${DOCKER_RUN_ARGS[@]}" "$IMAGE_NAME" bash -c '
        set -e
        . /etc/cargo-bazel-env
        # Bazel actions compile against system headers (libopus, libssl, ...)
        # that the image does not carry, so install the build-deps first --
        # the same step the deb build runs.
        cd base
        sudo mk-build-deps -i -t "apt-get -o Debug::pkgProblemResolver=yes --no-install-recommends -y"
        cd ..
        tools/buildutils/cf-bazel-build.sh "$@"
    ' _ "${TARGETS[@]}"
fi

echo "[build-cf-riscv64] running cuttlefish-base deb + cvd_host_riscv64 tarball..."
exec docker run -i "${DOCKER_RUN_ARGS[@]}" "$IMAGE_NAME" bash -c '
    set -e
    # Non-interactive bash skips /etc/bash.bashrc, so source the cargo-bazel
    # env vars (URL + SHA256) explicitly.  debian/rules on riscv64 requires
    # CARGO_BAZEL_GENERATOR_URL.
    . /etc/cargo-bazel-env
    tools/buildutils/build_package.sh base
    tools/buildutils/cf-bazel-build.sh \
        //cuttlefish/package/cvd_host_riscv64:cvd_host_package_riscv64
'
