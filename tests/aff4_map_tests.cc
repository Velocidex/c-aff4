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
    //unlink(filename.c_str());
  };

  // Create a sparse AFF4Map stream with some data in it.
  virtual void SetUp() {
    MemoryDataStore resolver;

    // We are allowed to write on the output filename.
    resolver.Set(filename, AFF4_STREAM_WRITE_MODE, new XSDString("truncate"));

    // The backing file is given to the volume.
    AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, filename);

    // Now an image is created inside the volume.
    AFF4ScopedPtr<AFF4Map> image = AFF4Map::NewAFF4Map(
        &resolver, zip->urn.Append(image_name), zip->urn);

    image->MarkDirty();

    if(0) {
      // Maps are written in random order.
      image->Seek(50, SEEK_SET);
      image->Write("50 - This is the position.");

      image->Seek(0, SEEK_SET);
      image->Write("00 - This is the position.");
    };

    // Store the URN for the test to use.
    image_urn = image->urn;
    volume_urn = zip->urn;
  };
};


TEST_F(AFF4MapTest, TestAddRange) {
  MemoryDataStore resolver;
  vector<Range> ranges;

  // Load the zip file into the resolver.
  AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, filename);

  ASSERT_TRUE(zip.get());

  AFF4ScopedPtr<AFF4Map> map = AFF4Map::NewAFF4Map(
      &resolver, volume_urn.Append(map_name), volume_urn);

  ASSERT_TRUE(map.get());

  // First test - overlapping regions:
  map->AddRange(0, 0, 100, "a");
  map->AddRange(10, 10, 100, "a");

  // Should be merged into a single range.
  ranges = map->GetRanges();
  EXPECT_EQ(ranges.size(), 1);
  EXPECT_EQ(ranges[0].length, 110);

  map->Clear();

  // Repeating regions - should not be merged but first region should be
  // truncated.
  map->AddRange(0, 0, 100, "a");
  map->AddRange(50, 0, 100, "a");

  ranges = map->GetRanges();
  EXPECT_EQ(ranges.size(), 2);
  EXPECT_EQ(ranges[0].length, 50);

  // Inserted region. Should split existing region into three.
  map->Clear();

  map->AddRange(0, 0, 100, "a");
  map->AddRange(50, 0, 10, "b");

  ranges = map->GetRanges();
  EXPECT_EQ(ranges.size(), 3);
  EXPECT_EQ(ranges[0].length, 50);
  EXPECT_EQ(ranges[0].target_id, 0);

  EXPECT_EQ(ranges[1].length, 10);
  EXPECT_EQ(ranges[1].target_id, 1);

  EXPECT_EQ(ranges[2].length, 40);
  EXPECT_EQ(ranges[2].target_id, 0);

  // New range overwrites all the old ranges.
  map->AddRange(0, 0, 100, "b");

  ranges = map->GetRanges();
  EXPECT_EQ(ranges.size(), 1);
  EXPECT_EQ(ranges[0].length, 100);
  EXPECT_EQ(ranges[0].target_id, 1);


  // Simulate writing contiguous regions. These should be merged into a single
  // region automatically.
  map->Clear();

  map->AddRange(0, 100, 10, "a");
  map->AddRange(10, 110, 10, "a");
  map->AddRange(20, 120, 10, "a");
  map->AddRange(30, 130, 10, "a");

  ranges = map->GetRanges();
  EXPECT_EQ(ranges.size(), 1);
  EXPECT_EQ(ranges[0].length, 40);
  EXPECT_EQ(ranges[0].target_id, 0);

  // Writing sparse image.
  map->Clear();

  map->AddRange(0, 100, 10, "a");
  map->AddRange(30, 130, 10, "a");

  ranges = map->GetRanges();
  EXPECT_EQ(ranges.size(), 2);
  EXPECT_EQ(ranges[0].length, 10);
  EXPECT_EQ(ranges[0].target_id, 0);
  EXPECT_EQ(ranges[1].length, 10);
  EXPECT_EQ(ranges[1].map_offset, 30);
  EXPECT_EQ(ranges[1].target_id, 0);

  // Now merge. Adding the missing region makes the image not sparse.
  map->AddRange(10, 110, 20, "a");
  ranges = map->GetRanges();
  EXPECT_EQ(ranges.size(), 1);
  EXPECT_EQ(ranges[0].length, 40);
};


TEST_F(AFF4MapTest, CreateMapStream) {
  MemoryDataStore resolver;

  // Load the zip file into the resolver.
  AFF4ScopedPtr<ZipFile> zip = ZipFile::NewZipFile(&resolver, filename);

  ASSERT_TRUE(zip.get());

  AFF4ScopedPtr<AFF4Map> map = AFF4Map::NewAFF4Map(
      &resolver, volume_urn.Append(map_name), volume_urn);

  ASSERT_TRUE(map.get());

  map->Seek(50, SEEK_SET);
  EXPECT_STREQ(map->Read(2).c_str(), "50");

  map->Seek(0, SEEK_SET);
  EXPECT_STREQ(map->Read(2).c_str(), "00");
};
