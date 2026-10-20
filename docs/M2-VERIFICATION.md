# Test identity and independent verification

Toolkit is an independent receiver/file verifier, not part of the Gateway runtime.
Use its [build instructions](https://github.com/Nanexus-AI/nanexus-video-trust-toolkit/blob/main/docs/build.md)
and [CLI documentation](https://github.com/Nanexus-AI/nanexus-video-trust-toolkit/blob/main/docs/usage.md).
Select a Toolkit build supporting `verify` and `inspect` for H.264 and record its
source revision/tool version when reproducing an observation. These external docs
may evolve; they do not change the Gateway's pinned build inputs. The file commands
below were exercised in M2 validation. No Toolkit binary or signing identity is
shipped here.

## Disposable test identity

This OpenSSL pattern was used in the historical public proof workflow. Generate
fresh local material, not a copy of another user's test key. A newly generated
identity with the documented public recipe has not been rerun as part of this
candidate's static review. It is for a controlled experiment, not production PKI.

From the repository root on a host with OpenSSL 3:

```bash
set -euo pipefail
mkdir -p build/m2
mkdir -m700 build/m2/test-pki
(
  set -eu
  umask 077
  cd build/m2/test-pki
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

Refuse an existing test-pki directory; do not overwrite identities to reuse a
command. Keep the CA private key on the host. Provision only signer.key.pem and
signer-chain.pem to the Gateway's protected private directory. The public ca.pem
is supplied independently to the verifier; never treat an arbitrary embedded
certificate as your trust decision. Check host/target time and two-day certificate
validity. The unencrypted signer key enables unattended testing only; permissions
must restrict access. No production key-management claim follows.

## Receiver evidence

Start the configured camera and Gateway as described in the [ARM64 guide](M2-ARM64.md).
The standing Gateway uses no --duration limit. Frigate/another receiver connects
independently; Gateway does not contain a Frigate destination setting or push into
Frigate. Preserve originals and hashes before extracting verification inputs.

An existing stock Frigate recording MP4 can be analyzed with the sample-extraction
example below. For a small operator-controlled observation, this external FFmpeg
stream-copy command is an **illustrative receiver**, not the exact custom receiver
used in the ARM64 acceptance or a newly qualified preservation toolchain:

```bash
mkdir -p build/m2/observation
ffmpeg -n -hide_banner -nostdin -rtsp_transport tcp \
  -i rtsp://board.example.invalid:18554/signed/camera \
  -map 0:v:0 -c:v copy -an -t 25 -f mp4 build/m2/observation/received.mp4
sha256sum build/m2/observation/received.mp4
```

Replace the documentation hostname with your board address. FFmpeg belongs to
the external receiver, not the Gateway. A 25-second media limit does not impose a
hard connection/read timeout: an operator must stop a stalled receiver. Keep
Gateway running. Confirm your receiver's actual codec, signing SEIs and Toolkit
result; do not presume it preserved all source bytes or EOS. Prefer original
Frigate recordings when studying Frigate boundaries. No special FFmpeg/NVR build
is supplied or required by the Gateway.

For a normal unencrypted H.264 MP4 with length-prefixed video samples, this
**documentation example** follows the direct sample-extraction method exercised
in the Frigate studies. It adds only Annex-B start codes, does not inject avcC
parameter sets, synthesize EOS, repair timestamps or filter SEIs. It preserves
sample NAL order in the selected file, not independently observed camera bytes.
It requires an external `ffprobe` and Python 3. Use a new output filename:

```bash
python3 - build/m2/observation/received.mp4 build/m2/observation/received.h264 <<'EXTRACT'
import json, subprocess, sys
from pathlib import Path
source, target = map(Path, sys.argv[1:])
assert source != target and not target.exists(), 'refuse output overwrite'
p = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
    '-show_packets', '-show_streams', '-of', 'json', str(source)],
    capture_output=True, text=True, check=True, timeout=30)
probe = json.loads(p.stdout)
stream = probe['streams'][0]
assert stream['codec_name'] == 'h264'
length = int(stream['nal_length_size'])
assert length in (1, 2, 3, 4)
nals = []
with source.open('rb') as f:
    for packet in probe['packets']:
        f.seek(int(packet['pos']))
        raw = f.read(int(packet['size']))
        assert len(raw) == int(packet['size'])
        offset = 0
        while offset < len(raw):
            assert offset + length <= len(raw)
            size = int.from_bytes(raw[offset:offset + length], 'big')
            offset += length
            assert size > 0 and offset + size <= len(raw)
            nals.append(raw[offset:offset + size])
            offset += size
