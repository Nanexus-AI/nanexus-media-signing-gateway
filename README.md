# Nanexus Media Signing Gateway Reference Platform

An embedded reference prototype for applying ONVIF Media Signing to already-encoded media. The completed M1 demonstration ingests live H.264 over RTSP/TCP, signs it on STM32MP257F-DK without decoding, re-encoding or transcoding, and produces an H.264 Annex-B artifact independently verified by Nanexus Video Trust Toolkit.

![STM32MP257F-DK reference platform](docs/images/stm32mp257f-dk-reference-platform.jpg)

*STM32MP257F-DK reference platform used for the M1 live H.264 signing demonstration.*

## Status and demonstrated capability

**M1 — Signing Gateway Prototype: Complete.** The intended initial release version is **v0.1.0**. This is an experimental reference platform, with evolving interfaces and no production appliance readiness claim.

The bounded demonstration captured **301 access units over 20.03 seconds**, inserted **21 ONVIF Media Signing SEIs**, and returned **VALID** in independent Toolkit verification with a test trust anchor and **zero pending verification frames**.

Exact comparison of the parser-output bytes admitted to the signer against the signed output confirmed **945 original NAL payloads**, including **602 VCL slices**, remained byte-identical and in order apart from added signing metadata. This comparison excludes Annex-B start codes; it does not establish byte identity for all camera or RTP input.

## Architecture and trust boundary

```text
H.264 RTSP/TCP source
→ rtspsrc → rtph264depay → h264parse
→ AU-aligned appsink → Toolkit StreamSigner → ONVIF Media Signing
→ signed H.264 Annex-B file
→ independent Nanexus Video Trust Toolkit verification
```

Transport integration lives in the bounded executable; the reused signing core remains independent of GStreamer. Output is a file, not an RTSP publication endpoint.

The Gateway signature authenticates media observed and signed at the Gateway's signing boundary. It does not establish camera authenticity, original-source identity, integrity before that boundary, or physical/event truth. **Camera/source identity and Gateway signer identity are distinct.** Verification depends on accepted trust anchors; M1 uses test credentials.

## Reference platform and reproduction

Tested on **STM32MP257F-DK**, AArch64/Cortex-A35, 4 GB RAM, using ST OpenSTLinux **5.0.17-openstlinux-6.6-yocto-scarthgap-mpu-v26.06.10**, kernel **6.6.129**, GStreamer **1.22.12** and OpenSSL **3.5.5**. The tested SDK is the OpenSTLinux Cortex-A35 SDK installed as **v6.2.1-stm32mp2-a35**, with GCC/G++ **13.4.0**.

See the [bounded proof guide](tools/live-signing-proof/README.md) for pinned external dependencies, the required ONVIF compatibility patch, cross-build, isolated deployment, fresh test credentials, synthetic RTSP input, capture, verification and cleanup. The build installs no packages and runs in a child shell.

Independent verification uses the Toolkit's `video-trust verify --codec h264 --ca TEST_CA SIGNED_FILE`. The included payload comparison helper checks preservation only; it is not a signature verifier.

## Scope and limitations

The live prototype demonstrates H.264 only. H.265 remains intended scope, without a demonstrated live path here. The experiment uses a bounded, single signing epoch and a controlled source repeating SPS/PPS with IDRs. It has no reconnect/resume or RTSP re-publication behavior.

Production PKI, persistent service lifecycle, watchdog, OTA/rollback, fleet management, production hardening, long-term stability and certification are outside M1. Signed observation timestamps do not establish trusted source capture time; diagnostic sidecars are unsigned.

## Roadmap

| Milestone | Status | Objective and representative areas |
|---|---|---|
| **M1 — Signing Gateway Prototype** | Complete | Demonstrate embedded signing of already-encoded live H.264 and independent verification: RTSP/TCP ingest, encoded-media adapter, signing SEIs, bounded artifact capture, payload preservation. |
| **M2 — Appliance Runtime** | Planned | Establish a maintainable appliance runtime: persistent service model, startup/shutdown lifecycle, configuration, health reporting, logging, supervision/restart behavior. |
| **M3 — Resilience & Recovery** | Planned | Define and validate device/runtime failure detection and recovery: application-health chain, hardware watchdog, fault recovery, abnormal restart handling, recovery semantics. |
| **M4 — Update & Deployability** | Planned | Explore maintainable deployment and recoverable updates: OTA, A/B update, rollback, failed-update recovery, reference packaging, OEM deployment model. |

Roadmap items describe future technical direction and do not imply delivery dates or commitments. M1 is the completed milestone for the intended v0.1.0 release.

## Security, standards and license

Generate disposable, local test-only CA/signer material. Never commit private keys, camera/LAN credentials, credential-bearing archives or raw private diagnostics. RTSP URLs containing credentials can appear in process listings, command history and debug logs; handle them locally and review logs before sharing.

This prototype uses the external [ONVIF Media Signing Framework](https://github.com/onvif/media-signing-framework); it does not claim ONVIF certification or endorsement. Nanexus-owned Gateway source is licensed under [Apache-2.0](LICENSE). Third-party code and dependencies retain their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Contributions should preserve the explicit signing boundary and bounded prototype scope. Interfaces and packaging remain experimental.
