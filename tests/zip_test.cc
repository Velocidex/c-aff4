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
#include "aff4/libaff4.h"
#include "aff4/volume_group.h"
#include <unistd.h>

namespace aff4 {

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

  // Create an initial Zip file for each test and write a segment in
  // it.
  virtual void SetUp() {
    MemoryDataStore resolver;

    AFF4Flusher<AFF4Stream> file;
    AFF4Flusher<ZipFile> zip;
    EXPECT_EQ(NewFileBackedObject(&resolver, filename, "truncate", file),
              STATUS_OK);
    EXPECT_EQ(ZipFile::NewZipFile(&resolver, std::move(file), zip),
              STATUS_OK);

    // The full URN of the segment is relative to the volume URN. When using the
    // generic AFF4Volume interface, we must always store fully qualified
    // URNs. While the ZipFile interface zip->CreateZipSegment() only accepts
    // zip member names.
    volume_urn = zip->urn;
    URN segment_urn = volume_urn.Append(segment_name);
    AFF4Flusher<AFF4Stream> segment;
    EXPECT_EQ(zip->CreateMemberStream(segment_urn, segment), STATUS_OK);

    segment->Write(data1);
  }
};

TEST_F(ZipTest, CreateMemberStream) {
  // Open the resulting ZipFile.
  MemoryDataStore resolver;

  AFF4Flusher<AFF4Stream> file;
  AFF4Flusher<AFF4Volume> zip;
  EXPECT_EQ(NewFileBackedObject(&resolver, filename, "read", file),
            STATUS_OK);

  EXPECT_EQ(ZipFile::OpenZipFile(&resolver, std::move(file), zip),
            STATUS_OK);

  // The parsed URN is the same as was written.
  ASSERT_EQ(zip->urn, volume_urn);

  AFF4Flusher<AFF4Stream> segment;
  EXPECT_EQ(zip->OpenMemberStream(segment_name, segment),
            STATUS_OK);

  EXPECT_STREQ(data1.c_str(), (segment->Read(1000).c_str()));

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
                 "C%3A/Windows/notepad.exe");

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
                 "aff4%3A%2F%2F123456/URN-with%21special%24chars/and/path");

    // When recovered it should not be merged with the base URN since it is a
    // fully qualified URN.
    EXPECT_STREQ(urn_from_member_name(
        member_name, zip->urn).SerializeToString().c_str(),
                 test.SerializeToString().c_str());
  }
}


/**
 * Tests if we can open a segment by its URN alone using the volume
 * group.
 */
TEST_F(ZipTest, OpenMemberByURN) {
  MemoryDataStore resolver;

  AFF4Flusher<AFF4Stream> file;
  AFF4Flusher<AFF4Volume> zip;
  EXPECT_EQ(NewFileBackedObject(&resolver, filename, "read", file),
            STATUS_OK);

  EXPECT_EQ(ZipFile::OpenZipFile(&resolver, std::move(file), zip),
            STATUS_OK);

  URN segment_urn = zip->urn.Append(segment_name);

  VolumeGroup volumes(&resolver);

  // Give the volume group this zip file.
  volumes.AddVolume(std::move(zip));


  // Now open the segment using its full URN;

  AFF4Flusher<AFF4Stream> segment;
  EXPECT_EQ(volumes.GetStream(segment_urn, segment), STATUS_OK);

  EXPECT_STREQ(data1.c_str(), (segment->Read(1000).c_str()));
}

/**
 * Test that we can handle concatenated volumes (i.e. an AFF4 volume appended to
 * something else. Check we can read them and also we can modify them without
 * corrupting the volume..
 */
TEST_F(ZipTest, ConcatenatedVolumes) {
  {
    MemoryDataStore resolver;

    std::string concat_filename = filename + "_con.zip";

    // Concatenate the zip file on top of some padding.
    {
        AFF4Flusher<FileBackedObject> concat_file;
        EXPECT_EQ(NewFileBackedObject(&resolver, concat_filename,
                                      "truncate", concat_file),
                  STATUS_OK);

        AFF4Flusher<FileBackedObject> file;
        EXPECT_EQ(NewFileBackedObject(&resolver, filename, "read", file),
                  STATUS_OK);

        auto padding = "pad pad pad pad pad pad pad";
        concat_file->Write(padding, strlen(padding));
        file->CopyToStream(*concat_file, file->Size());
    }

    AFF4Flusher<AFF4Stream> concat_file;
    EXPECT_EQ(NewFileBackedObject(&resolver, concat_filename,
                                  "read", concat_file),
              STATUS_OK);

    AFF4Flusher<AFF4Volume> zip;
    EXPECT_EQ(ZipFile::OpenZipFile(&resolver, std::move(concat_file), zip),
              STATUS_OK);

    AFF4Flusher<AFF4Stream> segment;
    EXPECT_EQ(zip->OpenMemberStream(segment_name, segment),
              STATUS_OK);

    EXPECT_STREQ(data1.c_str(), (segment->Read(1000).c_str()));
  }
}


} // namespace aff4
