/*
Copyright 2015 Google Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use
this file except in compliance with the License.  You may obtain a copy of the
License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.  See the License for the
specific language governing permissions and limitations under the License.
*/

#include <gtest/gtest.h>
#include <libaff4.h>
#include <unistd.h>

class ZipTest: public ::testing::Test {
 protected:
	std::string filename = "/tmp/aff4_test.zip";
	std::string segment_name = "Foobar.txt";
	std::string data1 = "I am a segment!";
	std::string data2 = "I am another segment!";
  URN volume_urn;

  // Remove the file on teardown.
  virtual void TearDown() {
    unlink(filename.c_str());
  }

  // Create an initial Zip file for each test.
  virtual void SetUp() {
    MemoryDataStore resolver;

    // We are allowed to write on the output filename.
    resolver.Set(filename, AFF4_STREAM_WRITE_MODE, new XSDString("truncate"));

    {
      AFF4ScopedPtr<AFF4Stream> file = resolver.AFF4FactoryOpen<AFF4Stream>(
          filename);

      ASSERT_TRUE(file.get()) << "Unable to create zip file";
    }

    // The backing file is given to the zip.
    AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, filename);

    volume_urn = zip->urn;

    // The full URN of the segment is relative to the volume URN. When using the
    // generic AFF4Volume interface, we must always store fully qualified
    // URNs. While the ZipFile interface zip->CreateZipSegment() only accepts
    // zip member names.
    URN segment_urn = volume_urn.Append(segment_name);

    {
      AFF4ScopedPtr<AFF4Stream> segment = zip->CreateMember(segment_urn);
      segment->Write(data1);
    }

    {
      // This is actually the same stream as above, we will simply get the same
      // pointer and so the new message will be appended to the old message.
      AFF4ScopedPtr<AFF4Stream> segment2 = zip->CreateMember(segment_urn);
      segment2->Seek(0, SEEK_END);
      segment2->Write(data2);
    }

    // Test the streamed interface.
    {
      URN streamed_urn = segment_urn.Append("streamed");
      std::unique_ptr<AFF4Stream> test_stream = StringIO::NewStringIO();
      test_stream->Write(data1);
      test_stream->Seek(0, SEEK_SET);

      AFF4ScopedPtr<ZipFileSegment> segment = zip->CreateZipSegment(
          member_name_for_urn(streamed_urn, zip->urn, true));

      segment->compression_method = ZIP_DEFLATE;
      segment->WriteStream(test_stream.get());
    }
  }
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

  std::string expected = data1 + data2;
  EXPECT_STREQ(expected.c_str(), (segment->Read(1000).c_str()));

  ASSERT_FALSE(zip->IsDirty());

  // Test conversion between urn and zip.
  {
    URN test = zip->urn.Append("URN-with!special$chars/and/path");
    std::string member_name = member_name_for_urn(test.SerializeToString(),
                                             zip->urn, true);
    EXPECT_STREQ(member_name.c_str(),
                 "URN-with%21special%24chars/and/path");

    // Check that the reverse works.
    EXPECT_STREQ(urn_from_member_name(
        member_name, zip->urn).SerializeToString().c_str(),
                 test.SerializeToString().c_str());
  }

  {
    // A windows based URN.
    URN test = zip->urn.Append("/C:/Windows/notepad.exe");
    std::string member_name = member_name_for_urn(test.SerializeToString(),
                                             zip->urn, true);
    EXPECT_STREQ(member_name.c_str(),
                 "C%3a/Windows/notepad.exe");

    // Check that the reverse works.
    EXPECT_STREQ(urn_from_member_name(
        member_name, zip->urn).SerializeToString().c_str(),
                 test.SerializeToString().c_str());
  }

  {
    // An AFF4 URN not based at zip->urn should be emitted fully escaped.
    URN test("aff4://123456/URN-with!special$chars/and/path");
    std::string member_name = member_name_for_urn(test.SerializeToString(),
                                             zip->urn, true);
    EXPECT_STREQ(member_name.c_str(),
                 "aff4%3a%2f%2f123456/URN-with%21special%24chars/and/path");

    // When recovered it should not be merged with the base URN since it is a
    // fully qualified URN.
    EXPECT_STREQ(urn_from_member_name(
        member_name, zip->urn).SerializeToString().c_str(),
                 test.SerializeToString().c_str());
  }
}


