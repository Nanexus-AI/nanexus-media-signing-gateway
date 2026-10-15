#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Run as a child shell; SDK settings do not persist in the caller.
set -euo pipefail
fail() { printf 'build-sdk: %s\n' "$*" >&2; exit 2; }
: "${SDK_ENV:?Set SDK_ENV to the Cortex-A35 SDK environment-setup file}"
: "${TOOLKIT_DIR:?Set TOOLKIT_DIR to the pinned Toolkit checkout}"
: "${ONVIF_PREFIX:?Set ONVIF_PREFIX to an isolated ARM64 installation prefix}"
[[ -f "$SDK_ENV" ]] || fail "SDK environment file missing: $SDK_ENV"
[[ -d "$TOOLKIT_DIR" ]] || fail "Toolkit checkout missing: $TOOLKIT_DIR"
gateway=$(cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$gateway/build/m1"}
# Normalize before sourcing the SDK or changing directories.
TOOLKIT_DIR=$(realpath "$TOOLKIT_DIR")
ONVIF_PREFIX=$(realpath -m "$ONVIF_PREFIX")
BUILD_DIR=$(realpath -m "$BUILD_DIR")
unset LD_LIBRARY_PATH
source "$SDK_ENV"
: "${CC:?SDK did not set CC}" "${CXX:?SDK did not set CXX}" "${SDKTARGETSYSROOT:?SDK did not set SDKTARGETSYSROOT}"
[[ $($CC -dumpmachine) == aarch64* ]] || fail "SDK must target AArch64"
expected_toolkit=e502a2d58a13ec92dc4d4cf4468d9dc9ac30cb8c
[[ $(git -C "$TOOLKIT_DIR" rev-parse HEAD) == "$expected_toolkit" ]] || fail "Toolkit HEAD must be $expected_toolkit"
git -C "$TOOLKIT_DIR" diff --quiet HEAD -- include src || fail "Toolkit include/src differs from tested revision"
mkdir -p "$BUILD_DIR"
if [[ ${BUILD_ONVIF:-0} == 1 ]]; then
  : "${ONVIF_SOURCE:?Set ONVIF_SOURCE to the pinned, patched external checkout}"
  [[ -d "$ONVIF_SOURCE" ]] || fail "ONVIF source missing: $ONVIF_SOURCE"
  ONVIF_SOURCE=$(realpath "$ONVIF_SOURCE")
  [[ $(git -C "$ONVIF_SOURCE" rev-parse HEAD) == cf7785ab993c18d921094e8e505c2c34a1350f28 ]] || fail "ONVIF revision differs from tested baseline"
  git -C "$ONVIF_SOURCE" apply --reverse --check "$gateway/tools/live-signing-proof/patches/onvif-reject-undefined-tag.patch" || fail "Required ONVIF patch not applied"
  # Generate a Meson cross file using the selected SDK, without local paths in source.
  python3 - "$BUILD_DIR/aarch64.cross" <<'PY'
import os, shlex, sys
from pathlib import Path
lines = ['[binaries]']
for name, variable in [('c', 'CC'), ('cpp', 'CXX'), ('ar', 'AR'), ('strip', 'STRIP'), ('pkg-config', 'PKG_CONFIG')]:
    lines.append(f'{name} = {shlex.split(os.environ.get(variable, "pkg-config" if name == "pkg-config" else ""))!r}')
lines += ['', '[built-in options]']
for name, variable in [('c_args', 'CFLAGS'), ('cpp_args', 'CXXFLAGS'), ('c_link_args', 'LDFLAGS'), ('cpp_link_args', 'LDFLAGS')]:
    lines.append(f'{name} = {shlex.split(os.environ.get(variable, ""))!r}')
lines += ['', '[properties]', 'needs_exe_wrapper = true', f'sys_root = {os.environ["SDKTARGETSYSROOT"]!r}', '', '[host_machine]', "system = 'linux'", "cpu_family = 'aarch64'", "cpu = 'cortex-a35'", "endian = 'little'"]
Path(sys.argv[1]).write_text('\n'.join(lines) + '\n')
PY
  # SDK installations may provide meson.real alongside an environment wrapper.
  if [[ -z ${MESON:-} ]]; then
    if command -v meson.real >/dev/null; then MESON=meson.real; else MESON=meson; fi
  fi
  [[ ! -e "$BUILD_DIR/onvif" ]] || fail "ONVIF build directory exists; use a new BUILD_DIR"
  "$MESON" setup "$BUILD_DIR/onvif" "$ONVIF_SOURCE" \
    --cross-file "$BUILD_DIR/aarch64.cross" --prefix="$ONVIF_PREFIX" --libdir=lib \
    --buildtype=release --wrap-mode=nodownload -Dsigningplugin=unthreaded
  ninja -C "$BUILD_DIR/onvif" -j "${BUILD_JOBS:-2}"
  "$MESON" install -C "$BUILD_DIR/onvif"
fi
[[ -f "$ONVIF_PREFIX/include/media-signing-framework/onvif_media_signing_signer.h" ]] || fail "ONVIF headers missing; see BUILD_ONVIF in the guide"
[[ -f "$ONVIF_PREFIX/lib/libmedia-signing-framework.so" ]] || fail "ONVIF ARM64 library missing"
for source_file in signing/stream_signer.cpp upstream/session.cpp core/annexb.cpp core/nal_classify.cpp; do
  [[ -f "$TOOLKIT_DIR/src/$source_file" ]] || fail "Toolkit source missing: $source_file"
done
$CC --version
$CXX --version
pkg-config --modversion gstreamer-1.0 gstreamer-app-1.0 gstreamer-rtp-1.0 openssl
# SDK compiler/flag variables intentionally contain multiple shell words.
$CXX ${CXXFLAGS:-} -std=c++20 -Wall -Wextra -pthread \
  -I"$TOOLKIT_DIR/include" -I"$ONVIF_PREFIX/include/media-signing-framework" \
  $(pkg-config --cflags gstreamer-app-1.0 gstreamer-rtp-1.0 openssl) \
  "$gateway/tools/live-signing-proof/main.cpp" \
  "$TOOLKIT_DIR/src/signing/stream_signer.cpp" "$TOOLKIT_DIR/src/upstream/session.cpp" \
  "$TOOLKIT_DIR/src/core/annexb.cpp" "$TOOLKIT_DIR/src/core/nal_classify.cpp" \
  -L"$ONVIF_PREFIX/lib" -Wl,-rpath,'$ORIGIN' ${LDFLAGS:-} \
  -lmedia-signing-framework $(pkg-config --libs gstreamer-app-1.0 gstreamer-rtp-1.0 openssl) \
  -o "$BUILD_DIR/live-sign-proof"
file "$BUILD_DIR/live-sign-proof"
$READELF -h -l -d --version-info "$BUILD_DIR/live-sign-proof"
