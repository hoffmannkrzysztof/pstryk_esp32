#include "core/ChunkedDecoder.h"

namespace pstryk {

static int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool ChunkedDecoder::feed(const char* data, size_t n, Sink sink, void* ctx) {
  size_t i = 0;
  while (i < n) {
    switch (state_) {
      case State::Done:
        return true;                       // trailers and anything after: ignored
      case State::Error:
        return false;
      case State::Size: {
        char c = data[i++];
        int v = hexValue(c);
        if (v >= 0) {
          if (digits_ == 8) { state_ = State::Error; break; }   // > 32 bits
          remaining_ = (remaining_ << 4) | (uint32_t)v;
          ++digits_;
        } else if (digits_ > 0 && c == ';') {
          state_ = State::Ext;
        } else if (digits_ > 0 && c == '\r') {
          state_ = State::SizeLf;
        } else {
          state_ = State::Error;
        }
        break;
      }
      case State::Ext:
        if (data[i++] == '\r') state_ = State::SizeLf;
        break;
      case State::SizeLf:
        if (data[i++] != '\n') { state_ = State::Error; break; }
        state_ = remaining_ == 0 ? State::Done : State::Data;
        break;
      case State::Data: {
        size_t take = n - i;
        if (take > remaining_) take = remaining_;
        if (!sink(ctx, data + i, take)) { state_ = State::Error; break; }
        i += take;
        remaining_ -= (uint32_t)take;
        if (remaining_ == 0) state_ = State::DataCr;
        break;
      }
      case State::DataCr:
        state_ = data[i++] == '\r' ? State::DataLf : State::Error;
        break;
      case State::DataLf:
        if (data[i++] != '\n') { state_ = State::Error; break; }
        state_ = State::Size;
        digits_ = 0;
        break;
    }
  }
  return state_ != State::Error;
}

bool ChunkedDecoder::done() const { return state_ == State::Done; }

}  // namespace pstryk