/**
 * Tests if we can open a segment by its URN alone.
 */
TEST_F(ZipTest, OpenMemberByURN) {
  MemoryDataStore resolver;
  URN segment_urn;

  // Open the resulting ZipFile.
  {
    AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, filename);
    segment_urn = zip->urn.Append(segment_name);

    ASSERT_TRUE(zip.get()) << "Unable to open zipfile: " << filename;
  }

  {
    // The generic AFF4Volume interface must refer to members by their full
    // URNs. This should fail.
    AFF4ScopedPtr<AFF4Stream> segment = resolver.AFF4FactoryOpen<AFF4Stream>(
        segment_name);

    ASSERT_TRUE(!segment) << "Wrong segment opened.";
  }

  // Try with the full URN.
  AFF4ScopedPtr<AFF4Stream> segment = resolver.AFF4FactoryOpen<AFF4Stream>(
      segment_urn);

  // Should work.
  ASSERT_TRUE(segment.get()) << "Failed to open segment by URN";

  std::string expected = data1 + data2;
  EXPECT_STREQ(expected.c_str(), (segment->Read(1000).c_str()));
}

/**
 * Test that we can handle concatenated volumes (i.e. an AFF4 volume appended to
 * something else. Check we can read them and also we can modify them without
 * corrupting the volume..
 */
TEST_F(ZipTest, ConcatenatedVolumes) {
  {
    MemoryDataStore resolver;

    std::string concate_filename = filename + "_con.zip";

    resolver.Set(concate_filename, AFF4_STREAM_WRITE_MODE, new XSDString(
        "truncate"));

    // Copy the files across.
    {
      AFF4ScopedPtr<AFF4Stream> file = resolver.AFF4FactoryOpen<AFF4Stream>(
          filename);

      ASSERT_TRUE(file.get()) << "Unable to create file";

      AFF4ScopedPtr<AFF4Stream> concate_file = resolver.AFF4FactoryOpen<
        AFF4Stream>(concate_filename);

      ASSERT_TRUE(concate_file.get()) << "Unable to create file";

      concate_file->Write("pad pad pad pad pad pad pad");
      file->CopyToStream(*concate_file, file->Size());
    }

    // Now open the zip file from the concatenated file.
    AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(
        &resolver, concate_filename);

    ASSERT_TRUE(zip.get()) << "Unable to create zip file";

    AFF4ScopedPtr<ZipFileSegment> segment(zip->OpenZipSegment(segment_name));
    ASSERT_TRUE(segment.get());

    std::string expected = data1 + data2;
    EXPECT_STREQ(expected.c_str(), (segment->Read(1000).c_str()));

    // Now ensure we can modify the file.
    segment->Truncate();
    segment->Write("foobar");
  }

  // Now check with a fresh resolver.
  MemoryDataStore resolver;

  std::string concate_filename = filename + "_con.zip";

  // Now open the zip file from the concatenated file.
  AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, concate_filename);
  ASSERT_TRUE(zip.get()) << "Unable to create zip file";

  AFF4ScopedPtr<ZipFileSegment> segment(zip->OpenZipSegment(segment_name));
  ASSERT_TRUE(segment.get());

  // New data should be there.
  EXPECT_STREQ("foobar", (segment->Read(1000).c_str()));
}


TEST_F(ZipTest, testStreamedSegment) {
  MemoryDataStore resolver;
  URN segment_urn;

  // Open the resulting ZipFile.
  {
    AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, filename);
    segment_urn = zip->urn.Append(segment_name).Append("streamed");

    ASSERT_TRUE(zip.get()) << "Unable to open zipfile: " << filename;
  }

  AFF4ScopedPtr<AFF4Stream> segment = resolver.AFF4FactoryOpen<AFF4Stream>(
          segment_urn);

  ASSERT_TRUE(segment.get());

  std::string expected = data1;
  EXPECT_STREQ(expected.c_str(), (segment->Read(1000).c_str()));
}
