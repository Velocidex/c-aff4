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
#include <glog/logging.h>


class AFF4ImageTest: public ::testing::Test {
 protected:
  std::string filename = "/tmp/aff4_test.zip";
  std::string image_name = "image.dd";
  URN volume_urn;
  URN image_urn;
  URN image_urn_2;
  URN image_urn_stream;

  // Remove the file on teardown.
  virtual void TearDown() {
    unlink(filename.c_str());
  }

  // Create an AFF4Image stream with some data in it.
  virtual void SetUp() {
    MemoryDataStore resolver;

    unlink(filename.c_str());

    // We are allowed to write on the output filename.
    resolver.Set(filename, AFF4_STREAM_WRITE_MODE, new XSDString("truncate"));

    // The backing file is given to the volume.
    AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(
        &resolver, filename);

    volume_urn = zip->urn;

    {
      image_urn = zip->urn.Append(image_name);

      // Now an image is created inside the volume.
      AFF4ScopedPtr<AFF4Image> image = AFF4Image::NewAFF4Image(
          &resolver, image_urn, zip->urn);
      ASSERT_NE(image.get(), nullptr);


      // For testing - rediculously small chunks. This will create many bevies.
      image->chunk_size = 10;
      image->chunks_per_segment = 3;

      for (int i = 0; i < 100; i++) {
        image->sprintf("Hello world %02d!", i);
      }
    }

    // Now test snappy compression in images.
    {
      image_urn_2 = image_urn.Append("2");

      AFF4ScopedPtr<AFF4Image> image_2 = AFF4Image::NewAFF4Image(
          &resolver, image_urn_2, zip->urn);

      ASSERT_NE(image_2.get(), nullptr);
      // Make the second image use snappy for compression.
      image_2->compression = AFF4_IMAGE_COMPRESSION_ENUM_SNAPPY;
      image_2->Write("This is a test");
    }

    // Now test the streaming API.
    {
    	std::unique_ptr<AFF4Stream> test_stream = StringIO::NewStringIO();
      test_stream->Write("This is a test");
      test_stream->Seek(0, SEEK_SET);

      image_urn_stream = zip->urn.Append(image_name).Append("stream");

      // Now an image is created inside the volume.
      AFF4ScopedPtr<AFF4Image> image = AFF4Image::NewAFF4Image(
          &resolver, image_urn_stream, zip->urn);
      ASSERT_NE(image.get(), nullptr);
      // For testing - rediculously small chunks. This will create many bevies.
      image->chunk_size = 10;
      image->chunks_per_segment = 3;
      image->compression = AFF4_IMAGE_COMPRESSION_ENUM_SNAPPY;

      image->WriteStream(test_stream.get());
    }
  }
};

TEST_F(AFF4ImageTest, OpenImageByURN) {
  MemoryDataStore resolver;

  // Load the zip file into the resolver.
  AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, filename);

  ASSERT_TRUE(zip.get());

  AFF4ScopedPtr<AFF4Image> image = resolver.AFF4FactoryOpen<AFF4Image>(
      image_urn);

  ASSERT_TRUE(image.get()) << "Unable to open the image urn!";

  // Ensure the newly opened image has the correct parameters.
  EXPECT_EQ(image->chunk_size, 10);
  EXPECT_EQ(image->chunks_per_segment, 3);

  EXPECT_STREQ(
      "Hello world 00!Hello world 01!Hello world 02!Hello world 03!"
      "Hello world 04!Hello world 05!Hello worl",
      image->Read(100).c_str());

  EXPECT_EQ(1500, image->Size());
}


TEST_F(AFF4ImageTest, TestAFF4ImageStream) {
  MemoryDataStore resolver;

  // Load the zip file into the resolver.
  AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(
      &resolver, filename);

  ASSERT_TRUE(zip.get());

  AFF4ScopedPtr<AFF4Image> image = resolver.AFF4FactoryOpen<AFF4Image>(
      image_urn);

  ASSERT_TRUE(image.get()) << "Unable to open the image urn!";

  std::unique_ptr<StringIO> stream_copy = StringIO::NewStringIO();
  for (int i = 0; i < 100; i++) {
    stream_copy->sprintf("Hello world %02d!", i);
  }

  for (int i = 0; i < 1500; i += 25) {
    image->Seek(i, SEEK_SET);
    stream_copy->Seek(i, SEEK_SET);

    // Randomly read buffers in the image to ensure Seek/Read works.
    std::string read_data = image->Read(13);
    std::string expected_data = stream_copy->Read(13);

    LOG(INFO) << "Expected:" << expected_data.c_str();
    LOG(INFO) << "Read:" << read_data.c_str();

    EXPECT_STREQ(expected_data.c_str(), read_data.c_str());
  }

  {
    // Now test snappy decompression.
    AFF4ScopedPtr<AFF4Image> image_2 = resolver.AFF4FactoryOpen<AFF4Image>(
        image_urn_2);

    ASSERT_TRUE(image_2.get()) << "Unable to open the image urn!";

    URN compression_urn;
    EXPECT_EQ(resolver.Get(image_2->urn, AFF4_IMAGE_COMPRESSION, compression_urn),
              STATUS_OK);

    EXPECT_STREQ(compression_urn.SerializeToString().c_str(),
                 AFF4_IMAGE_COMPRESSION_SNAPPY);

    std::string data = image_2->Read(100);
    EXPECT_STREQ(data.c_str(), "This is a test");
  }

  // Now test streaming interface.
  {
    // Now test snappy decompression.
    AFF4ScopedPtr<AFF4Image> image_stream = resolver.AFF4FactoryOpen<AFF4Image>(
        image_urn_stream);

    ASSERT_TRUE(image_stream.get()) << "Unable to open the image urn!";

    URN compression_urn;
    EXPECT_EQ(resolver.Get(image_stream->urn, AFF4_IMAGE_COMPRESSION, compression_urn),
              STATUS_OK);

    EXPECT_STREQ(compression_urn.SerializeToString().c_str(),
                 AFF4_IMAGE_COMPRESSION_SNAPPY);

    std::string data = image_stream->Read(100);
    EXPECT_STREQ(data.c_str(), "This is a test");
  }
}
