# Third-party notices

Nanexus Media Signing Gateway Reference Platform
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

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Historical M1 external build/runtime dependencies (not vendored)
----------------------------------------------------------------
Nanexus Video Trust Toolkit (Apache-2.0): the preserved M1 proof build compiles selected
Toolkit signing, upstream-session and Annex-B source files from an external
checkout. Toolkit's applicable NOTICE attribution is:

Nanexus Video Trust Toolkit
Copyright 2026 Nanexus-AI contributors
This product includes software developed for the Nanexus Video Trust Toolkit project.
Licensed under the Apache License, Version 2.0. See LICENSE.

ONVIF Media Signing Framework (MIT): external signing shared library.
GStreamer core/app/RTP and RTSP/RTP/parser plugins (LGPL-2.1-or-later): external
libraries and target plugins. Check the licenses of the actual installed plugins.
OpenSSL 3.x (Apache-2.0): external cryptographic library.

Keep applicable licenses, copyright notices and source/relinking obligations
when redistributing dependency sources or binaries. The Gateway LICENSE does
not replace dependency licenses. This source release bundles no dependency
binaries and makes no claim of ONVIF certification or endorsement.

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

The optional native M2 build now links official ONVIF/OpenSSL and the pinned
ZLM/ZLToolKit/jsoncpp/FLV/MOV static closure. It does not link GStreamer, Toolkit
or FFmpeg media libraries. ZLToolKit's FFmpeg-derived Base64 code remains part of
that closure and retains LGPL-2.1-or-later obligations; its presence is not an
FFmpeg media-framework dependency. Complete file/object attribution and binary
source/relinking compliance remain required before any distribution approval.
