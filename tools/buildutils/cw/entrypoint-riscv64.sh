#!/usr/bin/env bash
#
# Entrypoint for android-cuttlefish-build:riscv64 (driven by
# tools/buildutils/build-cf-riscv64.sh).
#
# Runs as root at container start: creates a user matching the host
# caller's UID/GID/name with proper /etc/passwd + /etc/shadow entries
# (so sudo + PAM work), grants NOPASSWD sudo, then exec-drops via gosu
# to run the supplied command unprivileged.
#
# Inputs (env vars from the build script):
#   USER_UID   numeric UID                  (default 1000)
#   USER_GID   numeric GID                  (default 1000)
#   USER_NAME  user/group name              (default "builder")
#   HOME       home dir to assign           (default /home/$USER_NAME)

set -e

UID_TARGET=${USER_UID:-1000}
GID_TARGET=${USER_GID:-1000}
NAME=${USER_NAME:-builder}
HOME_DIR=${HOME:-/home/$NAME}

# Create the group if its GID isn't already taken.
if ! getent group "$GID_TARGET" >/dev/null; then
    groupadd -g "$GID_TARGET" "$NAME"
fi

# Create the user if its UID isn't already taken.  -M skips home-dir
# creation -- we handle it below to avoid clobbering bind mounts.
if ! getent passwd "$UID_TARGET" >/dev/null; then
    useradd \
        -u "$UID_TARGET" \
        -g "$GID_TARGET" \
        -M \
        -d "$HOME_DIR" \
        -s /bin/bash \
        "$NAME"
fi

# Make sure HOME exists and belongs to the user.  chown is non-recursive
# on purpose: HOME may have bind-mounted subtrees (e.g. ~/.cache/bazel)
# that already have correct ownership from the host filesystem.
mkdir -p "$HOME_DIR"
chown "$UID_TARGET:$GID_TARGET" "$HOME_DIR"

# Grant the user NOPASSWD sudo.  build_package.sh:76 runs
# `sudo mk-build-deps -i ...` which expects to elevate without a prompt.
SUDOERS_FILE=/etc/sudoers.d/$NAME-nopasswd
echo "$NAME ALL=(ALL) NOPASSWD: ALL" > "$SUDOERS_FILE"
chmod 440 "$SUDOERS_FILE"

# Drop privileges and run the command.
exec gosu "$UID_TARGET:$GID_TARGET" "$@"
