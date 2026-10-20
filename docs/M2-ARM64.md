# M2 ARM64 build, installation and operation

This is an application build on an existing compatible Linux system, not an OS
flashing guide. Commands are a parameterized reconstruction of exercised M2
cross-build/deployment mechanics; this public recipe has been statically checked,
not executed as a new build or deployment. Run only the steps appropriate to your
own test system. Do not overwrite an existing installation or its signing identity.

## Validated environment and prerequisites

- STM32MP257F-DK, Cortex-A35/AArch64, PCB MB1605C.
- ST OpenSTLinux Weston
  `5.0.17-openstlinux-6.6-yocto-scarthgap-mpu-v26.06.10`, kernel 6.6.129;
  OpenSSL 3.5.5, systemd 255 in the tested deployment.
- Official OpenSTLinux Cortex-A35 SDK identified as
  `v6.2.1-stm32mp2-a35`, GCC/G++ 13.4.0, `aarch64-ostl-linux`.
  This installation identifier is not an SDK archive checksum. Obtain the
  compatible official ST SDK yourself; do not substitute a generic ARM compiler
  without checking the target ABI.
- Linux build host with Bash, Git, Python 3, CMake >=3.18, Meson, Ninja,
  pkg-config, and the SDK. Accepted Meson version was 1.3.1. The SDK supplies
  target OpenSSL headers/libraries. SSH/SCP are needed only for operator transfer.

