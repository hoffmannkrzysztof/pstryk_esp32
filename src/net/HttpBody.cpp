#include "net/HttpBody.h"
#include "core/BodyReader.h"
#include <HTTPClient.h>

namespace pstryk {

namespace {

// The TLS client as a BodySource (available/read/connected dispatch to
// NetworkClientSecure, which never blocks: its socket is non-blocking).
class ClientSource : public BodySource {
 public:
  explicit ClientSource(NetworkClient& c) : c_(c) {}
  int available() override { return c_.available(); }
  int read(char* buf, size_t len) override {
    return c_.read(reinterpret_cast<uint8_t*>(buf), len);
  }
  bool connected() override { return c_.connected(); }
  uint32_t millis() override { return ::millis(); }
  void pause() override { delay(2); }

 private:
  NetworkClient& c_;
};

// HTTPClient parses Transfer-Encoding while reading the headers but keeps the
// result in a protected member. A pointer to member formed through a derived
// class is the standard-conforming way to read it -- and it means no caller has
// to remember to collect the header (collectHeaders() replaces the whole list).
struct TransferEncodingOf : HTTPClient {
  static bool chunked(HTTPClient& h) {
    return h.*(&TransferEncodingOf::_transferEncoding) == HTTPC_TE_CHUNKED;
  }
};

bool appendToString(void* ctx, const char* data, size_t len) {
  return static_cast<String*>(ctx)->concat(data, (unsigned int)len);
}

}  // namespace

bool readHttpBody(HTTPClient& http, String& out, size_t maxBytes, uint32_t budgetMs) {
  out = String();
  NetworkClient* s = http.getStreamPtr();
  if (!s) return false;
  const int size = http.getSize();   // Content-Length, or -1 when absent
  const BodyFraming framing = TransferEncodingOf::chunked(http) ? BodyFraming::Chunked
                              : size >= 0                         ? BodyFraming::Length
                                                                  : BodyFraming::UntilClose;
  if (framing == BodyFraming::Length) {
    if ((size_t)size > maxBytes) return false;
    out.reserve((unsigned int)size);
  }
  ClientSource src(*s);
  return readBody(src, framing, size > 0 ? (size_t)size : 0, maxBytes, budgetMs,
                  appendToString, &out);
}

}  // namespace pstryk
