#pragma once
#include <cstddef>
#include <cstdint>
#include "core/ChunkedDecoder.h"

namespace pstryk {

// The transport an HTTP response body is read from, reduced to what the read
// loop needs -- so the loop runs natively under test with a scripted source and
// a fake clock (net/HttpBody adapts the TLS client to it).
class BodySource {
 public:
  virtual ~BodySource() = default;
  virtual int available() = 0;                    // bytes readable now; <= 0 if none
  virtual int read(char* buf, size_t len) = 0;    // < 0 on error
  virtual bool connected() = 0;
  virtual uint32_t millis() = 0;
  virtual void pause() = 0;                       // nothing to read yet: yield briefly
};

enum class BodyFraming : uint8_t {
  Length,       // exactly contentLength bytes (keep-alive safe)
  UntilClose,   // everything until the peer closes
  Chunked,      // Transfer-Encoding: chunked, decoded
};

// Reads one response body from `src` into `sink`. Fails when the wall-clock
// budget runs out (total time, not idle time), when the peer closes before the
// body is complete, on a read error or malformed chunk framing, when the body
// would exceed maxBytes, or when the sink refuses data.
bool readBody(BodySource& src, BodyFraming framing, size_t contentLength,
              size_t maxBytes, uint32_t budgetMs, ChunkedDecoder::Sink sink, void* ctx);

}  // namespace pstryk