The accepted ELF needs `/lib/ld-linux-aarch64.so.1`, OpenSSL .3, ONVIF .25,
libstdc++.so.6, libgcc_s.so.1 and libc (including GLIBC_2.38 symbols and
GLIBCXX_3.4.32 in that build); libm is part of the transitive runtime closure.
Do not replace target system libraries to force compatibility. Inspect each new
build against the actual target; “AArch64” alone is insufficient. The
[host-native recipe](M2-FOUNDATION.md#pinned-isolated-build) uses the host compiler;
never reuse its objects or ONVIF library for the target build below.

## Source preparation

From the Gateway repository root, acquire official sources as shown in the
[native guide](M2-FOUNDATION.md#pinned-isolated-build). Upstream locations are
<https://github.com/ZLMediaKit/ZLMediaKit> and
<https://github.com/onvif/media-signing-framework>; use ZLM's pinned submodule
checkouts, not their latest branches. All required revisions are:

| Component | Exact revision |
|---|---|
| ZLMediaKit | `08329b39bf36c141b0a12c2bcceb819456632d9b` |
| ZLToolKit | `d7d601200f65f3111dbf9d20ed7f14d2654bbd6e` |
| ONVIF r25.12.6 | `cf7785ab993c18d921094e8e505c2c34a1350f28` |
| jsoncpp | `ca98c98457b1163cca1f7d8db62827c115fec6d1` |
| media-server | `21c4451ff2e4c4bb1c817e606c8b4e5deac1e719` |

Set your own SDK environment file and use a **new absolute build directory**.
The example SDK path is a placeholder; no particular host installation is assumed.

```bash
set -euo pipefail
export SDK_ENV=/path/to/sdk/environment-setup-cortexa35-ostl-linux
export ARM_BUILD="$PWD/build/m2-arm64"
export GATEWAY_SOURCE="$PWD"
export HOST_PYTHON="$(command -v python3)"
test -f "$SDK_ENV"
test ! -e "$ARM_BUILD"
mkdir -p "$ARM_BUILD"
python3 scripts/prepare-m2-dependencies.py \
  --zlm-source build/cache/ZLMediaKit --onvif-source build/cache/onvif \
  --dest "$ARM_BUILD/deps"
```

Preparation selects detached locked commits and applies only the two existing
[patches](../patches/README.md). It refuses an existing dependency destination,
wrong revision, unexpected source diff, staged or untracked source additions.
`--verify` accepts only the approved complete tracked diffs. Do not force patch
application or migrate ONVIF to r26.6.2 because it contains a later fix.

## Cross-build in a child shell

The following child shell keeps SDK variables out of the caller. It generates
its cross file from SDK tools/flags rather than hard-coding a local sysroot.
Use paths supported by the SDK (in particular, SDK compiler variables may not
support installation paths containing spaces). No packages are installed.

```bash
bash <<'BUILD'
set -euo pipefail
unset LD_LIBRARY_PATH
source "$SDK_ENV"
export PKG_CONFIG_LIBDIR="$SDKTARGETSYSROOT/usr/lib/pkgconfig:$SDKTARGETSYSROOT/usr/share/pkgconfig"
cd "$GATEWAY_SOURCE"
test "$($CC -dumpmachine)" = aarch64-ostl-linux
if command -v meson.real >/dev/null; then MESON=meson.real; else MESON=meson; fi

"$HOST_PYTHON" - "$ARM_BUILD/aarch64.cross" <<'CROSS'
import os, shlex, sys
from pathlib import Path
lines = ['[binaries]']
for name, env in [('c', 'CC'), ('cpp', 'CXX'), ('ar', 'AR'),
                  ('strip', 'STRIP'), ('pkg-config', 'PKG_CONFIG')]:
    fallback = 'pkg-config' if name == 'pkg-config' else ''
    lines.append(f'{name} = {shlex.split(os.environ.get(env, fallback))!r}')
lines += ['', '[built-in options]']
for name, env in [('c_args', 'CFLAGS'), ('cpp_args', 'CXXFLAGS'),
                  ('c_link_args', 'LDFLAGS'), ('cpp_link_args', 'LDFLAGS')]:
    lines.append(f'{name} = {shlex.split(os.environ.get(env, ""))!r}')
lines += ['', '[properties]', 'needs_exe_wrapper = true',
          f'sys_root = {os.environ["SDKTARGETSYSROOT"]!r}', '', '[host_machine]',
          "system = 'linux'", "cpu_family = 'aarch64'",
          "cpu = 'cortex-a35'", "endian = 'little'"]
Path(sys.argv[1]).write_text('\n'.join(lines) + '\n')
CROSS

"$MESON" setup "$ARM_BUILD/onvif-build" "$ARM_BUILD/deps/onvif" \
  --cross-file "$ARM_BUILD/aarch64.cross" \
  --prefix "$ARM_BUILD/onvif-prefix" --libdir lib --buildtype release \
  --wrap-mode nodownload -Dsigningplugin=unthreaded

# Stop before compilation if the optional check dependency changes the closure.
"$HOST_PYTHON" - "$ARM_BUILD/onvif-build" <<'CHECK'
import json, sys
from pathlib import Path
b = Path(sys.argv[1])
deps = json.loads((b / 'meson-info/intro-dependencies.json').read_text())
assert not any(d['name'] == 'check' for d in deps), 'check dependency selected'
assert 'OMS_UNIT_TEST' not in (b / 'compile_commands.json').read_text(), 'unit-test build selected'
CHECK

ninja -C "$ARM_BUILD/onvif-build" -j2 libmedia-signing-framework.so.25.12.6
"$MESON" install -C "$ARM_BUILD/onvif-build" --no-rebuild
"$HOST_PYTHON" scripts/prepare-m2-dependencies.py --verify --dest "$ARM_BUILD/deps"

cmake -S . -B "$ARM_BUILD/gateway-build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DNANEXUS_NATIVE=ON \
  -DNANEXUS_DEPS="$ARM_BUILD/deps" -DONVIF_PREFIX="$ARM_BUILD/onvif-prefix" \
  -DONVIF_LIBRARY="$ARM_BUILD/onvif-prefix/lib/libmedia-signing-framework.so" \
  -DPython3_EXECUTABLE="$HOST_PYTHON" \
  -DCMAKE_DISABLE_FIND_PACKAGE_JEMALLOC=TRUE \
  -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON '-DCMAKE_INSTALL_RPATH=$ORIGIN/../lib' \
  -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=OFF
cmake --build "$ARM_BUILD/gateway-build" --target nanexus-gateway --parallel 2
"$HOST_PYTHON" scripts/prepare-m2-dependencies.py --verify --dest "$ARM_BUILD/deps"

mkdir "$ARM_BUILD/stage"
mkdir "$ARM_BUILD/stage/bin" "$ARM_BUILD/stage/lib"
install -m755 "$ARM_BUILD/gateway-build/nanexus-gateway" "$ARM_BUILD/stage/bin/"
install -m755 "$ARM_BUILD/onvif-prefix/lib/libmedia-signing-framework.so.25.12.6" "$ARM_BUILD/stage/lib/"
"$HOST_PYTHON" - "$ARM_BUILD/stage" <<'STRIP'
import os, shlex, subprocess, sys
from pathlib import Path
p = Path(sys.argv[1])
subprocess.run(shlex.split(os.environ['STRIP']) + ['--strip-unneeded',
    str(p / 'bin/nanexus-gateway'),
    str(p / 'lib/libmedia-signing-framework.so.25.12.6')], check=True)
STRIP
ln -s libmedia-signing-framework.so.25.12.6 "$ARM_BUILD/stage/lib/libmedia-signing-framework.so.25"
$READELF -h -l -d --version-info "$ARM_BUILD/stage/bin/nanexus-gateway"
$READELF -h -d "$ARM_BUILD/stage/lib/libmedia-signing-framework.so.25.12.6"
cd "$ARM_BUILD/stage"
sha256sum bin/nanexus-gateway lib/libmedia-signing-framework.so.25.12.6 > SHA256SUMS
BUILD
```

Check ELF architecture, interpreter, NEEDED and RUNPATH against the requirements
above, and confirm the relative SONAME link stays inside `lib/`. The SDK sysroot
provides system libraries; do not copy the sysroot into the application payload.
Record SDK/tool versions, dependency manifest and your generated checksums.

The accepted environments had jemalloc and ONVIF's optional `check` absent.
CMake's existing disable-find-package flag prevents jemalloc auto-linking; the
Meson metadata/compile-command guard stops if `check` was selected. These two
recipe safeguards are statically reviewed additions to the exercised build
mechanics, not newly executed cross-build evidence. If they fail, stop and select
an isolated compatible build environment; do not edit upstream or remove system
packages automatically. No build-driver edit is required for this documented
fail-closed workflow. Host/target tool versions and upstream build timestamps can
change binary hashes: reproducible pins and patches do not promise bit-identical
binaries across machines.

## Operator installation on a compatible target

These commands are instructions, not an installation performed by this repository.
Use a test board you control. Back up any existing application configuration
separately; the sequence refuses an existing application root. Preserve the M1
rollback medium and do not reflash/repartition a working Linux system.

On the host, select your verified SSH identity/host key and target hostname:

```bash
set -euo pipefail
export BOARD=root@board.example.invalid
ssh -o BatchMode=yes -o StrictHostKeyChecking=yes "$BOARD" \
  'set -eu; umask 077; test ! -e /usr/local/nanexus/m2; test ! -L /usr/local/nanexus/m2; mkdir -p /usr/local/nanexus; mkdir /usr/local/nanexus/m2; mkdir /usr/local/nanexus/m2/bin /usr/local/nanexus/m2/lib'
scp -o BatchMode=yes -o StrictHostKeyChecking=yes \
  "$ARM_BUILD/stage/bin/nanexus-gateway" "$BOARD:/usr/local/nanexus/m2/bin/"
scp -o BatchMode=yes -o StrictHostKeyChecking=yes \
  "$ARM_BUILD/stage/lib/libmedia-signing-framework.so.25.12.6" "$BOARD:/usr/local/nanexus/m2/lib/"
scp -o BatchMode=yes -o StrictHostKeyChecking=yes \
  "$ARM_BUILD/stage/SHA256SUMS" "$BOARD:/usr/local/nanexus/m2/"
ssh -o BatchMode=yes -o StrictHostKeyChecking=yes "$BOARD" \
  'set -eu; cd /usr/local/nanexus/m2; sha256sum -c SHA256SUMS; ln -s libmedia-signing-framework.so.25.12.6 lib/libmedia-signing-framework.so.25; umask 077; mkdir config private; chmod 700 . config private'
```

Stop after any error; do not repeat transfer over a partial installation. Inspect
that state before operator cleanup. SCP implementations differ in symlink
handling, so transfer the real files explicitly and create the relative SONAME
link only after checksum verification, as above.
Do not use a global loader-cache update. Validate the loader on your own target
before a camera run (use its dynamic loader's `--list` option on the executable,
and the executable's `--check-config` after provisioning). No ARM program is run
by the cross-build steps.

The intended installed layout is:

```text
/usr/local/nanexus/m2/
  bin/nanexus-gateway
  lib/libmedia-signing-framework.so.25.12.6
  lib/libmedia-signing-framework.so.25 -> libmedia-signing-framework.so.25.12.6
  SHA256SUMS
  config/gateway.conf
  private/camera.auth
  private/signer.key.pem
  private/signer-chain.pem
```

Provision the disposable signer key/chain and camera auth through a protected
operator channel; never put their contents in command arguments, public archives
or logs. Use root-owned directories 0700 and files 0600 for configuration/auth/key/
chain. Do not copy the CA private key to the board. See
[identity preparation](M2-VERIFICATION.md#disposable-test-identity).

Create `config/gateway.conf` locally on the target, using this **syntax example**
with your actual camera endpoint; it contains no credentials:

```ini
camera.url=rtsp://camera.example.invalid:554/stream
camera.auth_file=/usr/local/nanexus/m2/private/camera.auth
output.url=rtsp://0.0.0.0:18554/signed/camera
signing.key_file=/usr/local/nanexus/m2/private/signer.key.pem
signing.certificate_file=/usr/local/nanexus/m2/private/signer-chain.pem
```

Omit camera.auth_file when no camera authentication is needed. That file contains
exactly two nonempty LF-terminated lines, username then password (<=2048 bytes,
no CR/NUL). Never embed credentials in the URL. Relative file references resolve
against the working directory; absolute paths above avoid ambiguity. Supported
keys/limits and strict syntax are in the [configuration contract](M2-FOUNDATION.md#configuration-and-operation).

## Foreground operation and receiver

With camera and network already available, run on the target:

```bash
cd /usr/local/nanexus/m2
./bin/nanexus-gateway --check-config config/gateway.conf
./bin/nanexus-gateway --config config/gateway.conf
```

Syntax validation does not open keys or prove camera availability. Standing
operation omits `--duration`; a bounded run can append `--duration 20`. The same
process actively ingests, signs and publishes. Listener startup follows camera
admission, including in-band SPS/PPS+IDR. Do not start another MediaServer or an
ONVIF daemon. Start Frigate/another receiver afterward and connect to
`rtsp://board.example.invalid:18554/signed/camera` using the board's real reachable
address. `0.0.0.0` is a bind address, not a receiver destination; `127.0.0.1` in the
shipped example permits only local clients. Scope the unauthenticated output to a
controlled network; the prototype supplies no output RTSP authentication/TLS.

Preserve signing-bearing receiver evidence and [verify independently](M2-VERIFICATION.md).
Do not stop a standing Gateway just to manufacture a complete receiver result.
SIGINT/SIGTERM deliberately finalizes pending signing data once and permits a
bounded drain. A source/media/signing failure exits nonzero without clean EOS.
An observed media_gap exit required operator recovery; no reconnect or continuity
repair is implemented. Before any operator restart, preserve failure evidence and
recognize the new signing epoch. Local EOS/exit success cannot prove downstream
receipt or retention; MP4 closure is not ONVIF EOS.

## Optional systemd startup

First validate foreground operation on your target, then stop **that exact
foreground process** normally before service installation. Preserve your current
startup arrangement before enabling another service. The optional
[unit example](examples/nanexus-m2-gateway.service) uses the layout above,
SIGTERM, bounded shutdown and `Restart=no`. It is an illustrative generalization
of the exercised board service, not a new tested unit.

It orders after `network-online.target`; this works only if your existing network
manager's wait-online implementation is enabled and actually waits for the camera
interface. The accepted board instead used a specific systemd-networkd routable
IPv4 interface wait. Adapt that dependency to your system before enabling the
example; no interface name or manager is assumed here. Camera must already be
reachable. Arbitrary boot ordering and automatic recovery are not promised.

Before copying or enabling the example, inspect the effective service definition
and all existing overrides on your target. These are read-only queries:

```bash
systemctl show nanexus-m2-gateway.service -p LoadState -p FragmentPath -p DropInPaths
systemctl cat nanexus-m2-gateway.service
systemd-analyze unit-paths
```

An absent unit may make `systemctl cat` return nonzero; distinguish that expected
absence from permission, service-manager or other errors. Inspect matching unit
files, symlinks and drop-in directories in the reported search paths, including
administrator, runtime and vendor locations (for example `/etc/systemd/system`,
`/run/systemd/system` and `/usr/lib/systemd/system`). Use read-only file metadata
and symlink inspection to establish ownership and purpose; consult the existing
administrator or package records when appropriate. Do not assume that absence
from `/etc` means no unit exists elsewhere. If a unit, override or conflicting
startup arrangement exists, **stop for review rather than overwrite or shadow
it**. Proceed only after confirming there is no existing configuration to displace.
The checks below are additional destination guards, not a complete search-path audit.

Operator commands after completing that review and provisioning files:

```bash
# Target terminal; obtain the example through your normal reviewed transfer.
set -eu
test ! -e /etc/systemd/system/nanexus-m2-gateway.service
test ! -L /etc/systemd/system/nanexus-m2-gateway.service
test ! -e /etc/systemd/system/nanexus-m2-gateway.service.d
install -m644 nanexus-m2-gateway.service /etc/systemd/system/nanexus-m2-gateway.service
systemd-analyze verify /etc/systemd/system/nanexus-m2-gateway.service
systemctl daemon-reload
systemctl enable --now nanexus-m2-gateway.service
systemctl status --no-pager nanexus-m2-gateway.service
journalctl -u nanexus-m2-gateway.service -n 30 --no-pager
```

Do not overwrite an existing unit/drop-in or enable after verification fails.
Service active/running does not establish valid media; independently verify the
stream. An automatic cold startup was demonstrated on the tested board with the
camera already online; this template is not universal boot qualification.

For rollback, stop/disable only this newly installed service, restore your recorded
prior startup arrangement and leave validated application/configuration files in
place for later use. Do not delete credentials/binaries routinely or alter the
preserved M1 medium. No reboot/power test is required by this guide's installation
steps; schedule any such test explicitly with the operator.
