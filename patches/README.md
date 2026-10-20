# Pinned upstream patches

- `zlmediakit/signed-access-unit.patch`: exactly 25 added lines in
  `ext-codec/H264Rtp.h` and `.cpp`, against
  `08329b39bf36c141b0a12c2bcceb819456632d9b`. Introduces one opt-in ordered signed-AU
  submission method. Reuses stock RTP fragmentation and packet construction.
  Stock `inputFrame` behavior is unchanged. The application owns the encoder
  privately and exclusively uses the new method; do not mix APIs. Adapted from
  the validated experimental extension. Full upstream terms are retained in
  `zlmediakit/LICENSE.upstream` and root third-party notices.
- `onvif/reject-undefined-tag.patch`: existing undefined-TLV rejection guard
  against ONVIF `cf7785ab993c18d921094e8e505c2c34a1350f28` / r25.12.6, unchanged.

Use `scripts/prepare-m2-dependencies.py` for fresh isolated preparation and exact
revision/diff verification. It never edits the supplied source clones. The script
refuses an existing preparation destination, preventing repeated patch application.
`--verify` is read-only and accepts precisely the approved complete tracked diffs.
No additional upstream changes are required beyond these patches.

## ONVIF provenance

The pinned r25.12.6 decoder can accept tag zero (`UNDEFINED_TAG`) and dispatch a
null decoder. Our guard rejects it. The corresponding decoder defect was fixed
upstream in [807ea4bf16d13ff9d4fd2913fc09e3a7476cb305](https://github.com/onvif/media-signing-framework/commit/807ea4bf16d13ff9d4fd2913fc09e3a7476cb305),
tagged r26.6.2. Upstream also changed encoder handling, so this one-line guard is
not an exact backport of that entire commit. M2 remains on r25.12.6 plus this
patch; no upgrade or formal upstream credit to Nanexus is implied.

Both patches retain upstream terms rather than the Nanexus application
Apache-2.0 license. ZLM copyright and its full MIT-like/supplementary terms are
preserved in zlmediakit/LICENSE.upstream and the root notices; ONVIF copyright and MIT
permission/disclaimer are in the root notices. Do not remove upstream service
identities or original source headers. These differential patches do not bundle
complete upstream implementations.

Apply only to the locked source revisions. Fresh preparation verifies patch
applicability; verification rejects unexpected revisions or source diffs. Do not
force the ONVIF patch onto r26.6.2 or automatically skip failed application.
