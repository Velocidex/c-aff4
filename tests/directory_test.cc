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
#include <unistd.h>
#include "utils.h"

namespace aff4 {


class AFF4DirectoryTest: public ::testing::Test {
 protected:
    std::string root_path = "/tmp/aff4_directory/";
    std::string member_name = "Foobar.txt";

    URN member_urn;

  // Remove the file on teardown.
  virtual void TearDown() {
      MemoryDataStore resolver;
      AFF4Directory::RemoveDirectory(&resolver, root_path);
  }

  // Create an initial container for each test.
  virtual void SetUp() {
      MemoryDataStore resolver;

      AFF4Flusher<AFF4Directory> volume;
      EXPECT_OK(AFF4Directory::NewAFF4Directory(
                    &resolver, root_path, true /* truncate */, volume));

      member_urn = volume->urn.Append(member_name);

      AFF4Flusher<AFF4Stream> member;
      EXPECT_OK(volume->CreateMemberStream(member_urn, member));

      member->Write("Hello world");
  }
};

TEST_F(AFF4DirectoryTest, CreateMemberStream) {
  MemoryDataStore resolver;

  AFF4Flusher<AFF4Directory> volume;
  EXPECT_OK(AFF4Directory::OpenAFF4Directory(
                &resolver, root_path, volume));

  // Check for member.
  AFF4Flusher<AFF4Stream> child;
  EXPECT_OK(volume->OpenMemberStream(member_urn, child));

  ASSERT_EQ(child->Read(10000), "Hello world");

  // Check that the metadata is carried over.
  XSDString filename;
  EXPECT_OK(resolver.Get(child->urn, AFF4_DIRECTORY_CHILD_FILENAME, filename));
  ASSERT_EQ(filename.SerializeToString(), member_name);
}

} // namespace aff4
