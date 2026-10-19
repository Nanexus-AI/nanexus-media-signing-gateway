# Native H.264 signing Gateway

M2 replaces the M1 application architecture. The native build implements one
H.264/TCP camera → bounded complete-AU admission → official ONVIF signing → signed
RTSP publication. There is no decoding/transcoding, GStreamer or FFmpeg media
framework dependency. Toolkit remains an independent verifier. The K11 functional
milestone includes bounded real-camera, independent receiver and stock Frigate
recording validation. Those observations do not qualify M2 on ARM64, other cameras,
industrial reliability or discontinuous motion-retention scenarios. Internal
validation artifacts are kept outside the product tree.

## Pinned isolated build

Requirements: Linux C/C++14 toolchain, CMake >=3.18, Python 3, Meson, Ninja,
pkg-config and OpenSSL >=3 development files. No package installation or network
acquisition occurs in the product build. Use local source caches containing the
exact revisions in `cmake/dependencies.lock.json`:

| Component | Revision |
|---|---|
| ZLMediaKit | `08329b39bf36c141b0a12c2bcceb819456632d9b` |
| ZLToolKit | `d7d601200f65f3111dbf9d20ed7f14d2654bbd6e` |
| Official ONVIF (r25.12.6) | `cf7785ab993c18d921094e8e505c2c34a1350f28` |
| jsoncpp | `ca98c98457b1163cca1f7d8db62827c115fec6d1` |
| media-server | `21c4451ff2e4c4bb1c817e606c8b4e5deac1e719` |

For a new cache, obtain the upstream repositories separately:

```sh
git clone https://github.com/ZLMediaKit/ZLMediaKit.git build/cache/ZLMediaKit
git -C build/cache/ZLMediaKit checkout --detach 08329b39bf36c141b0a12c2bcceb819456632d9b
git -C build/cache/ZLMediaKit submodule update --init 3rdpart/ZLToolKit 3rdpart/jsoncpp 3rdpart/media-server
git clone https://github.com/onvif/media-signing-framework.git build/cache/onvif
git -C build/cache/onvif checkout --detach cf7785ab993c18d921094e8e505c2c34a1350f28
```

Prepare new copies from committed objects, without modifying caches. Preparation
refuses an existing destination. It checks exact revisions and whole tracked diffs,
applies only the two documented patches, rejects staged/untracked source edits,
and prints a revision/patch-hash manifest:

```sh
python3 scripts/prepare-m2-dependencies.py --zlm-source build/cache/ZLMediaKit --onvif-source build/cache/onvif --dest build/m2/deps
meson setup build/m2/onvif-build build/m2/deps/onvif --prefix "$PWD/build/m2/onvif-prefix" --libdir lib --buildtype release --wrap-mode nodownload -Dsigningplugin=unthreaded
ninja -C build/m2/onvif-build -j2
meson install -C build/m2/onvif-build
python3 scripts/prepare-m2-dependencies.py --verify --dest build/m2/deps
cmake -S . -B build/m2/native -DCMAKE_BUILD_TYPE=Release -DNANEXUS_NATIVE=ON -DNANEXUS_DEPS="$PWD/build/m2/deps" -DONVIF_PREFIX="$PWD/build/m2/onvif-prefix"
cmake --build build/m2/native -j2
ctest --test-dir build/m2/native --output-on-failure
```

CMake verifies prepared source identities/diffs during configuration. Verify again
before incremental builds after any manual source changes. Build ONVIF from the
verified source using these instructions; an arbitrary installed `.so` is not
provenance. Local build RPATH locates the explicit ONVIF prefix; no system library
installation or loader-cache changes are required. Do not reuse host objects in
future ARM cross-builds.

Pinned ZLM's existing MP4-enabled FLV/MOV helper closure is retained because
ext-codec includes those headers; this does not add a recording or transcoding
workflow. Unused executables/features and reporting are disabled. Full upstream
trees and binaries stay in ignored build directories. See [patch provenance](../patches/README.md)
and [third-party notices](../THIRD_PARTY_NOTICES.md). Publication remains subject to
license/attribution/source-relinking review, including ZLToolKit LGPL Base64.

The default `NANEXUS_NATIVE=OFF` build supports configuration validation and
portable guard tests without media dependencies; runtime invocation reports that
native adapters were not built. It is not a functional Gateway build.

## Configuration and operation

Copy `config/gateway.example.conf` to `config/gateway.local.conf` (ignored). Fields:

| Field | Meaning |
|---|---|
| camera.url | Required RTSP/TCP input, one H.264 video track |
| camera.auth_file | Optional external local credential file |
| output.url | Required owned RTSP listener endpoint, app/stream path |
| signing.key_file | Required local PEM signer private key |
| signing.certificate_file | Required matching PEM chain including test trust anchor |

The auth file contains exactly two LF-terminated lines: username, then password.
Each must be nonempty; no CR, embedded LF or NUL; whole file <=2048 bytes. Use an
ignored `*.auth` file with restrictive local permissions. It is read into upstream
per-player options, never embedded in a URL or logged. Private key and certificate
reads are capped at 64 KiB each. Key buffer is cleared on all exits; credentials
remain in the upstream player for session authentication. No identity is shipped.
The official key API's manufacturer-provisioned slot is used with supplied test
material; this is not evidence of camera authenticity or production PKI.

