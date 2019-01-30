#include <gtest/gtest.h>
#include "aff4/libaff4.h"
#include <iostream>

namespace aff4 {


void URNVerifySerialization(std::string url) {
  URN test(url);
  EXPECT_EQ(test.SerializeToString(), url);
}


TEST(URNTest, SerializeURN) {
  std::string url = "http://www.google.com/path/to/element#hash_data";
  URNVerifySerialization(url);

  // First some valid input.
  URNVerifySerialization("http://www.google.com/path/to/element");
  URNVerifySerialization("http://www.google.com");
  URNVerifySerialization("ftp://www.google.com");
  URNVerifySerialization("");

#ifdef _WIN32
  // Absolute paths.
  EXPECT_EQ(URN::NewURNFromFilename(
      "C:\\Windows\\notepad.exe").SerializeToString(),
            "file:///C:/Windows/notepad.exe");
#else
  // Absolute paths.
  EXPECT_EQ(URN::NewURNFromFilename(
      "/etc/passwd").SerializeToString(), "file:///etc/passwd");
#endif

#ifdef _WIN32
  // FIXME: Due to a bug in uriparser handling of relative paths, we currently
  // force all UNIX path names to be absolute.

  // Relative paths are relative to the current working directory.
  {
    char cwd[1024];
    std::string cwd_string = "/";
    memset(cwd, 0, sizeof(cwd));

    char* u = getcwd(cwd, sizeof(cwd));
    UNUSED(u);
    for(unsigned int i=0; i<sizeof(cwd)-1; i++)
      if(cwd[i]=='\\')
        cwd[i]='/';

    if(cwd[0]!='/')
      cwd_string += cwd;
    else
      cwd_string = cwd;

    EXPECT_EQ(URN("etc/passwd").SerializeToString(),
                std::string("file://") + cwd_string + "/etc/passwd");
  };
#endif
}


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

#ifdef _WIN32
// FIXME: Due to a bug in uriparser handling of relative paths, we currently
// force all UNIX path names to be absolute.
TEST(URNTest, RelativePath) {
  URN parent("aff4://e21659ea-c7d6-4f4d-8070-919178aa4c7b");
  URN child(
      "aff4://e21659ea-c7d6-4f4d-8070-919178aa4c7b/bin/../bin/ls/"
      "00000000/index");

  EXPECT_EQ(parent.RelativePath(child), "/bin/ls/00000000/index");
}
#endif

// Test that XSDInteger can accomodate very large values.
TEST(XSDIntegerTest, SerializeToString) {
  XSDInteger value(0xfff880000000000ULL);

  EXPECT_EQ(value.SerializeToString(), "1152789563211513856");
  EXPECT_EQ(STATUS_OK,
            value.UnSerializeFromString("1152789563211503856", 19));

  EXPECT_EQ(value.value, 1152789563211503856);

  EXPECT_NE(STATUS_OK,
            value.UnSerializeFromString("sdffsdfsdsdf", 12));

  // Can also read hex data.
  EXPECT_EQ(STATUS_OK,
            value.UnSerializeFromString("0xfff880000000000", 17));

  EXPECT_EQ(value.value, 0xfff880000000000);
}


} // namespace aff4
