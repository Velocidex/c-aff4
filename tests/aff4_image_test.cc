#include <gtest/gtest.h>
#include <libaff4.h>
#include <unistd.h>
#include <glog/logging.h>


class AFF4ImageTest: public ::testing::Test {
 protected:
  string filename = "/tmp/aff4_test.aff4";
  string image_name = "image.dd";
  URN volume_urn;
  URN image_urn;

  // Remove the file on teardown.
  virtual void TearDown() {
    //    unlink(filename.c_str());
  };

  // Create an AFF4Image stream with some data in it.
  virtual void SetUp() {
    MemoryDataStore resolver;

    unlink(filename.c_str());

    // First create the backing file.
    AFF4ScopedPtr<AFF4Stream> file = resolver.AFF4FactoryOpen<AFF4Stream>(
        filename);

    ASSERT_TRUE(file.get()) << "Unable to create file";

    // The backing file is given to the volume.
    AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(
        &resolver, file->urn);

    // Now an image is created inside the volume.
    AFF4ScopedPtr<AFF4Image> image = AFF4Image::NewAFF4Image(
        &resolver, image_name, zip->urn);

    // For testing - rediculously small chunks. This will create many bevies.
    image->chunk_size = 10;
    image->chunks_per_segment = 3;

    for(int i=0; i<100; i++) {
      image->sprintf("Hello world %02d!", i);
    };

    image_urn = image->urn;
    volume_urn = zip->urn;
  };
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
};


TEST_F(AFF4ImageTest, TestAFF4ImageStream) {
  MemoryDataStore resolver;

  // Load the zip file into the resolver.
  AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(
      &resolver, filename);

  ASSERT_TRUE(zip.get());

  AFF4ScopedPtr<AFF4Image> image = resolver.AFF4FactoryOpen<AFF4Image>(
      image_urn);

  ASSERT_TRUE(image.get()) << "Unable to open the image urn!";

  unique_ptr<StringIO> stream_copy = StringIO::NewStringIO();
  for(int i=0; i<100; i++) {
    stream_copy->sprintf("Hello world %02d!", i);
  };

  for(int i=0; i<1500; i+=25) {
    image->Seek(i, SEEK_SET);
    stream_copy->Seek(i, SEEK_SET);

    // Randomly read buffers in the image to ensure Seek/Read works.
    string read_data = image->Read(13);
    string expected_data = stream_copy->Read(13);

    LOG(INFO) << "Expected:" << expected_data.c_str();
    LOG(INFO) << "Read:" << read_data.c_str();

    EXPECT_STREQ(expected_data.c_str(), read_data.c_str());
  };

};
