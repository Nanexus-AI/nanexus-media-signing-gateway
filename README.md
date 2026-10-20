# Nanexus Media Signing Gateway

**An embedded gateway for adding ONVIF Media Signing to existing IP cameras.**

Many installed IP cameras can stream H.264 video over RTSP but do not support cryptographic media signing. Nanexus Media Signing Gateway demonstrates how these cameras can gain ONVIF-based media-signing capabilities through an external gateway, without replacing the cameras or modifying their firmware.

The project also provides a standards-based signing source for developers and researchers evaluating how NVR/VMS systems preserve, record, and verify signed video. It offers a practical reference for exploring the integration of media signing into existing video-surveillance workflows.

The gateway receives an existing camera's H.264 RTSP stream, adds ONVIF Media Signing information, and republishes the signed video over RTSP for recording, monitoring, or independent verification. It operates on already-encoded video, without decoding or transcoding the original stream.

**Current release: v0.2.0 — Functional Prototype**

## How It Works

```text
                    Nanexus Media Signing Gateway
                   ┌─────────────────────────────┐
                   │                             │
 IP Camera         │   RTSP Ingest               │         NVR / VMS
 (H.264 / RTSP) ──►│        ↓                    │────────► Recording
                   │   ONVIF Media Signing       │
                   │        ↓                    │         Independent
                   │   Signed RTSP Output        │────────► Verification
                   │                             │
                   └─────────────────────────────┘
```

The gateway sits between an existing camera and downstream systems.

It does not require the camera to implement ONVIF Media Signing. Instead, signing is performed at the gateway, using its own configured signing identity.

