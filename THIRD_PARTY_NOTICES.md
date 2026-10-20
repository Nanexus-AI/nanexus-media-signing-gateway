# Third-party notices

Nanexus Media Signing Gateway — source/patch distribution
Copyright 2026 Nanexus-AI contributors
Nanexus-owned Gateway source is licensed under Apache-2.0; see LICENSE.

Source included or adapted
--------------------------
The ONVIF compatibility patch includes a small excerpt of upstream code:
ONVIF Media Signing Framework r25.12.6
Copyright (c) 2025 ONVIF. All rights reserved.
MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next paragraph) shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Development/test-only tooling (not shipped Gateway components)
-------------------------------------------------------------
FFmpeg/libx264 and MediaMTX were used to generate and serve historical synthetic test input.
Stock Frigate and its external FFmpeg tools are independent recording/test components;
neither their source nor their images/binaries are included in this repository.
Their distribution terms apply separately if redistributed. SDK/compiler,
Meson, Ninja and Python are external build/development tools.

M2 signed-AU patch (included source adaptation)
----------------------------------------------
patches/zlmediakit/signed-access-unit.patch adapts H264RtpEncoder at ZLMediaKit
08329b39bf36c141b0a12c2bcceb819456632d9b.
Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
Full original license, including supplementary service-field attribution terms:
patches/zlmediakit/LICENSE.upstream. Retain upstream RTSP Server/User-Agent
identities; this patch does not alter them.

patches/onvif/reject-undefined-tag.patch carries the existing ONVIF TLV guard for
cf7785ab993c18d921094e8e505c2c34a1350f28. The ONVIF MIT permission/copyright notice
above applies. Neither patch bundles a complete upstream library.

M2 external dependencies (not redistributed)
---------------------------------------------
The native build uses the externally obtained, pinned ZLMediaKit, ZLToolKit,
jsoncpp and media-server sources listed in cmake/dependencies.lock.json, and
external official ONVIF/OpenSSL libraries. See docs/M2-FOUNDATION.md for the
acquisition/build contract. These dependencies retain their respective upstream
licenses and file-level notices. No complete upstream source files or trees,
compiled libraries, executables, SDKs, sysroots or images are included here.

Nanexus-owned source is Apache-2.0. The two distributed differential patches
include upstream excerpts/adaptations under the terms identified above; the root
Apache-2.0 license does not relabel those upstream excerpts. Patch context and
resulting upstream file headers retain the original ownership. See
patches/README.md for modified paths and ONVIF fix provenance.
