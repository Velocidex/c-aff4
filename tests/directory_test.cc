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

class AFF4DirectoryTest: public ::testing::Test {
 protected:
	std::string root_path = "/tmp/aff4_directory/";
	std::string segment_name = "Foobar.txt";

  // Remove the file on teardown.
  virtual void TearDown() {
    AFF4Directory::RemoveDirectory(root_path);
  }

  // Create an initial container for each test.
  virtual void SetUp() {
    MemoryDataStore resolver;
    URN root_urn = URN::NewURNFromFilename(root_path);

    // We are allowed to write on the output filename.
    resolver.Set(root_urn, AFF4_STREAM_WRITE_MODE, new XSDString("truncate"));

    // Create a new directory.
    AFF4ScopedPtr<AFF4Directory> volume = AFF4Directory::NewAFF4Directory(
        &resolver, root_urn);

    ASSERT_TRUE(volume.get()) << "Unable to create AFF4Directory " <<
        root_urn.SerializeToString();

    URN segment_urn = volume->urn.Append(segment_name);

    AFF4ScopedPtr<AFF4Stream> member = volume->CreateMember(segment_urn);

    ASSERT_TRUE(member.get()) << "Unable to create member " <<
        segment_urn.SerializeToString();

    member->Write("Hello world");
    resolver.Set(member->urn, AFF4_STREAM_ORIGINAL_FILENAME,
                 new XSDString(root_path + segment_name));
  }
};

TEST_F(AFF4DirectoryTest, CreateMember) {
  MemoryDataStore resolver;
  URN root_urn = URN::NewURNFromFilename(root_path);

  // Open the Directory volume:
  AFF4ScopedPtr<AFF4Directory> directory = AFF4Directory::NewAFF4Directory(
      &resolver, root_urn);

  ASSERT_TRUE(directory.get()) << "Unable to open AFF4Directory: " <<
      root_urn.SerializeToString();

  // Check for member.
  URN child_urn = directory->urn.Append(segment_name);
  AFF4ScopedPtr<AFF4Stream> child = resolver.AFF4FactoryOpen<AFF4Stream>(
      child_urn);

  ASSERT_TRUE(child.get()) << "Unable to open member.";

  ASSERT_EQ(child->Read(10000), "Hello world");

  // Check that the metadata is carried over.
  XSDString filename;
  ASSERT_EQ(STATUS_OK,
            resolver.Get(child_urn, AFF4_STREAM_ORIGINAL_FILENAME, filename));

  ASSERT_EQ(filename.SerializeToString(), root_path + segment_name);
}