Use strict `key=value`, one per line, surrounding spaces trimmed. Blank lines and
whole-line `#` comments are allowed; no quoting, interpolation or inline comments.
Unknown, duplicate, empty and missing fields fail. Configuration is bounded to
16 KiB, each line to 2048 bytes. URLs require `rtsp://HOST:PORT/PATH`; DNS/IPv4,
explicit port 1–65535 and simple paths only. Inline credentials, query strings,
TLS and IPv6 are unsupported. Output host must be locally bindable; app and stream
must both be nonempty. File references resolve from the working directory.

| Optional field (`limits.` prefix) | Default | Allowed |
|---|---:|---:|
| startup_timeout_ms | 10000 | 1–60000 |
| gap_timeout_ms | 2000 | 1–60000 |
| drain_timeout_ms | 1000 | 1–10000 |
| max_signing_frames | 15 | 1–300 |
| max_nal_bytes | 1048576 | 1–8388608 |
| max_au_bytes | 8388608 | 1–8388608 |
| max_nals | 4096 | 1–4096 |

NAL limit cannot exceed AU limit. Signed AU accounting includes four-byte framing
per NAL and generated SEIs, so leave metadata headroom below the AU ceiling.
Limits are not a total-process RSS guarantee. One current and one pending original
AU are retained, with temporary bounded signed/encoder copies; no full-run history.
Decoder fragmentation is bounded before upstream input; SEI counts/returned sizes
are checked before application growth. The library allocates generated SEIs
internally before returning them, so this is not a custom allocator guarantee.
A fixed 512-entry upstream source ring and stock socket send timeouts remain;
slow-reader/multi-client resource qualification is outside this experiment scope.

```sh
build/m2/native/nanexus-gateway --check-config config/gateway.local.conf
build/m2/native/nanexus-gateway --config config/gateway.local.conf --duration 20
```

Startup order: Camera → Gateway → Frigate. Duration is 1–600 wall-clock seconds
from first admitted AU, with up to the configured drain wait added. Without
`--duration`, run until SIGINT/SIGTERM. No reconnect or retry. Startup deadline
includes waiting for admissible in-band SPS/PPS+IDR. Upstream verbose logs are
suppressed because they can contain credentials; fixed runtime reason codes and
counts are emitted without URLs or private paths. Check-config does not open
keys, contact a camera or prove endpoint readiness. Invalid config exits 1;
usage/non-native runtime exits 2; finalized experimental runtime exits 0;
initialization/media/signing failures exit 1.

## Media and signing behavior

Input is progressive H.264 I/P-only, with one complete picture per RTP timestamp/
marker group. Require in-band SPS/PPS with startup IDR; ignore unselected audio.
Discard the initial partial group through its marker. Reject detected packet gaps,
SSRC changes, timestamp discontinuity, malformed RTP/STAP/FU, incomplete fragments,
B/SP/SI slices, partitions and interlaced SPS. FU identity/size/count are checked
before stock H264RtpDecoder input. Marker and NAL-count agreement gates admission.
This is a bounded supported-input guard, not a complete arbitrary-H264 conformance
parser. Configure the camera accordingly; unsupported/ambiguous layouts may fail.
Raw RTP time is accumulated with wrap-safe deltas into wider relative ticks, mapped
to milliseconds, with DTS=PTS under the no-B restriction. No timing is repaired.

One serialized, RAII-owned official context signs all admitted AUs independently
of readers. Before every original NAL: peek/drain generated SEIs, feed inserted
SEIs to signing, then submit the original. Original raw NAL bytes/order remain
unchanged; transport framing may differ. No H264Track media submission, parameter
set insertion, AUD removal, decode or transcode occurs. H264Track is used only to
construct SDP. Only the private signed-AU encoder entry point is used.

One complete AU stays pending. Deliberate termination signs it, invokes EOS once,
null-peek drains trailers without feeding them back, and publishes the combined
final AU. Any partial current input group is excluded. Source loss/gap/signing
failure aborts without clean EOS; no restart or synthetic continuity. After a
partial signing failure the context is discarded and affected AU is unpublished.
Publication failures can still leave partial transport output; no false success
is asserted.

The RTSP source is producer-owned and uses stock RTSP low-latency cache flushing.
After final publication it remains alive for `drain_timeout_ms`, then the process
reports `eos_emitted=1` and `recording_completeness=unknown`. This is a bounded
opportunity for delivery, not a socket acknowledgement or archival guarantee.
Stopping before any admitted AU returns failure with no EOS. No fault-tolerant
shutdown state machine is provided.

## Focused evidence tests

CTest covers strict configuration and sanitized packetization/admission guards.
For signing and positive AU tests, provide a local unsigned Annex-B synthetic
fixture plus a five-column AU manifest (`offset bytes pts_ms dts_ms duration_ms`),
and disposable test key/chain. The test also crosses the RTP timestamp wrap and
checks the concrete publisher's source cache without a downstream RTSP client:

```sh
mkdir -p build/m2/test-output
build/m2/native/native-media-tests INPUT.h264 AU-MANIFEST.tsv TEST.key.pem TEST-chain.pem build/m2/test-output
video-trust verify --codec h264 --ca TEST-CA.pem build/m2/test-output/signed.h264
video-trust verify --codec h264 --ca TEST-CA.pem build/m2/test-output/reconstructed.h264
```

The fixture corpus, identities, outputs and reports remain local; they are not
bundled with product source. The test accepts <=1000 AUs, each <=8 MiB. Independent
Toolkit status is evidence about that input/output and selected trust anchor,
not camera truth or downstream retained-recording completeness. Publisher, retained
MP4 and any separately recovered cache suffix require distinct coverage and
completeness assessments. A valid observed suffix is not full source coverage.
