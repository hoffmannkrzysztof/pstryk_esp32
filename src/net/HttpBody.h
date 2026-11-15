#pragma once
#include <Arduino.h>

class HTTPClient;

namespace pstryk {

// Reads the body of the response `http` has just received (after GET()) into
// `out`, within `budgetMs` of wall time and `maxBytes` of payload.
// HTTPClient::getString() has no deadline at all: its read loop spins for as
// long as the socket stays open, and over TLS a peer that goes quiet after the
// headers keeps it open until TCP keepalive gives up (~2 h 11 min) -- on the
// battery board with the radio on the whole time. Content-Length, read-to-close
// and chunked bodies are all handled (the loop is core/BodyReader, tested
// natively). Returns false on timeout, on a short, malformed or oversized body,
// and when out of memory.
bool readHttpBody(HTTPClient& http, String& out, size_t maxBytes, uint32_t budgetMs);

}  // namespace pstryk
