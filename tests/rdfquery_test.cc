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
#include <string>
#include <gtest/gtest.h>
#include "aff4/libaff4.h"
#include <unistd.h>
#include <glog/logging.h>
#include "utils.h"


namespace aff4 {


class AFF4ImageRDFQuery : public ::testing::Test {
protected:
    const std::string reference_images = "ReferenceImages/";
};


TEST_F(AFF4ImageRDFQuery, Sample1URN) {
    std::string filename = reference_images + "AFF4Std/Base-Linear.aff4";

    MemoryDataStore resolver;

    AFF4Flusher<AFF4Stream> file;
    AFF4Flusher<AFF4Volume> zip;
    EXPECT_OK(NewFileBackedObject(&resolver, filename, "read", file));
    EXPECT_OK(ZipFile::OpenZipFile(&resolver, std::move(file), zip));

    // Query the resolver.
    const URN type(AFF4_IMAGE_TYPE);
    std::unordered_set<URN> images = resolver.Query(URN(AFF4_TYPE), &type);

    ASSERT_EQ(1, images.size());
    for(URN u : images){
        ASSERT_EQ(
            "aff4://cf853d0b-5589-4c7c-8358-2ca1572b87eb",
            u.SerializeToString());
    }
}

TEST_F(AFF4ImageRDFQuery, Sample2URN) {
    std::string filename = reference_images + "AFF4Std/Base-Allocated.aff4";

    MemoryDataStore resolver;

    AFF4Flusher<AFF4Stream> file;
    AFF4Flusher<AFF4Volume> zip;
    EXPECT_OK(NewFileBackedObject(&resolver, filename, "read", file));
    EXPECT_OK(ZipFile::OpenZipFile(&resolver, std::move(file), zip));

    const URN type(AFF4_IMAGE_TYPE);
    std::unordered_set<URN> images = resolver.Query(URN(AFF4_TYPE), &type);
    ASSERT_EQ(1, images.size());
    for(URN u : images){
        ASSERT_EQ("aff4://8fcced2b-989f-4f51-bfa2-38d4a4d818fe",
                  u.SerializeToString());
    }
}

TEST_F(AFF4ImageRDFQuery, Sample3URN) {
    std::string filename = reference_images + "AFF4Std/Base-Linear-ReadError.aff4";

    MemoryDataStore resolver;

    AFF4Flusher<AFF4Stream> file;
    AFF4Flusher<AFF4Volume> zip;
    EXPECT_OK(NewFileBackedObject(&resolver, filename, "read", file));
    EXPECT_OK(ZipFile::OpenZipFile(&resolver, std::move(file), zip));

    const URN type(AFF4_IMAGE_TYPE);
    std::unordered_set<URN> images = resolver.Query(URN(AFF4_TYPE), &type);
    ASSERT_EQ(1, images.size());
    for(URN u : images){
        ASSERT_EQ("aff4://3a873665-7bf6-47b5-a12a-d6632a58ddf9",
                  u.SerializeToString());
    }
}

} // namespace aff4