uuid = bytes.fromhex('005bc93f2d715e95ada4796f90877a6f')
sei_count = sum((n[0] & 31) == 6 and uuid in n for n in nals)
assert sei_count > 0, 'no recognized ONVIF signing SEI; stop and inspect receiver'
with target.open('xb') as f:
    for nal in nals:
        f.write(b'\x00\x00\x00\x01' + nal)
print('NALs:', len(nals), 'ONVIF UUID SEIs:', sei_count)
EXTRACT
sha256sum build/m2/observation/received.h264
```

UUID presence is a structural observation, not a signature verdict. This small
example holds the bounded observation's NALs in memory; use it only for short
files, not unbounded archives. Do not concatenate independent signing epochs or
silently reorder files to improve a result. Preserve each original MP4 unchanged;
label a chronological multi-file Annex-B input as derived evidence.

## Independent file verification

On the verifier host, use its own Toolkit executable and the matching public test
CA (paths below assume the disposable identity above):

```bash
# Nonzero evidence statuses must not suppress inspect or discard diagnostics.
set +e
video-trust verify --codec h264 --ca build/m2/test-pki/ca.pem --json \
  build/m2/observation/received.h264 \
  > build/m2/observation/verify.json 2> build/m2/observation/verify.stderr
verify_status=$?
video-trust inspect --codec h264 --ca build/m2/test-pki/ca.pem --json \
  build/m2/observation/received.h264 \
  > build/m2/observation/inspect.json 2> build/m2/observation/inspect.stderr
inspect_status=$?
set -e
printf 'verify exit=%s; inspect exit=%s\n' "$verify_status" "$inspect_status"
cat build/m2/observation/verify.json build/m2/observation/inspect.json
```

Keep stdout/stderr and exit codes even when nonzero. The tested CLI reports 0 for
VALID, 1 for INVALID and 4 for unresolved/PARTIAL evidence; inspect the actual JSON,
not exit status alone. Configuration/operational errors must be reported separately.
Check signature_integrity, continuity, verification_completeness, certificate
status, key-change flags and pending frames/NALs. Test signer provenance is not
camera/source authenticity.

- A bounded, deliberately finalized ARM64 receiver observation verified **VALID**,
  complete and zero pending. This proves that observed input under its trust anchor,
  not all camera media or future recorder coverage.
- Arbitrary boundaries in a standing stream often leave a pending suffix:
  **PARTIAL / integrity ok / continuity intact** can be the actual result. Inspect
  for genuine INVALID signatures/interior gaps; do not automatically excuse every
  PARTIAL or relabel it VALID. Future adjacent signing metadata may close an earlier
  file tail; pending counts from separately verified files must not simply be summed.
- In the completed native retention study, all ten gap-spanning sequences were
  **INVALID / not_ok / broken**, while separately observed sides were PARTIAL/ok/
  intact. Missing retained time is not proof of malicious tampering. Exact upstream
  failure mechanism and publisher-to-file missing-NAL identity were not established.
- Local Gateway EOS, receiver finalization, MP4 finalization and archival coverage
  are different. EOS can reach a recorder cache without appearing in retained files.
  Adding an observed cache suffix for analysis does not turn it into a retained file
  or prove complete source coverage.

## Optional synthetic component tests

The existing native-media-tests accepts an unsigned H.264 Annex-B fixture, a
five-column AU manifest and disposable identity, as described in the
[native guide](M2-FOUNDATION.md#focused-evidence-tests). Accepted controlled tests
verified signing, NAL preservation, packetization/reconstruction and EOS, with
independent VALID results. The fixture was a specific locally generated synthetic
input; this release does not ship it or a public generator for the matching AU
manifest. Do not substitute arbitrary H.264 bytes and assume valid AU indexing.
A complete self-contained fixture-generation recipe remains unavailable; no new
generator is invented here. The smallest usable alternative is the short
receiver/retained-file verification path above, with its explicit preservation and
boundary limits. CTest guard success alone is not the positive signing test.
