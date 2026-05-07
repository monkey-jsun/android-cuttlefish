#!/usr/bin/env bash
#
# Run `bazel build` with the same flag set base/debian/rules:96 uses for the
# deb build.  Use this for any standalone bazel build against this workspace
# (e.g., secure_env for cvd-host_package assembly) so its action keys match
# the deb build's, and the deb build's bazel cache (~3 GB of riscv64-opt
# artifacts) is reused instead of forcing a full recompile of the C++ tree.
#
# Usage (from anywhere in the repo or absolute path):
#
#   tools/buildutils/bazel-cf-build.sh <bazel-target> [bazel-target...]
#
# Static flags mirror rules:96 directly.  Dynamic flags (CFLAGS / CXXFLAGS /
# LDFLAGS) come from dpkg-buildflags and become --conlyopt / --cxxopt /
# --linkopt entries.  No DEB_BUILD_OPTIONS=noopt/dbg handling -- this is
# strictly about cache reuse for the normal opt build.
#
# License: same as the rest of android-cuttlefish (Apache 2.0).

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT/base/cvd"

CONLY=(); CXX=(); LDX=()
for f in $(dpkg-buildflags --get CFLAGS);   do CONLY+=(--conlyopt="$f"); done
for f in $(dpkg-buildflags --get CXXFLAGS); do CXX+=(--cxxopt="$f"); done
for f in $(dpkg-buildflags --get LDFLAGS);  do LDX+=(--linkopt="$f"); done
LDX+=(--linkopt=-Wl,--build-id=sha1)

exec bazel build \
    -c opt \
    --strip=never \
    --spawn_strategy=local \
    --workspace_status_command=../stamp_helper.sh \
    --build_tag_filters=-clang-tidy \
    "${CONLY[@]}" "${CXX[@]}" "${LDX[@]}" \
    "$@"
