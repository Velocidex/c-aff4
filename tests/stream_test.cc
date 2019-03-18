#include <gtest/gtest.h>
#include "aff4/libaff4.h"
#include <unistd.h>

namespace aff4 {


class StreamTest : public ::testing::Test {
 protected:
  void test_Stream(AFF4Stream &stream) {
    EXPECT_EQ(0, stream.Tell());
    EXPECT_EQ(0, stream.Size());

    stream.Write("hello world");
    EXPECT_EQ(11, stream.Tell());

    stream.Seek(0, 0);
    EXPECT_EQ(0, stream.Tell());
    EXPECT_STREQ("hello",
                 stream.Read(5).c_str());
    EXPECT_EQ(5, stream.Tell());

    stream.Seek(0, 0);
    EXPECT_STREQ("hello world",
                 stream.Read(1000).c_str());

    EXPECT_EQ(11, stream.Tell());

    stream.Seek(-5, 2);
    EXPECT_EQ(6, stream.Tell());

    EXPECT_STREQ("world",
                 stream.Read(1000).c_str());

    stream.Seek(-5, 2);
    EXPECT_EQ(6, stream.Tell());

    stream.Write("Cruel world");
    stream.Seek(0, 0);
    EXPECT_EQ(0, stream.Tell());
    EXPECT_STREQ("hello Cruel world",
                 stream.Read(1000).c_str());

    EXPECT_EQ(17, stream.Tell());

    stream.Seek(0, 0);

    EXPECT_STREQ("he",
                 stream.Read(2).c_str());

    stream.sprintf("I have %d arms and %#x legs.", 2, 1025);
    EXPECT_EQ(31, stream.Tell());

    stream.Seek(0, 0);
    EXPECT_STREQ("heI have 2 arms and 0x401 legs.",
                 stream.Read(1000).c_str());

  };

};

TEST_F(StreamTest, StringIOTest) {
        std::unique_ptr<AFF4Stream> stream = StringIO::NewStringIO();

  test_Stream(*stream);
}

class FileBackedStreamTest: public StreamTest {
 protected:
        std::string filename = "/tmp/test_filename.bin";

  // Remove the file on teardown.
  virtual void TearDown() {
    unlink(filename.c_str());
  };
};

TEST_F(FileBackedStreamTest, FileBackedObjectIOTest) {
  MemoryDataStore resolver;
  AFF4Flusher<FileBackedObject> file;
  EXPECT_EQ(NewFileBackedObject(&resolver, filename,
                                "truncate", file), STATUS_OK);
  test_Stream(*file);
}


} // namespace aff4
