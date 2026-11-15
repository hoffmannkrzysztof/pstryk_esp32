#include <unity.h>
#include <cstring>
#include <string>
#include "core/ChunkedDecoder.h"

using namespace pstryk;

void setUp() {}
void tearDown() {}

static bool appendSink(void* ctx, const char* data, size_t len) {
  static_cast<std::string*>(ctx)->append(data, len);
  return true;
}

static bool refuseSink(void*, const char*, size_t) { return false; }

static bool feedStr(ChunkedDecoder& d, const char* s, std::string& out) {
  return d.feed(s, std::strlen(s), appendSink, &out);
}

void test_single_chunk_is_decoded() {
  ChunkedDecoder d;
  std::string out;
  TEST_ASSERT_TRUE(feedStr(d, "5\r\nhello\r\n0\r\n\r\n", out));
  TEST_ASSERT_TRUE(d.done());
  TEST_ASSERT_EQUAL_STRING("hello", out.c_str());
}

void test_several_chunks_are_concatenated() {
  ChunkedDecoder d;
  std::string out;
  TEST_ASSERT_TRUE(feedStr(d, "4\r\n{\"fr\r\n1a\r\names\":[1,2,3],\"summary\":{}\r\n1\r\n}\r\n0\r\n\r\n", out));
  TEST_ASSERT_TRUE(d.done());
  TEST_ASSERT_EQUAL_STRING("{\"frames\":[1,2,3],\"summary\":{}}", out.c_str());
}

// TLS records arrive in arbitrary slices: the decoder must not care where a
// size line, a CRLF or the payload is split.
void test_byte_by_byte_feeding_gives_the_same_result() {
  const char* raw = "3\r\nabc\r\nA\r\n0123456789\r\n0\r\n\r\n";
  ChunkedDecoder d;
  std::string out;
  for (const char* p = raw; *p; ++p) TEST_ASSERT_TRUE(d.feed(p, 1, appendSink, &out));
  TEST_ASSERT_TRUE(d.done());
  TEST_ASSERT_EQUAL_STRING("abc0123456789", out.c_str());
}

void test_hex_size_is_case_insensitive() {
  ChunkedDecoder d;
  std::string out;
  TEST_ASSERT_TRUE(feedStr(d, "a\r\n0123456789\r\nB\r\nabcdefghijk\r\n0\r\n\r\n", out));
  TEST_ASSERT_TRUE(d.done());
  TEST_ASSERT_EQUAL_STRING("0123456789abcdefghijk", out.c_str());
}

void test_chunk_extension_is_ignored() {
  ChunkedDecoder d;
  std::string out;
  TEST_ASSERT_TRUE(feedStr(d, "5;name=value\r\nhello\r\n0\r\n\r\n", out));
  TEST_ASSERT_TRUE(d.done());
  TEST_ASSERT_EQUAL_STRING("hello", out.c_str());
}

// The body is complete at the zero-length chunk; the trailer section and its
// final CRLF may never be read (the caller stops reading as soon as done()).
void test_done_at_last_chunk_and_trailers_are_ignored() {
  ChunkedDecoder d;
  std::string out;
  TEST_ASSERT_TRUE(feedStr(d, "2\r\nok\r\n0\r\n", out));
  TEST_ASSERT_TRUE(d.done());
  TEST_ASSERT_TRUE(feedStr(d, "X-Trailer: 1\r\n\r\nGARBAGE", out));
  TEST_ASSERT_EQUAL_STRING("ok", out.c_str());
}

void test_truncated_body_is_not_done() {
  ChunkedDecoder d;
  std::string out;
  TEST_ASSERT_TRUE(feedStr(d, "5\r\nhel", out));
  TEST_ASSERT_FALSE(d.done());
  TEST_ASSERT_EQUAL_STRING("hel", out.c_str());
}

void test_rejects_non_hex_size() {
  ChunkedDecoder d;
  std::string out;
  TEST_ASSERT_FALSE(feedStr(d, "zz\r\n", out));
  TEST_ASSERT_FALSE(d.done());
}

void test_rejects_empty_size_line() {
  ChunkedDecoder d;
  std::string out;
  TEST_ASSERT_FALSE(feedStr(d, "\r\nhello", out));
}

