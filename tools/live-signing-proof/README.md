# Bounded live H.264 signing proof

Experimental single-epoch adapter: RTSP/TCP → `rtspsrc` → `rtph264depay` → `h264parse` → byte-stream/AU `appsink` → Toolkit StreamSigner / ONVIF → Annex-B files. No decoder, encoder, transcoder or RTSP output exists on this signing path.

## Tested baseline

| Component | Tested baseline |
|---|---|
| Board | STM32MP257F-DK, Cortex-A35/AArch64, 4 GB RAM |
| Target OS | ST OpenSTLinux 5.0.17-openstlinux-6.6-yocto-scarthgap-mpu-v26.06.10 |
| Kernel | 6.6.129 |
| SDK | OpenSTLinux Cortex-A35 SDK, installation identifier v6.2.1-stm32mp2-a35 |
| Compiler | GCC/G++ 13.4.0; aarch64-ostl-linux |
| GStreamer / OpenSSL | 1.22.12 / 3.5.5 |
| Nanexus Video Trust Toolkit | `e502a2d58a13ec92dc4d4cf4468d9dc9ac30cb8c` |
| ONVIF Media Signing Framework | r25.12.6, `cf7785ab993c18d921094e8e505c2c34a1350f28`, plus the patch below |

The Toolkit pin is recorded by the successful M1 proof's pre-run revision snapshot. SDK installation identifiers are not an SDK archive checksum; obtain the matching official ST Cortex-A35 SDK for the target baseline. Other environments are unvalidated. The cleaned-up public reproduction recipe was successfully rerun on the STM32MP257F-DK reference platform before the intended v0.1.0 release.

Host requirements: Bash, Git, Python 3, Meson, Ninja, pkg-config, file/readelf through the SDK, SSH/SCP, and the official SDK. Source generation additionally needs FFmpeg with libx264 and a MediaMTX RTSP server (tested 1.15.3). The target needs the SDK-compatible runtime and `rtspsrc`, `rtph264depay`, `h264parse`, `appsink` plugins. Install tools yourself; the build script installs no packages.

## External checkouts and ONVIF patch

From the Gateway root, choose workspace locations. Examples keep generated files under ignored `build/`; external checkouts remain separate.

```sh
export GATEWAY_DIR="$PWD"
export TOOLKIT_DIR="/path/to/nanexus-video-trust-toolkit"
export ONVIF_SOURCE="/path/to/media-signing-framework"
export SDK_ENV="/path/to/sdk/environment-setup-cortexa35-ostl-linux"
export BUILD_DIR="$GATEWAY_DIR/build/m1"
export ONVIF_PREFIX="$GATEWAY_DIR/build/onvif-prefix"
```

Obtain the Toolkit from `https://github.com/Nanexus-AI/nanexus-video-trust-toolkit` and ONVIF from `https://github.com/onvif/media-signing-framework`. Use dedicated clean checkouts, then select the pins:

```sh
git -C "$TOOLKIT_DIR" checkout --detach e502a2d58a13ec92dc4d4cf4468d9dc9ac30cb8c
git -C "$ONVIF_SOURCE" checkout --detach cf7785ab993c18d921094e8e505c2c34a1350f28
git -C "$ONVIF_SOURCE" apply --check \
  "$GATEWAY_DIR/tools/live-signing-proof/patches/onvif-reject-undefined-tag.patch"
git -C "$ONVIF_SOURCE" apply \
  "$GATEWAY_DIR/tools/live-signing-proof/patches/onvif-reject-undefined-tag.patch"
```

The small MIT-licensed [patch](patches/onvif-reject-undefined-tag.patch) explicitly rejects `UNDEFINED_TAG` in the TLV decoder. It preserves the compatibility fix present in the successful M1 baseline, which that upstream revision lacks. It is not a dependency upgrade or an independently established security advisory. Keep the upstream license and [Gateway third-party notices](../../THIRD_PARTY_NOTICES.md); do not vendor the framework.

## Cross-build

```sh
BUILD_ONVIF=1 bash tools/live-signing-proof/build-sdk.sh
```

