#include <gtest/gtest.h>
#include <libaff4.h>
#include <unistd.h>
#include <glog/logging.h>

class AFF4MapTest: public ::testing::Test {
 protected:
  string filename = "/tmp/aff4_test.zip";
  string image_name = "image.dd";
  string map_name = "test_map";

  URN volume_urn;
  URN image_urn;

  // Remove the file on teardown.
  virtual void TearDown() {
    unlink(filename.c_str());
  };

  // Create an AFF4Image stream with some data in it.
  virtual void SetUp() {
    MemoryDataStore resolver;

    unlink(filename.c_str());

    // We are allowed to write on the output filename.
    resolver.Set(filename, AFF4_STREAM_WRITE_MODE, new XSDString("truncate"));

    // The backing file is given to the volume.
    AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, filename);

    // Now an image is created inside the volume.
    AFF4ScopedPtr<AFF4Image> image = AFF4Image::NewAFF4Image(
        &resolver, zip->urn.Append(image_name), zip->urn);

    // For testing - rediculously small chunks. This will create many bevies.
    for(int i=0; i<100; i++) {
      image->sprintf("Hello world %02d!", i);
    };

    // Store the URN for the test to use.
    image_urn = image->urn;
    volume_urn = zip->urn;
  };
};


TEST_F(AFF4MapTest, CreateMapStream) {
  MemoryDataStore resolver;

  // Load the zip file into the resolver.
  AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, filename);

  ASSERT_TRUE(zip.get());

  AFF4ScopedPtr<AFF4Map> map = AFF4Map::NewAFF4Map(
      &resolver, volume_urn.Append(map_name), volume_urn);

  ASSERT_TRUE(map.get());



};
