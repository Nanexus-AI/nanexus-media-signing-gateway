# Four-channel native Gateway

The four-channel extension uses the v0.2.0 **native ZLMediaKit + official ONVIF**
media path. It does not use GStreamer. Build with `NANEXUS_NATIVE=ON` using the
existing [host](M2-FOUNDATION.md) or [ARM64](M2-ARM64.md) instructions and the same
pinned dependencies/patches. The default non-native build only checks configuration.

## Configuration and invocation

Start from [the four-channel example](../config/gateway-four-channel.example.conf).
`[gateway]` accepts `signing.key_file`, `signing.certificate_file`, and the existing
`limits.*` keys. `[channel.cam1]` through `[channel.cam4]` accept `camera.url`,
optional `camera.auth_file`, and `output.url`. Declare one to four channels.
Credentials, key bounds, RTSP URL syntax, H.264 restrictions, and media limits
remain as documented for the single-channel runtime. Keep local private copies
in ignored `config/*.local.conf` files; paths resolve against the working directory.
The file remains bounded to 16 KiB and lines to 2048 bytes.

```sh
nanexus-gateway --check-config config/gateway-four.local.conf
nanexus-gateway --check-config config/gateway-four.local.conf --channel cam2
nanexus-gateway --config config/gateway-four.local.conf --channel cam2
# A bounded foreground observation may append --duration 20.
```

Full checking validates every channel and requires distinct listener ports, even
if host spellings differ. A selected worker validates shared configuration and
its own channel; an invalid URL, missing/duplicate/unknown field in another channel
cannot prevent its startup. Structural errors (bad/duplicate section names,
mixed formats, file bounds) and shared-identity/limit errors fail the file.
A conflicting listener port also fails affected workers. Errors never echo values.
A multi-channel invocation requires `--channel`; the CLI does not launch child
processes. Existing unsectioned single-channel files and commands remain supported.

Four default NVR URLs, substituting the reachable Gateway address:

```text
rtsp://GATEWAY:18554/signed/camera1
rtsp://GATEWAY:18555/signed/camera2
rtsp://GATEWAY:18556/signed/camera3
rtsp://GATEWAY:18557/signed/camera4
```

The example binds loopback. Change each output host to an appropriate local bind
address for remote clients. A bind address such as `0.0.0.0` is not an NVR destination.
Output remains unauthenticated RTSP: use the existing controlled-network deployment.

## Independent services

Install a verified ARM64 executable and ONVIF library in a separate root:

```text
/usr/local/nanexus/four-channel/
  bin/nanexus-gateway
  lib/libmedia-signing-framework.so.25.12.6
  lib/libmedia-signing-framework.so.25 -> libmedia-signing-framework.so.25.12.6
  config/gateway.conf
  private/signer.key.pem
  private/signer-chain.pem
  private/camera1.auth   # optional, separately provisioned per input
```

Use protected directories/files as in the ARM64 guide. Keep the CA private key off
the device. All workers reference the same supplied signer key/chain; each creates
its own independent ONVIF context. This identity belongs to the Gateway and does
not prove camera capture identity. Per-channel names live in configuration/logs;
this extension does not add cryptographically bound camera provenance metadata.

The [template](examples/nanexus-four-channel@.service) runs one worker per process.
The [target](examples/nanexus-four-channel.target) starts four configured channels.
Review existing units, overrides, identities and occupied ports before installation;
never overwrite a working single-channel installation. Install these new files
under `/etc/systemd/system/` only after foreground and ABI checks. Check syntax
with `systemd-analyze verify`, then reload the service manager. With files installed:

```sh
systemctl start nanexus-four-channel.target
systemctl status nanexus-four-channel@cam2.service
journalctl -u nanexus-four-channel@cam2.service --no-pager -n 30
systemctl restart nanexus-four-channel@cam2.service
systemctl stop nanexus-four-channel.target
```

The target example starts all four instances; with fewer channels start only the
configured instances instead. Enable boot operation separately after validation,
after reviewing the network setup. Workers order after `network.target` and retry
independently while the network or camera is unavailable; they do not pull in a
global wait-online job that could block all four channels. No reboot
or firmware change is needed to install this application extension.

Each failed worker restarts after five seconds. Deliberate systemd stop remains
stopped; a successful bounded `--duration` run also stays stopped. Recovery creates
a new signing context and a new stream session: do not concatenate independent
epochs and claim continuity. This also means a camera unavailable at boot retries
independently. Configuration/identity failures retry too; logs expose the fixed
failure reason until an operator repairs the configuration or stops that instance.

The template applies `MemoryMax=128M`, `CPUQuota=40%`, and a 15-second shutdown
deadline per worker. They are initial reference-board budgets, not universal
performance specifications. Changing one channel's configuration requires restarting
that channel. Changing the shared identity requires planned restarts of all channels.
Keep service/worker failure, signature integrity, stream continuity, and receiver
recording completeness as separate observations.

Process/cgroup isolation contains worker crashes and bounded resource faults.
The device's OS, network, power and signing identity remain shared. No claim of
absolute isolation against kernel/hardware failure or hostile privileged processes
is made. There is no common RTSP output proxy to turn one worker crash into a
four-stream outage.

## Local verification

CTest exercises legacy/new parsing, selection, malformed-field isolation, and
native RTP/admission guards. Positive signing and network tests require explicitly
provided local tools and disposable test identity:

```sh
python3 tests/four_channel_live.py prepare build/four-fixtures
python3 tests/four_channel_live.py host \
  build/m2/native/nanexus-gateway build/four-fixtures \
  /absolute/path/to/video-trust build/four-host-test --seconds 20
```

Preparation uses external FFmpeg to encode four distinguishable 1080p/15fps
I/P-only fixtures, approximately 2 Mbps, GOP 30 with repeated SPS/PPS, and creates
a two-day test identity. This is test-source encoding on the host; Gateway performs
no encoding/transcoding. Output directories must not already exist.
The live test needs Docker access and a locally cached MediaMTX 1.15.3 image
(`32ecec42d5c7` in the exercised lab), local TCP ports 18654 and 18554–18557, and
one healthy independent receiver per channel. It refuses existing evidence and
container-name conflicts, owns its test processes/container, and cleans them up.
It does not install or stop board services or existing Frigate containers.

`tests/rtsp_receive.py` is the independent bounded RTSP/TCP evidence receiver.
It checks sequence/SSRC/fragment/AU boundaries, preserves original RTP and Annex-B
NALs, and supports `--seconds`, `--wall`, and byte ceilings. A stopped standing
capture can have unresolved signing metadata at the tail; use both Toolkit verify
and inspect and preserve their actual results. See [verification semantics](M2-VERIFICATION.md).
Four retained Frigate recordings must be evaluated independently; an MP4 file
boundary is not a Gateway EOS, and a recorder result does not prove full camera
coverage. Four real-camera compatibility and long-duration unattended reliability
require separate qualification.
