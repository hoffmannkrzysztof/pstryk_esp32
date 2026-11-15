#include <unity.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "core/BodyReader.h"

using namespace pstryk;

void setUp() {}
void tearDown() {}

// Scripted transport. Each script entry is what one poll finds: a data segment
// (handed out over as many read() calls as the reader asks for), or "" = nothing
// has arrived yet this time. Once the script is used up the peer has either
// closed the connection or keeps it open and silent (keep-alive). The clock is
// fake: each idle pause costs pauseMs, each read readMs.
struct ScriptedSource : BodySource {
  std::vector<std::string> script;
  size_t idx = 0, off = 0;
  bool closesAtEnd = true;
  int failReadAt = -1;            // script index whose read() reports an error
  int zeroReadsLeft = 0;          // read() calls that return 0 despite available() > 0
  uint32_t now = 0, pauseMs = 10, readMs = 0;

  int available() override {
    if (idx < script.size() && script[idx].empty()) {   // a gap: nothing yet this poll
      ++idx;
      return 0;
    }
    return idx < script.size() ? (int)(script[idx].size() - off) : 0;
  }
  int read(char* buf, size_t len) override {
    if ((int)idx == failReadAt) return -1;
    if (zeroReadsLeft > 0) { --zeroReadsLeft; return 0; }
    if (idx >= script.size()) return 0;
    size_t k = std::min(len, script[idx].size() - off);
    std::memcpy(buf, script[idx].data() + off, k);
    off += k;
    if (off == script[idx].size()) { ++idx; off = 0; }
    now += readMs;
    return (int)k;
  }
  bool connected() override { return idx < script.size() || !closesAtEnd; }
  uint32_t millis() override { return now; }
  void pause() override { now += pauseMs; }
};

static bool toString(void* ctx, const char* data, size_t len) {
  static_cast<std::string*>(ctx)->append(data, len);
  return true;
}

static bool readAll(ScriptedSource& s, BodyFraming f, size_t len, std::string& out,
                    size_t maxBytes = 1000, uint32_t budgetMs = 5000) {
  return readBody(s, f, len, maxBytes, budgetMs, toString, &out);
}

void test_length_body_across_segments_and_gaps() {
  ScriptedSource s;
  s.script = {"{\"fra", "", "", "mes\":[]}"};
  std::string out;
  TEST_ASSERT_TRUE(readAll(s, BodyFraming::Length, 13, out));
  TEST_ASSERT_EQUAL_STRING("{\"frames\":[]}", out.c_str());
}

// Keep-alive: the server never closes, so the reader must stop at exactly
// Content-Length bytes and must not swallow what follows.
void test_length_body_stops_at_content_length_on_keep_alive() {
  ScriptedSource s;
  s.script = {"hello" "HTTP/1.1 200 OK"};
  s.closesAtEnd = false;
  std::string out;
  TEST_ASSERT_TRUE(readAll(s, BodyFraming::Length, 5, out));
  TEST_ASSERT_EQUAL_STRING("hello", out.c_str());
}

void test_length_body_cut_short_by_close_fails() {
  ScriptedSource s;
  s.script = {"hel"};
  std::string out;
  TEST_ASSERT_FALSE(readAll(s, BodyFraming::Length, 5, out));
}

void test_zero_length_body_is_complete_at_once() {
  ScriptedSource s;
  s.closesAtEnd = false;
  std::string out;
  TEST_ASSERT_TRUE(readAll(s, BodyFraming::Length, 0, out));
  TEST_ASSERT_EQUAL_STRING("", out.c_str());
}

void test_until_close_body_ends_when_the_peer_closes() {
  ScriptedSource s;
  s.script = {"ab", "", "cd"};
  std::string out;
  TEST_ASSERT_TRUE(readAll(s, BodyFraming::UntilClose, 0, out));
  TEST_ASSERT_EQUAL_STRING("abcd", out.c_str());
}

void test_chunked_body_is_decoded_across_segments() {
  ScriptedSource s;
  s.script = {"5\r\nhel", "", "lo\r\n6\r\n world\r", "\n0\r\n\r\n"};
  s.closesAtEnd = false;          // keep-alive: done() must end the read, not a close
  std::string out;
  TEST_ASSERT_TRUE(readAll(s, BodyFraming::Chunked, 0, out));
  TEST_ASSERT_EQUAL_STRING("hello world", out.c_str());
}

