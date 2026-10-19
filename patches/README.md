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
No socket, scheduler, proxy, transport or decoder changes are required.
