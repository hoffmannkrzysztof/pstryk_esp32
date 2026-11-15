#pragma once
#include <cstddef>
#include <cstdint>

namespace pstryk {

// Incremental decoder for an HTTP/1.1 "Transfer-Encoding: chunked" body.
// net/HttpBody reads response bodies itself, under a wall-clock budget, instead
// of HTTPClient::getString() (whose read loop has no deadline at all), so it
// has to undo the chunk framing itself too. Pure and byte-oriented on purpose:
// TLS hands data over in arbitrary slices, and this is unit-tested natively.
class ChunkedDecoder {
 public:
  // Receives decoded payload bytes; returns false to abort (out of memory).
  using Sink = bool (*)(void* ctx, const char* data, size_t len);

  // Consumes the next `n` raw body bytes. Returns false on malformed framing or
  // when the sink refuses data; the decoder then stays failed. Bytes after the
  // terminating zero-length chunk (trailers) are accepted and ignored.
  bool feed(const char* data, size_t n, Sink sink, void* ctx);

  // True once the zero-length chunk that ends the body has been read.
  bool done() const;

 private:
  enum class State : uint8_t { Size, Ext, SizeLf, Data, DataCr, DataLf, Done, Error };
  State state_ = State::Size;
  uint32_t remaining_ = 0;   // chunk length being parsed, then payload bytes left
  uint8_t digits_ = 0;       // hex digits seen on the current size line
};

}  // namespace pstryk
