// SPDX-License-Identifier: Apache-2.0
// Bounded, single-epoch H.264 experiment. Transport stays outside StreamSigner.
#include "videotrust/stream_signer.hpp"
#include "videotrust/nal_classify.hpp"
#include <gst/app/gstappsink.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtsp/gstrtspmessage.h>
#include <gst/rtsp/gstrtsptransport.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>

using namespace videotrust;
using Clock = std::chrono::steady_clock;
namespace {
constexpr size_t kMaxAu = 8 * 1024 * 1024;
constexpr size_t kMaxOutput = 32 * 1024 * 1024;
volatile std::sig_atomic_t cancelled = 0;
void Cancel(int) { cancelled = 1; }
struct Context {
  std::mutex mutex;
  std::string fault;
  bool closing = false, linked = false, rtp = false, output = false;
  bool events[3]{};
  uint16_t seq = 0;
  uint32_t ssrc = 0;
  GstElement* depay = nullptr;
  void Fail(const std::string& why) { if (!closing && fault.empty()) fault = why; }
};
void PadAdded(GstElement*, GstPad* pad, gpointer data) {
  auto& c = *static_cast<Context*>(data);
  GstCaps* caps = gst_pad_get_current_caps(pad);
  if (!caps) caps = gst_pad_query_caps(pad, nullptr);
  if (!caps || gst_caps_is_empty(caps)) { if (caps) gst_caps_unref(caps); return; }
  const auto* s = gst_caps_get_structure(caps, 0);
  const char* media = gst_structure_get_string(s, "media");
  const char* encoding = gst_structure_get_string(s, "encoding-name");
  if (media && encoding && std::string(media) == "video" && std::string(encoding) == "H264") {
    std::lock_guard lock(c.mutex);
    if (c.linked) c.Fail("second H264 video pad");
    else {
      GstPad* sink = gst_element_get_static_pad(c.depay, "sink");
      c.linked = gst_pad_link(pad, sink) == GST_PAD_LINK_OK;
      gst_object_unref(sink);
      if (!c.linked) c.Fail("H264 pad link failed");
    }
  }
  gst_caps_unref(caps);
}
void PadRemoved(GstElement*, GstPad*, gpointer data) {
  auto& c = *static_cast<Context*>(data);
  std::lock_guard lock(c.mutex); c.Fail("source pad removed");
}
gboolean BeforeSend(GstElement*, GstRTSPMessage* message, gpointer data) {
  auto& c = *static_cast<Context*>(data);
  std::lock_guard lock(c.mutex);
  if (c.rtp && message->type == GST_RTSP_MESSAGE_REQUEST &&
      (message->type_data.request.method == GST_RTSP_SETUP ||
       message->type_data.request.method == GST_RTSP_PLAY)) c.Fail("RTSP session restarted");
  return TRUE;
}
struct Probe { Context* c; bool input; };
void Observe(Probe& p, GstBuffer* b) {
  auto& c = *p.c;
  if (GST_BUFFER_FLAG_IS_SET(b, GST_BUFFER_FLAG_CORRUPTED) ||
      GST_BUFFER_FLAG_IS_SET(b, GST_BUFFER_FLAG_GAP)) c.Fail("corrupted/gap buffer");
  bool discont = GST_BUFFER_FLAG_IS_SET(b, GST_BUFFER_FLAG_DISCONT);
  if (p.input) {
    GstRTPBuffer r = GST_RTP_BUFFER_INIT;
    if (!gst_rtp_buffer_map(b, GST_MAP_READ, &r)) { c.Fail("invalid RTP buffer"); return; }
    auto seq = gst_rtp_buffer_get_seq(&r);
    auto ssrc = gst_rtp_buffer_get_ssrc(&r);
    if (c.rtp && (discont || ssrc != c.ssrc || seq != uint16_t(c.seq + 1)))
      c.Fail("RTP sequence/SSRC/DISCONT fault: previous=" + std::to_string(c.seq) + " current=" + std::to_string(seq));
    c.rtp = true; c.seq = seq; c.ssrc = ssrc;
    gst_rtp_buffer_unmap(&r);
  } else {
    if (c.output && discont) c.Fail("parsed output DISCONT");
    c.output = true;
  }
}
GstPadProbeReturn Inspect(GstPad*, GstPadProbeInfo* info, gpointer data) {
  auto& p = *static_cast<Probe*>(data);
  std::lock_guard lock(p.c->mutex);
  if (p.c->closing) return GST_PAD_PROBE_OK;
  if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER)
    Observe(p, GST_PAD_PROBE_INFO_BUFFER(info));
  if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER_LIST) {
    auto* list = GST_PAD_PROBE_INFO_BUFFER_LIST(info);
    for (guint i = 0; i < gst_buffer_list_length(list); ++i) Observe(p, gst_buffer_list_get(list, i));
  }
  if (GST_PAD_PROBE_INFO_TYPE(info) & (GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_EVENT_FLUSH)) {
    auto* e = GST_PAD_PROBE_INFO_EVENT(info);
    auto type = GST_EVENT_TYPE(e);
    auto* s = gst_event_get_structure(e);
    if (type == GST_EVENT_FLUSH_START || type == GST_EVENT_FLUSH_STOP || type == GST_EVENT_GAP ||
        (s && gst_structure_has_name(s, "GstRTPPacketLost"))) p.c->Fail(GST_EVENT_TYPE_NAME(e));
    if (!p.input) {
      int index = type == GST_EVENT_STREAM_START ? 0 : type == GST_EVENT_CAPS ? 1 : type == GST_EVENT_SEGMENT ? 2 : -1;
      if (index >= 0) {
        if (p.c->events[index] && p.c->output) p.c->Fail(std::string("repeated ") + GST_EVENT_TYPE_NAME(e));
        p.c->events[index] = true;
      }
    }
  }
  return GST_PAD_PROBE_OK;
}
int64_t Utc100ns() {
  return (std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count() + 11644473600000000LL) * 10;
}
void Check(Error e) { if (e.code != ErrorCode::Ok) throw std::runtime_error(e.message); }
void Write(std::ofstream& out, std::span<const uint8_t> bytes) {
  out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  if (!out) throw std::runtime_error("artifact write failed");
}
void Close(std::ofstream& out) {
  out.flush(); if (!out) throw std::runtime_error("artifact flush failed");
  out.close(); if (!out) throw std::runtime_error("artifact close failed");
}
}
int main(int argc, char** argv) {
  if (argc != 6) { std::cerr << "usage: live-sign-proof RTSP_URL KEY CHAIN NEW_OUTPUT_DIR SECONDS(15..30)\n"; return 2; }
  Context c; Probe input{&c, true}, output{&c, false};
  GstElement* graph = nullptr;
  GstBus* bus = nullptr;
  std::optional<StreamSigner> signer;
  int result = 1;
  try {
    const unsigned seconds = std::stoul(argv[5]);
    if (seconds < 15 || seconds > 30) throw std::runtime_error("duration outside 15..30 seconds");
    std::signal(SIGINT, Cancel); std::signal(SIGTERM, Cancel);
    std::filesystem::path dir(argv[4]);
    if (!std::filesystem::create_directory(dir)) throw std::runtime_error("output directory must be new");
    std::ofstream unsigned_out(dir / "unsigned.h264", std::ios::binary);
    std::ofstream signed_out(dir / "signed.h264", std::ios::binary);
    std::ofstream timing(dir / "timing.csv");
    if (!unsigned_out || !signed_out || !timing) throw std::runtime_error("cannot open outputs");
    timing << "au,pts_ns,dts_ns,duration_ns,flags,utc_100ns,input_offset,input_size,output_offset,output_size,nals\n";
    StreamSigningOptions options;
    options.key_pem_path = argv[2]; options.cert_pem_path = argv[3];
    options.max_output_bytes = kMaxOutput; options.max_signing_frames = 15;
    size_t emitted = 0, input_bytes = 0, nals = 0, aus = 0, samples = 0;
    auto made = StreamSigner::Create(options, [&](std::span<const uint8_t> bytes) {
      Write(signed_out, bytes); ++emitted; return MakeError(ErrorCode::Ok, {});
    });
    if (!made.ok()) throw std::runtime_error(made.error().message);
    signer.emplace(std::move(made.value()));
    gst_init(nullptr, nullptr);
    graph = gst_pipeline_new("nanexus-live-proof");
    auto* source = gst_element_factory_make("rtspsrc", "source");
    c.depay = gst_element_factory_make("rtph264depay", "depay");
    auto* parser = gst_element_factory_make("h264parse", "parser");
    auto* sink = gst_element_factory_make("appsink", "sink");
    if (!graph || !source || !c.depay || !parser || !sink) throw std::runtime_error("required element unavailable");
    gst_bin_add_many(GST_BIN(graph), source, c.depay, parser, sink, nullptr);
    g_object_set(source, "location", argv[1], "protocols", GST_RTSP_LOWER_TRANS_TCP, "latency", 200U,
        "tcp-timeout", guint64(5000000), "teardown-timeout", guint64(2000000), nullptr);
    g_object_set(parser, "config-interval", 0, "update-timecode", FALSE, nullptr);
    auto* required = gst_caps_from_string("video/x-h264,stream-format=byte-stream,alignment=au");
    gst_app_sink_set_caps(GST_APP_SINK(sink), required); gst_caps_unref(required);
    g_object_set(sink, "max-buffers", 2U, "drop", FALSE, "sync", FALSE,
        "emit-signals", FALSE, "enable-last-sample", FALSE, nullptr);
    if (!gst_element_link_many(c.depay, parser, sink, nullptr)) throw std::runtime_error("graph link failed");
    g_signal_connect(source, "pad-added", G_CALLBACK(PadAdded), &c);
    g_signal_connect(source, "pad-removed", G_CALLBACK(PadRemoved), &c);
    g_signal_connect(source, "before-send", G_CALLBACK(BeforeSend), &c);
    const auto probe_types = GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST |
        GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_EVENT_FLUSH);
    auto* pad = gst_element_get_static_pad(c.depay, "sink");
    gst_pad_add_probe(pad, probe_types, Inspect, &input, nullptr); gst_object_unref(pad);
    pad = gst_element_get_static_pad(parser, "src");
    gst_pad_add_probe(pad, probe_types, Inspect, &output, nullptr); gst_object_unref(pad);
    bus = gst_element_get_bus(graph);
    std::cout << "start_utc_100ns=" << Utc100ns() << "\ngraph=rtspsrc(TCP)->rtph264depay->h264parse->appsink->StreamSigner->file\n";
    if (gst_element_set_state(graph, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) throw std::runtime_error("PLAYING failed");
    const auto started = Clock::now(); auto admitted = started, last = started;
    int64_t first_pts = -1, last_utc = 0;
    while (true) {
      if (cancelled) throw std::runtime_error("caller cancelled; epoch aborted");
      if (Clock::now() - started > std::chrono::seconds(45)) throw std::runtime_error("45 second wall deadline");
      if (Clock::now() - last > std::chrono::seconds(5)) throw std::runtime_error("5 second input stall");
      if (!aus && Clock::now() - started > std::chrono::seconds(10)) throw std::runtime_error("no complete SPS/PPS/IDR startup within 10 seconds");
      { std::lock_guard lock(c.mutex); if (!c.fault.empty()) throw std::runtime_error(c.fault); }
      while (auto* msg = gst_bus_pop_filtered(bus, GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_ELEMENT))) {
        std::string fault;
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
          GError* error = nullptr; gchar* debug = nullptr; gst_message_parse_error(msg, &error, &debug);
          fault = std::string("GStreamer: ") + error->message + (debug ? std::string(" / ") + debug : "");
          g_error_free(error); g_free(debug);
        } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) fault = "unexpected RTSP EOS";
        else { auto* s = gst_message_get_structure(msg); if (s && gst_structure_has_name(s, "GstRTSPSrcTimeout")) fault = "RTSP timeout"; }
        gst_message_unref(msg); if (!fault.empty()) throw std::runtime_error(fault);
      }
      GstSample* raw = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 100 * GST_MSECOND);
      if (!raw) continue;
      std::unique_ptr<GstSample, decltype(&gst_sample_unref)> sample(raw, gst_sample_unref);
      ++samples; last = Clock::now();
      auto* caps = gst_sample_get_caps(raw); auto* s = caps ? gst_caps_get_structure(caps, 0) : nullptr;
      const char* format = s ? gst_structure_get_string(s, "stream-format") : nullptr;
      const char* align = s ? gst_structure_get_string(s, "alignment") : nullptr;
      if (!s || !gst_structure_has_name(s, "video/x-h264") || !format || std::string(format) != "byte-stream" || !align || std::string(align) != "au") throw std::runtime_error("unexpected negotiated caps");
      if (samples == 1) { gchar* text = gst_caps_to_string(caps); std::cout << "negotiated_caps=" << text << '\n'; g_free(text); }
      auto* buffer = gst_sample_get_buffer(raw);
      auto size = gst_buffer_get_size(buffer);
      if (!size || size > kMaxAu) throw std::runtime_error("AU size outside bound");
      GstMapInfo map{};
      if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) throw std::runtime_error("map failed");
      auto parsed = AnnexBReader::Parse({map.data, map.size});
      gst_buffer_unmap(buffer, &map);
      if (!parsed.ok()) throw std::runtime_error(parsed.error().message);
      auto& units = parsed.value();
      if (units.size() > 4096) throw std::runtime_error("too many NALs in AU");
      if (!aus) {
        bool sps = false, pps = false, idr = false;
        for (const auto& nal : units) { auto t = RawNalType(Codec::H264, nal); sps |= t == 7; pps |= t == 8; idr |= t == 5; }
        if (!sps || !pps || !idr) continue;
        admitted = Clock::now(); first_pts = GST_BUFFER_PTS_IS_VALID(buffer) ? int64_t(GST_BUFFER_PTS(buffer)) : -1;
      }
      // Probes and admission/finalization share a lock. No fault may be silently
      // crossed; own teardown happens after the boundary is explicitly closed.
      std::lock_guard lock(c.mutex);
      if (!c.fault.empty()) throw std::runtime_error(c.fault);
      const auto utc = Utc100ns();
      if (last_utc && (utc < last_utc || utc - last_utc > 50000000LL)) throw std::runtime_error("UTC clock discontinuity");
      last_utc = utc;
      if (size > kMaxOutput - input_bytes) throw std::runtime_error("unsigned output bound");
      const size_t before = signer->output_bytes();
      for (const auto& nal : units) { Check(signer->AddNal(nal, utc)); Write(unsigned_out, nal.bytes); ++nals; }
      timing << aus << ',' << GST_BUFFER_PTS(buffer) << ',' << GST_BUFFER_DTS(buffer) << ','
             << GST_BUFFER_DURATION(buffer) << ',' << GST_BUFFER_FLAGS(buffer) << ',' << utc << ','
             << input_bytes << ',' << size << ',' << before << ',' << signer->output_bytes() - before << ',' << units.size() << '\n';
      if (!timing) throw std::runtime_error("timing write failed");
      input_bytes += size; ++aus;
      // Stop after a complete AU on a monotonic wall window, not a timer signal.
      if (Clock::now() - admitted >= std::chrono::seconds(seconds)) {
        c.closing = true;
        Check(signer->Finalize());
        Close(unsigned_out); Close(signed_out); Close(timing);
        std::ofstream summary(dir / "summary.txt");
        summary << "state=finalized\nstop=deliberate-complete-AU\nsamples=" << samples << "\naus=" << aus
                << "\noriginal_nals=" << nals << "\nsigning_seis=" << emitted - nals
                << "\nunsigned_bytes=" << input_bytes << "\nsigned_bytes=" << signer->output_bytes()
                << "\nfirst_pts_ns=" << first_pts << "\nelapsed_seconds="
                << std::chrono::duration<double>(Clock::now() - admitted).count()
                << "\nend_utc_100ns=" << Utc100ns() << '\n';
        Close(summary);
        std::cout << "state=finalized aus=" << aus << " original_nals=" << nals
                  << " signing_seis=" << emitted - nals << " signed_bytes=" << signer->output_bytes() << '\n';
        result = 0; break;
      }
    }
  } catch (const std::exception& e) {
    if (signer) signer->Abort();
    std::cerr << "state=aborted_or_failed reason=" << e.what() << '\n';
  }
  { std::lock_guard lock(c.mutex); c.closing = true; }
  if (graph) { gst_element_set_state(graph, GST_STATE_NULL); gst_object_unref(graph); }
  if (bus) gst_object_unref(bus);
  return result;
}
