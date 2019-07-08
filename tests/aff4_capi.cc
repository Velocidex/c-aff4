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
#include <glog/logging.h>

#include "aff4/libaff4-c.h"

namespace aff4 {


void printBuffer(const char* buffer, int size) {
    for (int i = 0; i < size; i++) {
        if (i > 0) {
            printf(":");
        }
        char ch = buffer[i];
        printf("%02X", (ch & 0xff));
    }
    printf("\n");
}

class AFF4CAPI : public ::testing::Test {
protected:
    const std::string reference_images = "ReferenceImages/";
};


TEST_F(AFF4CAPI, Sample1URN) {
    std::string filename = reference_images + "AFF4Std/Base-Linear.aff4";

    AFF4_set_verbosity(AFF4_LOG_LEVEL_TRACE);

    AFF4_Handle* handle = AFF4_open(filename.c_str(), nullptr);
    ASSERT_TRUE(handle);

    uint64_t size = AFF4_object_size(handle, nullptr);
    ASSERT_EQ(268435456, size);

    char buffer[33];
    memset(buffer, 0, 33);
    ssize_t read = AFF4_read(handle, 0, buffer, 32, nullptr);
    ASSERT_EQ(32, read);
    ASSERT_STREQ("\x33\xC0\x8E\xD0\xBC\x00\x7C\x8E\xC0\x8E\xD8\xBE\x00\x7C\xBF\x00\x06\xB9\x00\x02\xFC\xF3\xA4\x50\x68\x1C\x06\xCB\xFB\xB9\x04\x00", buffer);

    AFF4_close(handle, nullptr);
}

TEST_F(AFF4CAPI, Sample2URN) {
    std::string filename = reference_images + "AFF4Std/Base-Allocated.aff4";

    AFF4_Handle* handle = AFF4_open(filename.c_str(), nullptr);
    ASSERT_TRUE(handle);

    uint64_t size = AFF4_object_size(handle, nullptr);
    ASSERT_EQ(268435456, size);

    char buffer[33];
    memset(buffer, 0, 33);

    // Start
    ssize_t read = AFF4_read(handle, 0, buffer, 32, nullptr);
    ASSERT_EQ(32, read);
    ASSERT_STREQ("\x33\xC0\x8E\xD0\xBC\x00\x7C\x8E\xC0\x8E\xD8\xBE\x00\x7C\xBF\x00\x06\xB9\x00\x02\xFC\xF3\xA4\x50\x68\x1C\x06\xCB\xFB\xB9\x04\x00", buffer);

    // Unreadable
    memset(buffer, 0, 33);
    read = AFF4_read(handle, 32326 * 512, buffer, 32, nullptr);
    ASSERT_EQ(32, read);
    ASSERT_STREQ("\x4D\xD9\x8B\x8C\xE2\x39\x44\x58\x6B\xA2\xA8\xDB\x04\x1C\x6D\x36\x81\x41\x36\x8B\x90\xA7\x16\xC2\x5E\x9A\x0C\xA6\xE6\xD9\x0B\x7E", buffer);

    AFF4_close(handle, nullptr);
}

TEST_F(AFF4CAPI, Sample3URN) {
    std::string filename = reference_images + "AFF4Std/Base-Linear-ReadError.aff4";
    AFF4_Handle* handle = AFF4_open(filename.c_str(), nullptr);
    ASSERT_TRUE(handle);

    uint64_t size = AFF4_object_size(handle, nullptr);
    ASSERT_EQ(268435456, size);

    char buffer[33];
    memset(buffer, 0, 33);

    // Start...
    ssize_t read = AFF4_read(handle, 0, buffer, 32, nullptr);
    ASSERT_EQ(32, read);
    ASSERT_STREQ("\x33\xC0\x8E\xD0\xBC\x00\x7C\x8E\xC0\x8E\xD8\xBE\x00\x7C\xBF\x00\x06\xB9\x00\x02\xFC\xF3\xA4\x50\x68\x1C\x06\xCB\xFB\xB9\x04\x00", buffer);

    // Unreadable
    memset(buffer, 0, 32);
    read = AFF4_read(handle, 32326 * 512, buffer, 32, nullptr);
    ASSERT_EQ(32, read);
    ASSERT_STREQ("\x55\x4E\x52\x45\x41\x44\x41\x42\x4C\x45\x44\x41\x54\x41\x55\x4E\x52\x45\x41\x44\x41\x42\x4C\x45\x44\x41\x54\x41\x55\x4E\x52\x45", buffer);

    AFF4_close(handle, nullptr);
}

} // namespace aff4