void test_rejects_missing_crlf_after_chunk_data() {
  ChunkedDecoder d;
  std::string out;
  TEST_ASSERT_FALSE(feedStr(d, "5\r\nhelloX\r\n0\r\n\r\n", out));
  TEST_ASSERT_FALSE(d.done());
}

// More than 32 bits of chunk length is never legitimate, and silently
// wrapping it would desynchronise the framing.
void test_rejects_chunk_length_over_32_bits() {
  ChunkedDecoder d;
  std::string out;
  TEST_ASSERT_FALSE(feedStr(d, "100000000\r\n", out));
}

void test_rejects_bad_byte_after_size_cr() {
  ChunkedDecoder d;
  std::string out;
  TEST_ASSERT_FALSE(feedStr(d, "5\rXhello\r\n", out));
}

void test_rejects_bad_byte_after_data_cr() {
  ChunkedDecoder d;
  std::string out;
  TEST_ASSERT_FALSE(feedStr(d, "5\r\nhello\rX0\r\n\r\n", out));
}

void test_extension_on_the_last_chunk_still_ends_the_body() {
  ChunkedDecoder d;
  std::string out;
  TEST_ASSERT_TRUE(feedStr(d, "2\r\nok\r\n0;final=1\r\n\r\n", out));
  TEST_ASSERT_TRUE(d.done());
  TEST_ASSERT_EQUAL_STRING("ok", out.c_str());
}

// Eight hex digits are the most a 32-bit length can need -- leading zeros
// included -- so they are still accepted.
void test_eight_hex_digits_are_accepted() {
  ChunkedDecoder d;
  std::string out;
  TEST_ASSERT_TRUE(feedStr(d, "00000005\r\nhello\r\n0\r\n\r\n", out));
  TEST_ASSERT_TRUE(d.done());
  TEST_ASSERT_EQUAL_STRING("hello", out.c_str());
}

// Strict CRLF framing: both servers involved (uvicorn, GitHub's CDN) emit it,
// and a bare-LF stream is far more likely corrupt than exotic.
void test_bare_lf_framing_is_rejected() {
  ChunkedDecoder d;
  std::string out;
  TEST_ASSERT_FALSE(feedStr(d, "5\nhello\n0\n\n", out));
}

void test_sink_refusal_stops_decoding() {
  ChunkedDecoder d;
  const char* raw = "5\r\nhello\r\n0\r\n\r\n";
  TEST_ASSERT_FALSE(d.feed(raw, std::strlen(raw), refuseSink, nullptr));
  TEST_ASSERT_FALSE(d.done());
}

void test_stays_failed_after_an_error() {
  ChunkedDecoder d;
  std::string out;
  TEST_ASSERT_FALSE(feedStr(d, "q", out));
  TEST_ASSERT_FALSE(feedStr(d, "5\r\nhello\r\n0\r\n\r\n", out));
  TEST_ASSERT_FALSE(d.done());
  TEST_ASSERT_EQUAL_STRING("", out.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_chunk_is_decoded);
  RUN_TEST(test_several_chunks_are_concatenated);
  RUN_TEST(test_byte_by_byte_feeding_gives_the_same_result);
  RUN_TEST(test_hex_size_is_case_insensitive);
  RUN_TEST(test_chunk_extension_is_ignored);
  RUN_TEST(test_done_at_last_chunk_and_trailers_are_ignored);
  RUN_TEST(test_truncated_body_is_not_done);
  RUN_TEST(test_rejects_non_hex_size);
  RUN_TEST(test_rejects_empty_size_line);
  RUN_TEST(test_rejects_missing_crlf_after_chunk_data);
  RUN_TEST(test_rejects_chunk_length_over_32_bits);
  RUN_TEST(test_rejects_bad_byte_after_size_cr);
  RUN_TEST(test_rejects_bad_byte_after_data_cr);
  RUN_TEST(test_extension_on_the_last_chunk_still_ends_the_body);
  RUN_TEST(test_eight_hex_digits_are_accepted);
  RUN_TEST(test_bare_lf_framing_is_rejected);
  RUN_TEST(test_sink_refusal_stops_decoding);
  RUN_TEST(test_stays_failed_after_an_error);
  return UNITY_END();
}
