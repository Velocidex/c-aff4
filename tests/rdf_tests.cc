#include <gtest/gtest.h>
#include <libaff4.h>
#include <iostream>

void URNVerifySerialization(string url) {
  URN test(url);
  EXPECT_EQ(test.SerializeToString(), url);
};


TEST(URNTest, SerializeURN) {
  string url = "http://www.google.com/path/to/element#hash_data";
  uri_components components = URN(url).Parse();

  EXPECT_EQ(components.scheme, "http");
  EXPECT_EQ(components.domain, "www.google.com");
  EXPECT_EQ(components.path, "/path/to/element");
  EXPECT_EQ(components.hash_data, "hash_data");
  URNVerifySerialization(url);

  // First some valid input.
  URNVerifySerialization("http://www.google.com/path/to/element");
  URNVerifySerialization("http://www.google.com");
  URNVerifySerialization("ftp://www.google.com");
  URNVerifySerialization("");

  EXPECT_EQ(URN("/etc/passwd").SerializeToString(),
            "file:///etc/passwd");

  EXPECT_EQ(URN("etc/passwd").SerializeToString(),
            "file:///etc/passwd");

  components = URN("http:www.google.com").Parse();

  // Some unusual and incorrect forms.
  EXPECT_EQ(URN("//etc/passwd").SerializeToString(), "file://etc/passwd");

  // www.google.com is considered a path below and domain is empty.
  EXPECT_EQ(URN("http:www.google.com").SerializeToString(),
            "http:///www.google.com");

  EXPECT_EQ(URN("http:/www.google.com").SerializeToString(),
            "http:///www.google.com");
};


TEST(URNTest, Append) {
  URN test = "http://www.google.com";

  EXPECT_EQ(test.Append("foobar").SerializeToString(),
            "http://www.google.com/foobar");

  EXPECT_EQ(test.Append("/foobar").SerializeToString(),
            "http://www.google.com/foobar");

  EXPECT_EQ(test.Append("..").SerializeToString(),
            "http://www.google.com");

  EXPECT_EQ(test.Append("../../../..").SerializeToString(),
            "http://www.google.com");

  EXPECT_EQ(test.Append("aa/bb/../..").SerializeToString(),
            "http://www.google.com");

  EXPECT_EQ(test.Append("aa//../c").SerializeToString(),
            "http://www.google.com/c");

  EXPECT_EQ(test.Append("aa///////////.///./c").SerializeToString(),
            "http://www.google.com/aa/c");
}


TEST(URNTest, RelativePath) {
  URN parent("aff4://e21659ea-c7d6-4f4d-8070-919178aa4c7b");
  URN child("aff4://e21659ea-c7d6-4f4d-8070-919178aa4c7b/bin/../bin/ls/00000000/index");

  EXPECT_EQ(parent.RelativePath(child), "/bin/ls/00000000/index");
};
