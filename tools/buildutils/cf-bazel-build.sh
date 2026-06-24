#!/usr/bin/env bash
#
# Run `bazel build` with the EXACT flag set base/debian/rules:96 uses for the
# deb build, so compile-action keys match and the deb's ~3 GB riscv64-opt
# bazel cache is reused instead of triggering a full C++ tree recompile.
# Use this for any standalone bazel build against this workspace -- e.g.,
# secure_env or cvd_host_package_riscv64 for cvd-host_package assembly.
#
# Why we don't strip in bazel: bazel's `--strip` flag is part of the
# configuration hash, so any value other than the deb's `--strip=never`
# invalidates every cached action.  We instead keep `--strip=never` here
# (matching the deb) and let downstream consumers strip externally if they
# need it -- e.g., the cvd_host_riscv64 target has a post-bazel genrule
# that strips the tarball's ELF files (mirroring what dh_strip does in
# the deb pipeline).
#
# Usage (from anywhere in the repo or absolute path):
#
#   tools/buildutils/cf-bazel-build.sh <bazel-target> [bazel-target...]
#
# Static flags mirror rules:96 directly.  Dynamic flags (CFLAGS / CXXFLAGS
# / LDFLAGS) come from dpkg-buildflags and become --conlyopt / --cxxopt /
# --linkopt entries.  No DEB_BUILD_OPTIONS=noopt/dbg handling -- this is
# strictly about cache reuse for the normal opt build.
#
# License: same as the rest of android-cuttlefish (Apache 2.0).

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Invoke dpkg-buildflags from base/ (where debian/ lives) so its output matches
# the deb build's exactly.  From any other directory dpkg-buildflags's
# -ffile-prefix-map / -fdebug-prefix-map values differ -- and they end up in
# every compile action's key, breaking action-cache reuse with the deb's
# riscv64-opt cache.  Then cd to base/cvd for bazel itself (workspace root).
cd "$REPO_ROOT/base"
CONLY=(); CXX=(); LDX=()
for f in $(dpkg-buildflags --get CFLAGS);   do CONLY+=(--conlyopt="$f"); done
for f in $(dpkg-buildflags --get CXXFLAGS); do CXX+=(--cxxopt="$f"); done
for f in $(dpkg-buildflags --get LDFLAGS);  do LDX+=(--linkopt="$f"); done
LDX+=(--linkopt=-Wl,--build-id=sha1)

# Locate cuttlefish-base*.deb at the repo root if present, and pass its
# absolute path to bazel actions via --action_env.  The cvd_host_riscv64
# tarball target reads CF_DEB_PATH from its environment; targets that don't
# care about the deb just ignore it.  (debuild writes its .deb outputs
# at the parent of the source-package directory, i.e. <repo>/, not <repo>/base/.)
ACTION_ENV=()
DEB_PATH=$(ls "$REPO_ROOT"/cuttlefish-base_*_riscv64.deb 2>/dev/null | head -1)
if [ -n "$DEB_PATH" ]; then
    ACTION_ENV+=(--action_env=CF_DEB_PATH="$(readlink -f "$DEB_PATH")")
fi

cd "$REPO_ROOT/base/cvd"

exec bazel build \
    -c opt \
    --strip=never \
    --copt=-gdwarf-4 \
    --spawn_strategy=local \
    --workspace_status_command=../stamp_helper.sh \
    --build_tag_filters=-clang-tidy \
    "${CONLY[@]}" "${CXX[@]}" "${LDX[@]}" \
    "${ACTION_ENV[@]}" \
    "$@"
