# Nanexus Media Signing Gateway Reference Platform

An embedded-Linux reference platform intended to bring standards-based media signing to a reliable embedded gateway, while producing a hardware-neutral Signing Runtime and portable integration contracts.

**Status: pre-implementation.** Phase 0 has not started. No signing, embedded appliance, or update/recovery functionality is implemented in this repository. The first planned reference target is **STM32MP257F-DK + OpenSTLinux / Yocto**; the board is awaiting delivery.

The planned primary path is:

```text
H.264/H.265 RTSP source
→ embedded gateway
→ ONVIF Media Signing
→ signed compressed media
→ independent Nanexus Video Trust Toolkit verification
```

**No decode, no re-encode, no transcoding on the signing path.** This is a design requirement, not a demonstrated capability here.

This project is independent of **Nanexus Video Trust Toolkit**. The gateway is intended to own embedded signing integration and deployment; the Toolkit independently verifies media and interprets trust. This project is not Toolkit M8. A gateway signature would cover the gateway observation boundary onward, without establishing authenticity before that boundary or equating source identity with gateway signer identity. Interoperability remains to be demonstrated.

The architecture direction is a **hardware-neutral Signing Runtime plus reference embedded integration**. STM32 is the first reference target, rather than a required dependency of the portable runtime.

| Planned phase | Goal |
| --- | --- |
| 0 — Board Bring-up | Stable headless embedded Linux target and reproducible baseline |
| 1 — Signing Gateway Port | Compressed RTSP signing and independent standards verification |
| 2 — Appliance Runtime | Service lifecycle, health, watchdog, and failure recovery |
| 3 — Update / Recovery | Firmware update, health validation, rollback, and recovery |

Standards-based ONVIF signing comes first. Signed provenance/context binding is a later planned capability, with its representation still undecided; it must not delay the standards baseline.

The intended model is a public-capable runtime and reference integration, with a separate future private commercial/operational layer. The public-capable foundation is intended to work independently of that private layer. The repository is currently private; no public release exists and no public license has been selected.
