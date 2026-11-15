#include "core/BodyReader.h"

namespace pstryk {

namespace {

// Forwards to the caller's sink while enforcing the byte cap.
struct CappedSink {
  ChunkedDecoder::Sink sink;
  void* ctx;
  size_t left;
};

bool cappedSink(void* p, const char* data, size_t len) {
  CappedSink* c = static_cast<CappedSink*>(p);
  if (len > c->left) return false;
  c->left -= len;
  return c->sink(c->ctx, data, len);
}

}  // namespace

bool readBody(BodySource& src, BodyFraming framing, size_t contentLength,
              size_t maxBytes, uint32_t budgetMs, ChunkedDecoder::Sink sink, void* ctx) {
  if (framing == BodyFraming::Length && contentLength > maxBytes) return false;
  CappedSink cap{sink, ctx, maxBytes};
  ChunkedDecoder dec;
  size_t left = contentLength;     // Length framing only
  char buf[512];
  const uint32_t t0 = src.millis();
  for (;;) {
    if (framing == BodyFraming::Length && left == 0) return true;
    if (framing == BodyFraming::Chunked && dec.done()) return true;
    if (src.millis() - t0 >= budgetMs) return false;
    int avail = src.available();
    if (avail <= 0) {
      // Peer gone: that ends a read-to-close body; any other body is short.
      if (!src.connected()) return framing == BodyFraming::UntilClose;
      src.pause();
      continue;
    }
    size_t want = (size_t)avail < sizeof(buf) ? (size_t)avail : sizeof(buf);
    if (framing == BodyFraming::Length && want > left) want = left;
    int n = src.read(buf, want);
    if (n < 0) return false;
    if (n == 0) {
      src.pause();
      continue;
    }
    if (framing == BodyFraming::Chunked) {
      if (!dec.feed(buf, (size_t)n, cappedSink, &cap)) return false;
    } else {
      if (!cappedSink(&cap, buf, (size_t)n)) return false;
      if (framing == BodyFraming::Length) left -= (size_t)n;
    }
  }
}

}  // namespace pstryk
