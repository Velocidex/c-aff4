#include <gtest/gtest.h>
#include <libaff4.h>
#include <unistd.h>

class ZipTest: public ::testing::Test {
 protected:
  string filename = "/tmp/aff4_test.zip";
  string segment_name = "Foobar.txt";
  string data1 = "I am a segment!";
  string data2 = "I am another segment!";
  URN volume_urn;

  // Remove the file on teardown.
  virtual void TearDown() {
    unlink(filename.c_str());
  };

  // Create an initial Zip file for each test.
  virtual void SetUp() {
    MemoryDataStore resolver;
    AFF4Stream* file = AFF4FactoryOpen<AFF4Stream>(&resolver, filename);

    ASSERT_TRUE(file) << "Unable to create zip file";

    // The backing file is given to the zip.
    AFF4Volume* zip = ZipFile::NewZipFile(&resolver, file->urn);

    volume_urn = zip->urn;

    AFF4Stream *segment = zip->CreateMember(segment_name);
    segment->Write(data1);

    // This is actually the same stream as above, we will simply get the same
    // pointer and so the new message will be appended to the old message.
    AFF4Stream *segment2 = zip->CreateMember(segment_name);
    segment2->Seek(0, SEEK_END);
    segment2->Write(data2);
  };

};

TEST_F(ZipTest, CreateMember) {
  // Open the resulting ZipFile.
  MemoryDataStore resolver;
  AFF4Stream* file = AFF4FactoryOpen<AFF4Stream>(&resolver, filename);

  ASSERT_TRUE(file) << "Unable to open zip file";

  ZipFile *zip = ZipFile::NewZipFile(&resolver, file->urn);

  ASSERT_TRUE(zip) << "Unable to parse Zip file:" << file->urn.value.c_str();

  // The parsed URN is the same as was written.
  ASSERT_EQ(zip->urn, volume_urn);

  unique_ptr<AFF4Stream> segment(zip->OpenZipSegment(segment_name));
  ASSERT_TRUE(segment.get());

  string expected = data1 + data2;
  EXPECT_STREQ(expected.c_str(), (segment->Read(1000).c_str()));

  ASSERT_FALSE(zip->IsDirty());
};


/**
 * Tests if we can open a segment by its URN alone.
 */
TEST_F(ZipTest, OpenMemberByURN) {
  // Open the resulting ZipFile.
  MemoryDataStore resolver;
  ZipFile *zip = ZipFile::NewZipFile(&resolver, filename);

  ASSERT_TRUE(zip) << "Unable to open zipfile: " << filename;

  // TODO: Within the resolver segments should be known by their volume URN.
  // i.e. as urn:volume_urn/segment_name.
  AFF4Stream *segment = AFF4FactoryOpen<AFF4Stream>(&resolver, segment_name);
  ASSERT_TRUE(segment) << "Failed to open segment by URN";

  string expected = data1 + data2;
  EXPECT_STREQ(expected.c_str(), (segment->Read(1000).c_str()));
};