The script sources the selected SDK in a child shell, checks AArch64 and dependency revisions, creates a Meson cross file from SDK variables, builds the patched ONVIF unthreaded shared library with two jobs, installs it in `ONVIF_PREFIX`, and compiles the bounded adapter with selected Toolkit sources. No full Toolkit live-ingest target is built; that target's newer GStreamer requirement is separate from this adapter's tested 1.22.12 APIs.

Use a new `BUILD_DIR` for each ONVIF build. `MESON` can select the host Meson executable; the default prefers `meson.real` if available. `BUILD_JOBS` selects ONVIF parallelism. To reuse your already built, pinned and patched ARM64 prefix:

```sh
BUILD_ONVIF=0 bash tools/live-signing-proof/build-sdk.sh
```

In reuse mode, you are responsible for prefix provenance; the script checks required files but cannot prove a binary's source revision. Paths containing spaces in SDK compiler/flag variables may require the SDK's own supported path conventions. Inspect the emitted ELF/dependency information before deployment.

## Fresh disposable test identity

Run on the host in a new local directory; never use production keys. This generates fresh material rather than copying repository fixtures. The unencrypted signer key is for unattended disposable testing only.

```sh
mkdir -m 700 "$BUILD_DIR/test-pki"
(
  umask 077
  cd "$BUILD_DIR/test-pki"
  openssl req -x509 -newkey rsa:2048 -nodes -sha256 -days 2 \
    -subj '/CN=Disposable Gateway Test CA' \
    -addext 'basicConstraints=critical,CA:TRUE' \
    -addext 'keyUsage=critical,keyCertSign,cRLSign' \
    -keyout ca.key.pem -out ca.pem
  openssl req -newkey rsa:2048 -nodes -sha256 \
    -subj '/CN=Disposable Gateway Test Signer' \
    -keyout signer.key.pem -out signer.csr.pem
  cat > signer.ext <<'EXT'
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature
EXT
  openssl x509 -req -sha256 -days 2 -in signer.csr.pem \
    -CA ca.pem -CAkey ca.key.pem -CAcreateserial \
    -extfile signer.ext -out signer.cert.pem
  cat signer.cert.pem ca.pem > signer-chain.pem
)
```

Keep the CA private key on the host. Deploy only the signer key and certificate chain, with restricted permissions. Ensure host/target clocks are valid for the certificates. Test credentials establish a test signer trust relationship, not camera authenticity.

## Controlled synthetic RTSP source

On a test host reachable by the board, run MediaMTX 1.15.3 with a local config, TCP RTSP on port 8554, and unused protocols disabled. For example, save this under your ignored build directory:

```yaml
rtsp: yes
rtspAddress: :8554
rtspTransports: [tcp]
rtmp: no
hls: no
webrtc: no
srt: no
paths:
  m1:
    source: publisher
```

Run `mediamtx /path/to/local-config.yml` in a test network. This example has no authentication and must be scoped to that network. In another terminal:

```sh
ffmpeg -hide_banner -nostdin -re \
  -f lavfi -i testsrc2=size=640x360:rate=15 -t 180 -an \
  -c:v libx264 -threads 2 -preset ultrafast -tune zerolatency \
  -pix_fmt yuv420p -g 15 -keyint_min 15 -sc_threshold 0 -bf 0 \
  -x264-params repeat-headers=1 \
  -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8554/m1
```

Source encoding occurs on the test host, outside the Gateway signing path. Use `rtsp://SOURCE_HOST:8554/m1` from the board. The source must repeat SPS/PPS with IDRs; one complete startup AU must contain all three. No camera credentials are required. If you use a credential-bearing RTSP URL, it can appear in process listings, shell history and debug logs; keep it local and do not share unreviewed diagnostics.

## Isolated deployment and bounded capture

Choose a task directory on the board's reference test storage (the proof used SD). Do not change system packages, global loader configuration or service lifecycle. Replace the documentation-only address below with your actual board SSH address. The task directory must be new and reside on your selected test storage:

```sh
export BOARD=root@192.0.2.10
ssh "$BOARD" 'umask 077; mkdir -p /usr/local/nanexus; mkdir /usr/local/nanexus/m1-proof'
scp "$BUILD_DIR/live-sign-proof" \
  "$ONVIF_PREFIX/lib/libmedia-signing-framework.so.25" \
  "$BUILD_DIR/test-pki/signer.key.pem" "$BUILD_DIR/test-pki/signer-chain.pem" \
  "$BOARD:/usr/local/nanexus/m1-proof/"
```

