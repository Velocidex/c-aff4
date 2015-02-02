#include <gtest/gtest.h>
#include <libaff4.h>
#include <unistd.h>

class AFF4ImageTest: public ::testing::Test {
 protected:
  string filename = "/tmp/aff4_test.aff4";
  string image_name = "image.dd";
  URN volume_urn;
  URN image_urn;

  // Remove the file on teardown.
  virtual void TearDown() {
    unlink(filename.c_str());
  };

  // Create an AFF4Image stream with some data in it.
  virtual void SetUp() {
    MemoryDataStore resolver;

    // First create the backing file.
    AFF4Stream* file = AFF4FactoryOpen<AFF4Stream>(&resolver, filename);

    ASSERT_TRUE(file) << "Unable to create file";

    // The backing file is given to the volume.
    AFF4Volume* zip = ZipFile::NewZipFile(&resolver, file->urn);

    // Now an image is created inside the volume.
    AFF4Image *image = AFF4Image::NewAFF4Image(&resolver, image_name, zip->urn);

    // For testing - rediculously small chunks. This will create many segments.
    image->chunk_size = 10;
    image->chunks_per_segment = 3;

    for(int i=0; i<100; i++) {
      image->sprintf("Hello world %d!", i);
    };

    image_urn = image->urn;
    volume_urn = zip->urn;
  };
};

TEST_F(AFF4ImageTest, OpenImageByURN) {
  MemoryDataStore resolver;

  // Load the zip file into the resolver.
  AFF4Volume* zip = ZipFile::NewZipFile(&resolver, filename);

  ASSERT_TRUE(zip);

  AFF4Image *image = AFF4FactoryOpen<AFF4Image>(&resolver, image_urn);

  ASSERT_TRUE(image) << "Unable to open the image urn!";

  // Ensure the newly opened image has the correct parameters.
  ASSERT_EQ(image->chunk_size, 10);
  ASSERT_EQ(image->chunks_per_segment, 3);

};