void test_chunked_body_cut_short_by_close_fails() {
  ScriptedSource s;
  s.script = {"5\r\nhello\r\n"};
  std::string out;
  TEST_ASSERT_FALSE(readAll(s, BodyFraming::Chunked, 0, out));
}

void test_malformed_chunked_body_fails() {
  ScriptedSource s;
  s.script = {"zz\r\n"};
  s.closesAtEnd = false;
  std::string out;
  TEST_ASSERT_FALSE(readAll(s, BodyFraming::Chunked, 0, out));
}

// The defect this reader exists for: headers in, then silence on an open
// connection. HTTPClient::getString() spun here until TCP keepalive (~2 h).
void test_silent_open_connection_ends_at_the_budget() {
  ScriptedSource s;
  s.script = {"partial"};
  s.closesAtEnd = false;
  std::string out;
  TEST_ASSERT_FALSE(readAll(s, BodyFraming::Length, 100, out, 1000, 5000));
  TEST_ASSERT_TRUE(s.now >= 5000u);
  TEST_ASSERT_TRUE(s.now < 5100u);
}

// The budget is total wall time, not idle time: a trickle that always has a
// byte ready must not stretch it.
void test_trickle_cannot_outlast_the_budget() {
  ScriptedSource s;
  for (int i = 0; i < 50; ++i) s.script.push_back("x");
  s.readMs = 1000;
  std::string out;
  TEST_ASSERT_FALSE(readAll(s, BodyFraming::Length, 50, out, 1000, 5000));
  TEST_ASSERT_TRUE(out.size() <= 6u);
}

void test_content_length_over_the_cap_fails_without_reading() {
  ScriptedSource s;
  s.script = {"0123456789"};
  std::string out;
  TEST_ASSERT_FALSE(readAll(s, BodyFraming::Length, 10, out, 8));
  TEST_ASSERT_EQUAL_STRING("", out.c_str());
}

void test_until_close_body_over_the_cap_fails() {
  ScriptedSource s;
  s.script = {"0123456789"};
  std::string out;
  TEST_ASSERT_FALSE(readAll(s, BodyFraming::UntilClose, 0, out, 8));
}

void test_chunked_body_over_the_cap_fails() {
  ScriptedSource s;
  s.script = {"a\r\n0123456789\r\n0\r\n\r\n"};
  std::string out;
  TEST_ASSERT_FALSE(readAll(s, BodyFraming::Chunked, 0, out, 8));
}

// TLS can report a record as available and still hand out nothing yet (a
// partial record): the reader must wait for it, not mistake it for an error.
void test_empty_read_is_retried() {
  ScriptedSource s;
  s.script = {"hello"};
  s.zeroReadsLeft = 3;
  std::string out;
  TEST_ASSERT_TRUE(readAll(s, BodyFraming::Length, 5, out));
  TEST_ASSERT_EQUAL_STRING("hello", out.c_str());
}

void test_read_error_fails() {
  ScriptedSource s;
  s.script = {"abc", "def"};
  s.failReadAt = 1;
  std::string out;
  TEST_ASSERT_FALSE(readAll(s, BodyFraming::Length, 6, out));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_length_body_across_segments_and_gaps);
  RUN_TEST(test_length_body_stops_at_content_length_on_keep_alive);
  RUN_TEST(test_length_body_cut_short_by_close_fails);
  RUN_TEST(test_zero_length_body_is_complete_at_once);
  RUN_TEST(test_until_close_body_ends_when_the_peer_closes);
  RUN_TEST(test_chunked_body_is_decoded_across_segments);
  RUN_TEST(test_chunked_body_cut_short_by_close_fails);
  RUN_TEST(test_malformed_chunked_body_fails);
  RUN_TEST(test_silent_open_connection_ends_at_the_budget);
  RUN_TEST(test_trickle_cannot_outlast_the_budget);
  RUN_TEST(test_content_length_over_the_cap_fails_without_reading);
  RUN_TEST(test_until_close_body_over_the_cap_fails);
  RUN_TEST(test_chunked_body_over_the_cap_fails);
  RUN_TEST(test_empty_read_is_retried);
  RUN_TEST(test_read_error_fails);
  return UNITY_END();
}
