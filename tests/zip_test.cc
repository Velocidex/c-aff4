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

    // We are allowed to write on the output filename.
    resolver.Set(filename, AFF4_STREAM_WRITE_MODE, new XSDString("truncate"));

    AFF4ScopedPtr<AFF4Stream> file = resolver.AFF4FactoryOpen<AFF4Stream>(
        filename);

    ASSERT_TRUE(file.get()) << "Unable to create zip file";

    // The backing file is given to the zip.
    AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, file->urn);

    volume_urn = zip->urn;

    // The full URN of the segment is relative to the volume URN. When using the
    // generic AFF4Volume interface, we must always store fully qualified
    // URNs. While the ZipFile interface zip->CreateZipSegment() only accepts
    // zip member names.
    URN segment_urn = volume_urn.Append(segment_name);

    AFF4ScopedPtr<AFF4Stream> segment = zip->CreateMember(segment_urn);
    segment->Write(data1);

    // This is actually the same stream as above, we will simply get the same
    // pointer and so the new message will be appended to the old message.
    AFF4ScopedPtr<AFF4Stream> segment2 = zip->CreateMember(segment_urn);
    segment2->Seek(0, SEEK_END);
    segment2->Write(data2);
  };

};

TEST_F(ZipTest, CreateMember) {
  // Open the resulting ZipFile.
  MemoryDataStore resolver;
  AFF4ScopedPtr<AFF4Stream> file = resolver.AFF4FactoryOpen<AFF4Stream>(
      filename);

  ASSERT_TRUE(file.get()) << "Unable to open zip file";

  AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, file->urn);

  ASSERT_TRUE(zip.get()) << "Unable to parse Zip file:" <<
      file->urn.value.c_str();

  // The parsed URN is the same as was written.
  ASSERT_EQ(zip->urn, volume_urn);

  AFF4ScopedPtr<ZipFileSegment> segment(zip->OpenZipSegment(segment_name));
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
  AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, filename);

  ASSERT_TRUE(zip.get()) << "Unable to open zipfile: " << filename;

  {
    // The generic AFF4Volume interface must refer to members by their full
    // URNs. This should fail.
    AFF4ScopedPtr<AFF4Stream> segment = resolver.AFF4FactoryOpen<AFF4Stream>(
        segment_name);

    ASSERT_TRUE(!segment) << "Wrong segment opened.";
  };

  // Try with the full URN.
  AFF4ScopedPtr<AFF4Stream> segment = resolver.AFF4FactoryOpen<AFF4Stream>(
      zip->urn.Append(segment_name));

  // Should work.
  ASSERT_TRUE(segment.get()) << "Failed to open segment by URN";

  string expected = data1 + data2;
  EXPECT_STREQ(expected.c_str(), (segment->Read(1000).c_str()));
};