The resulting stream can be consumed by RTSP-compatible software, while ONVIF Media Signing-compatible verification tools, including the [Nanexus Video Trust Toolkit](https://github.com/Nanexus-AI/nanexus-video-trust-toolkit), can independently verify the signed video and inspect its signing evidence.

Signing authenticates video from the gateway's signing boundary onward; it does not establish the authenticity of the camera's original capture.

## Embedded Reference Platform

![STM32MP257F-DK reference platform](docs/images/stm32mp257f-dk-reference-platform.jpg)

The functional prototype has been implemented and tested on the **STMicroelectronics STM32MP257F-DK**, running a compatible OpenSTLinux environment.

The device operates as a standalone RTSP signing gateway: it connects to an existing camera, signs the incoming video, and makes the resulting stream available to downstream receivers over the network.

The implementation combines:

- **ZLMediaKit** for RTSP input and output, pinned to commit `08329b39bf36c141b0a12c2bcceb819456632d9b`.
- **The official ONVIF Media Signing Framework** for signing, pinned to release `r25.12.6` (commit `cf7785ab993c18d921094e8e505c2c34a1350f28`).
- **Nanexus Gateway components** for integration, configuration, signing workflow, and runtime control.

ZLMediaKit's ZLToolKit dependency is also pinned to the validated commit `d7d601200f65f3111dbf9d20ed7f14d2654bbd6e`. The source preparation process applies the required Nanexus patches to the exact upstream revisions.

These components operate within one Gateway executable. No separate ZLMediaKit MediaServer or ONVIF signing daemon is required.

## What the Prototype Provides

**Signing for existing cameras**

A compatible H.264 RTSP camera can provide video to the gateway without native media-signing support.

**Signed RTSP output**

The gateway adds ONVIF signing metadata while retaining the admitted encoded video payloads, and publishes the result as an RTSP stream.

**Independent verification**

The signed stream can be recorded and subsequently examined using an independent ONVIF-compatible verification workflow, including the [Nanexus Video Trust Toolkit](https://github.com/Nanexus-AI/nanexus-video-trust-toolkit).

**Embedded standalone operation**

The gateway runs on the STM32MP257F-DK under Linux, with a persistent service configuration demonstrated on the reference platform.

**Integration with existing recording systems**

The prototype has been exercised with Frigate, a widely used open-source NVR. The experiments demonstrate that signing information can survive ordinary MP4 recording segmentation and remain available for independent historical verification.

## What Has Been Demonstrated

The v0.2.0 implementation has been tested with a real Tapo C210 H.264 RTSP camera, on both a Linux development host and the STM32MP257F-DK reference platform.

The completed work includes successful camera ingestion, signing, signed RTSP publication, and independent verification of the resulting media.

Additional integration experiments with Frigate confirmed that ordinary recording-file boundaries can preserve signing continuity. Tests involving intentionally unretained recording intervals also demonstrated that missing signing history can be detected during cross-gap verification.

These results establish the core functionality of an operational embedded signing-gateway prototype. They do not imply compatibility with every camera, NVR, video format, or recording policy.

More detailed implementation and validation information is provided in the technical documentation.

## Build, Install, and Verify

This project is distributed as **Nanexus-owned source code and small upstream patches**, rather than a complete redistribution of third-party media-server sources or prebuilt firmware.

Users obtain the specified upstream revisions, apply the provided patches through the dependency preparation process, and build the Gateway for a supported environment.

The documented workflow is:

1. Obtain and prepare the pinned dependencies.
2. Build the Gateway and required ONVIF signing library.
3. Configure the camera endpoint and signing identity.
4. Start the Gateway after the camera and network are available.
5. Connect an RTSP receiver or recording system to the signed output.
6. Preserve signed video and verify it independently.

### Documentation

- [Native Linux build and configuration](docs/M2-FOUNDATION.md)
- [STM32MP257F-DK ARM64 build, installation, and operation](docs/M2-ARM64.md)
- [Recording and independent signature verification](docs/M2-VERIFICATION.md)
- [Gateway configuration example](config/gateway.example.conf)
- [Upstream dependencies and patches](patches/README.md)

For normal standing operation, the Gateway is started with `nanexus-gateway --config FILE`. The detailed guides cover dependency versions, build commands, signing identity setup, service installation, and verification procedures.

## Scope and Current Limitations

**v0.2.0 is a functional prototype, not a production-ready security appliance.**

The validated implementation currently focuses on a compatible H.264 RTSP camera and a single signing gateway instance.

The prototype demonstrates the complete core signing workflow, but does not yet provide production-level device management, security hardening, broad camera compatibility, or qualified 24/7 unattended reliability.

In particular:

- Automatic recovery from all camera or media-stream interruptions is not implemented; an observed media gap required operator recovery.
- Signing establishes authenticity at the gateway boundary, not at the camera sensor.
- Recording gaps may interrupt cross-interval verification even when the retained portions verify independently.
- ONVIF Media Signing Add-on certification is not claimed.

Detailed protocol, signing-continuity, verification-status, and platform-specific limitations are described in the technical guides.

## Source Code and Licensing

Nanexus-owned source code is provided under the [Apache License 2.0](LICENSE).

The project relies on explicitly pinned upstream revisions of ZLMediaKit, ZLToolKit, and the official ONVIF Media Signing Framework. The published modifications to upstream projects are provided as minimal differential patches, with applicable upstream copyright and license terms retained.

The repository does not redistribute complete upstream source trees, compiled third-party libraries, operating-system images, or signing credentials.

See [Patch and dependency information](patches/README.md) and [Third-Party Notices](THIRD_PARTY_NOTICES.md).

## Acknowledgments

We gratefully acknowledge the open-source projects that make this gateway possible:

- **[ZLMediaKit](https://github.com/ZLMediaKit/ZLMediaKit)** and its contributors, for providing the RTSP media infrastructure on which the gateway is built.
- **[ONVIF Media Signing Framework](https://github.com/onvif/media-signing-framework)** and its contributors, for providing the open-source implementation of ONVIF Media Signing used by this project.

Their work provides the foundation for integrating media signing into existing camera and video-recording workflows.

## Project Evolution

**Milestone 1 — v0.1.0: Initial Signing Gateway Prototype**

The first milestone demonstrated the initial media-signing approach and established its feasibility. Its historical implementation and supporting proof remain available in the [v0.1.0 tag](https://github.com/Nanexus-AI/nanexus-media-signing-gateway/tree/v0.1.0).

**Milestone 2 — v0.2.0: Embedded Functional Gateway Prototype**

The second milestone introduces a substantially different architecture, integrating native ZLMediaKit-based RTSP handling with the official ONVIF Media Signing Framework in an independently runnable embedded gateway.

Version v0.2.0 replaces the earlier architecture for this release; the GStreamer-based Milestone 1 adapter is not required.

The release provides a reproducible, openly available implementation for experimentation and integration with recording and independent verification systems.
