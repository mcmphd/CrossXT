#include <cstdio>
#include <cstring>
#include <string>

#include "lib/JsonParser/TrmnlDisplayJsonParser.h"
#include "src/trmnl/TrmnlDisplayConfig.h"

static int testsPassed = 0;
static int testsFailed = 0;

#define ASSERT_TRUE(cond)                                                \
  do {                                                                   \
    if (!(cond)) {                                                       \
      fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      testsFailed++;                                                     \
      return;                                                            \
    }                                                                    \
  } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b)                                                           \
  do {                                                                            \
    auto _a = (a);                                                                \
    auto _b = (b);                                                                \
    if (_a != _b) {                                                               \
      fprintf(stderr, "  FAIL: %s:%d: %s != expected\n", __FILE__, __LINE__, #a); \
      testsFailed++;                                                              \
      return;                                                                     \
    }                                                                             \
  } while (0)

#define ASSERT_STREQ(a, b)                                                              \
  do {                                                                                  \
    const char* _a = (a);                                                               \
    const char* _b = (b);                                                               \
    if (strcmp(_a, _b) != 0) {                                                          \
      fprintf(stderr, "  FAIL: %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a, _b); \
      testsFailed++;                                                                    \
      return;                                                                           \
    }                                                                                   \
  } while (0)

#define PASS() testsPassed++

static void feedChunked(TrmnlDisplayJsonParser& parser, const char* json, const size_t chunkSize) {
  const size_t len = strlen(json);
  for (size_t off = 0; off < len; off += chunkSize) {
    const size_t n = len - off < chunkSize ? len - off : chunkSize;
    parser.feed(json + off, n);
  }
}

void testParsesDisplayResponse() {
  printf("testParsesDisplayResponse...\n");

  const char* json = R"({
    "status": 0,
    "image_url": "https://example.test/api/bitmap/calendar.png",
    "filename": "calendar.png",
    "refresh_rate": 300,
    "update_firmware": false
  })";

  TrmnlDisplayJsonParser parser;
  parser.feed(json, strlen(json));

  ASSERT_FALSE(parser.hasError());
  ASSERT_TRUE(parser.foundImageUrl());
  ASSERT_STREQ(parser.getImageUrl(), "https://example.test/api/bitmap/calendar.png");
  ASSERT_STREQ(parser.getFilename(), "calendar.png");
  ASSERT_EQ(parser.getRefreshRateSeconds(), 300u);

  printf("  passed\n");
  PASS();
}

void testParsesChunkedMinifiedResponse() {
  printf("testParsesChunkedMinifiedResponse...\n");

  const char* json =
      R"({"image_url":"http://192.168.1.50:3000/api/display.bmp","image_name":"display.bmp","refresh_rate":60})";

  TrmnlDisplayJsonParser parser;
  feedChunked(parser, json, 7);

  ASSERT_FALSE(parser.hasError());
  ASSERT_TRUE(parser.foundImageUrl());
  ASSERT_STREQ(parser.getImageUrl(), "http://192.168.1.50:3000/api/display.bmp");
  ASSERT_STREQ(parser.getFilename(), "display.bmp");
  ASSERT_EQ(parser.getRefreshRateSeconds(), 60u);

  printf("  passed\n");
  PASS();
}

void testIgnoresNestedImageUrl() {
  printf("testIgnoresNestedImageUrl...\n");

  const char* json = R"({"nested":{"image_url":"bad"},"image_url":"https://ok.test/screen.png"})";

  TrmnlDisplayJsonParser parser;
  parser.feed(json, strlen(json));

  ASSERT_FALSE(parser.hasError());
  ASSERT_TRUE(parser.foundImageUrl());
  ASSERT_STREQ(parser.getImageUrl(), "https://ok.test/screen.png");

  printf("  passed\n");
  PASS();
}

void testMissingImageUrl() {
  printf("testMissingImageUrl...\n");

  const char* json = R"({"refresh_rate":300,"filename":"calendar.png"})";

  TrmnlDisplayJsonParser parser;
  parser.feed(json, strlen(json));

  ASSERT_FALSE(parser.hasError());
  ASSERT_FALSE(parser.foundImageUrl());
  ASSERT_STREQ(parser.getImageUrl(), "");
  ASSERT_STREQ(parser.getFilename(), "calendar.png");

  printf("  passed\n");
  PASS();
}

// Regression test for a real TRMNL /api/display response: a presigned S3 URL with
// several &-escaped ampersands (valid JSON -- TRMNL's server was observed emitting
// both literal '&' and & for the same field across different responses). Each
// & is stored as 6 raw bytes by this parser (it intentionally doesn't decode
// \uXXXX escapes -- see StreamingJsonParser's onEscapedChar()), so this 486-byte URL
// (with literal '&') becomes 521 bytes with & substituted in -- over the old
// 512-byte TOKEN_BUF_SIZE, which silently dropped the image_url callback with no
// error (tokenOverflow just skips onString()). This must fit in the current buffer.
void testHandlesLongEscapedImageUrl() {
  printf("testHandlesLongEscapedImageUrl...\n");

  const std::string url =
      "https://trmnl.s3.us-east-2.amazonaws.com/m8b1cjzs3zidyc0lexm460vy3t5j?"
      "response-content-disposition=inline%3B%20filename%3D%22plugin-10a5b0%22%3B%20"
      "filename%2A%3DUTF-8%27%27plugin-10a5b0\\u0026"
      "response-content-type=image%2Fpng\\u0026"
      "X-Amz-Algorithm=AWS4-HMAC-SHA256\\u0026"
      "X-Amz-Credential=AKIA47CRUQUU4VKBBMOF%2F20260701%2Fus-east-2%2Fs3%2Faws4_request\\u0026"
      "X-Amz-Date=20260701T154633Z\\u0026"
      "X-Amz-Expires=300\\u0026"
      "X-Amz-SignedHeaders=host\\u0026"
      "X-Amz-Signature=8e8f45ef38cbc565dc18b46574cafb1f8d117b513200d8c795729ef629a41545";
  const std::string json = R"({"status":0,"image_url":")" + url +
                           R"(","filename":"plugin-10a5b0-1782914788","refresh_rate":907})";

  TrmnlDisplayJsonParser parser;
  parser.feed(json.c_str(), json.length());

  ASSERT_FALSE(parser.hasError());
  ASSERT_TRUE(parser.foundImageUrl());
  ASSERT_STREQ(parser.getImageUrl(), url.c_str());

  printf("  passed\n");
  PASS();
}

void testDisplaySizeForOrientation() {
  printf("testDisplaySizeForOrientation...\n");

  const auto landscape = trmnl::displaySizeFor(trmnl::Orientation::Landscape);
  const auto portrait = trmnl::displaySizeFor(trmnl::Orientation::Portrait);

  ASSERT_EQ(landscape.width, 800);
  ASSERT_EQ(landscape.height, 480);
  ASSERT_EQ(portrait.width, 480);
  ASSERT_EQ(portrait.height, 800);
  ASSERT_STREQ(trmnl::modelFor(trmnl::Orientation::Landscape), "og_png");

  printf("  passed\n");
  PASS();
}

int main() {
  testParsesDisplayResponse();
  testParsesChunkedMinifiedResponse();
  testIgnoresNestedImageUrl();
  testMissingImageUrl();
  testHandlesLongEscapedImageUrl();
  testDisplaySizeForOrientation();

  printf("\n%d passed, %d failed\n", testsPassed, testsFailed);
  return testsFailed == 0 ? 0 : 1;
}