SCP copies the shared library contents under its SONAME filename. Record and compare SHA-256 hashes before/after transfer. On the board:

```sh
cd /usr/local/nanexus/m1-proof
chmod 600 signer.key.pem signer-chain.pem
LD_LIBRARY_PATH="$PWD" ./live-sign-proof \
  rtsp://SOURCE_HOST:8554/m1 signer.key.pem signer-chain.pem capture 20
```

The executable requires a **new output directory** and a 15–30 second capture window. It waits for SPS/PPS/IDR, admits complete AUs, and finalizes pending signing metadata at deliberate completion. Application checks cover a 45-second loop deadline, five-second input stall, ten-second startup window, 8 MiB/AU, 4096 NALs/AU, and 32 MiB each of unsigned/signed media. They do not prove a hard process termination deadline, total memory ceiling, or production watchdog behavior.

Continuity faults, stream changes, gap/corruption/discontinuity, unexpected EOS, source restart, stall or cancellation abort the epoch. Initial establishment/DISCONT is permitted. There is no reconnect/resume policy. A failed capture's partial files must not be represented as a complete result; require exit 0 and `summary.txt` with `state=finalized`.

Outputs:

- `unsigned.h264`: exact admitted parser output.
- `signed.h264`: original NALs plus signing SEIs.
- `timing.csv`: AU timing/flags/byte ranges and Gateway UTC observations; unsigned diagnostic data. Missing GStreamer times use `GST_CLOCK_TIME_NONE` (18446744073709551615).
- `summary.txt`: completion state and counts. Final metadata trailers follow the last AU's recorded output range.

## Independent verification and preservation

Copy that same capture back to a local ignored output location. On an independent host, build the pinned Toolkit's `video-trust` CLI using its [public build instructions](https://github.com/Nanexus-AI/nanexus-video-trust-toolkit/blob/e502a2d58a13ec92dc4d4cf4468d9dc9ac30cb8c/docs/build.md) and the same patched ONVIF baseline. The Gateway script produces an ARM64 signer, not this host verifier. Select your actual CLI path:

```sh
export VIDEO_TRUST=/path/to/host/video-trust
"$VIDEO_TRUST" verify --codec h264 --ca "$BUILD_DIR/test-pki/ca.pem" \
  "$BUILD_DIR/capture/signed.h264"
"$VIDEO_TRUST" inspect --codec h264 --ca "$BUILD_DIR/test-pki/ca.pem" \
  --json "$BUILD_DIR/capture/signed.h264"
python3 tools/live-signing-proof/check-preservation.py \
  "$BUILD_DIR/capture/unsigned.h264" "$BUILD_DIR/capture/signed.h264" \
  "$BUILD_DIR/capture/timing.csv"
```

Require verification `VALID`, complete verification and zero pending frames. Source authenticity remains `not_established`. The payload helper must independently return `PASS`/exit 0. It checks the exact same admitted H.264 stream and sidecar ranges, excluding start codes, and removes only recognized ONVIF UUID signing SEIs. It is tailored to unsigned input and the tested metadata layout; it is **not a signature verifier** or general SEI validator. Structural UUID recognition alone is not evidence of authenticity.

The prior demonstration had 301 AUs over 20.03 seconds, 945 original NAL payloads/602 VCL slices preserved and 21 signing SEIs added. New runs need their own comparison and verification; those counts are not required outcomes.

## Cleanup and scope

Stop your task-owned publisher/server and confirm the Gateway process has exited. Remove task signer keys/certificates, executable/library, capture and logs from the board. Delete disposable local private keys and credential-bearing staging archives when no longer needed. Keep any needed diagnostics locally; never commit generated media, private keys or raw credential-bearing logs. Use local paths you have verified before deletion.

This proves the Gateway's observed signing boundary for a bounded H.264 experiment. H.265, camera authenticity, pre-Gateway integrity, event truth, RTSP output, recovery, service/watchdog/OTA behavior and production PKI remain outside the demonstrated capability.
