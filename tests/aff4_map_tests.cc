#include <gtest/gtest.h>
#include "aff4/libaff4.h"
#include <unistd.h>
#include <glog/logging.h>
#include "utils.h"

namespace aff4 {


class AFF4MapTest: public ::testing::Test {
 protected:
        std::string filename = "/tmp/aff4_test.zip";
        std::string source_filename = "/tmp/source.txt";
        std::string image_name = "image.dd";

    URN volume_urn;
    URN image_urn;
    URN image_urn_streamed;

  // Remove the file on teardown.
  virtual void TearDown() {
    unlink(filename.c_str());
    unlink(source_filename.c_str());
  }

  // Create a sparse AFF4Map stream with some data in it.
  virtual void SetUp() {
    MemoryDataStore resolver;

    AFF4Flusher<AFF4Stream> file;
    AFF4Flusher<ZipFile> zip;
    EXPECT_OK(NewFileBackedObject(&resolver, filename, "truncate", file));
    EXPECT_OK(ZipFile::NewZipFile(&resolver, std::move(file), zip));

    // Store the URN for the test to use.
    volume_urn = zip->urn;
    image_urn = volume_urn.Append(image_name);

    // Write Map image sequentially (Seek/Write method).
    {
        AFF4Flusher<AFF4Stream> data_stream;
        EXPECT_OK(AFF4Image::NewAFF4Image(
                      &resolver, image_urn.Append("data"),
                      zip.get(), data_stream));

        AFF4Flusher<AFF4Map> image;
        EXPECT_OK(AFF4Map::NewAFF4Map(
                      &resolver, image_urn, zip.get(),
                      data_stream.get(), image));

        resolver.logger->debug("Datastream urn {}", data_stream->urn);

        // Maps are written in random order.
        image->Seek(50, SEEK_SET);
        image->Write("XX - This is the position.");

        image->Seek(0, SEEK_SET);
        image->Write("00 - This is the position.");

        // We can "overwrite" data by writing the same range again.
        image->Seek(50, SEEK_SET);
        image->Write("50");
    }

    // Test the Stream method.
    {
        image_urn_streamed = image_urn.Append("streamed");

        // First create a stream and add some data in it.
        AFF4Flusher<AFF4Stream> source(new StringIO(&resolver));

        // Fill it with data.
        source->Write("AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHH");

        // Make a temporary map that defines our plan.
        AFF4Flusher<AFF4Map> helper_map(new AFF4Map(&resolver));

        helper_map->AddRange(4, 0, 4, source.get());    // 0000AAAA
        helper_map->AddRange(0, 12, 4, source.get());   // DDDDAAAA
        helper_map->AddRange(12, 16, 4, source.get());  // DDDDAAAA0000EEEE

        AFF4Flusher<AFF4Map> image;
        EXPECT_OK(AFF4Map::NewAFF4Map(
                      &resolver, image_urn_streamed, zip.get(),
                      nullptr, image));

        // Now we create the real map by copying the temporary map stream.
        DefaultProgress default_progress(&resolver);
        EXPECT_OK(
            AFF4Map::CopyStreamFromMap(helper_map.get(), image.get(),
                                       &default_progress));
    }
  }
};


TEST_F(AFF4MapTest, TestAddRange) {
  MemoryDataStore resolver;

  AFF4Flusher<AFF4Stream> file;
  AFF4Flusher<AFF4Volume> zip;
  EXPECT_OK(NewFileBackedObject(&resolver, filename, "read", file));
  EXPECT_OK(ZipFile::OpenZipFile(&resolver, std::move(file), zip));

  AFF4Flusher<AFF4Stream> a(new StringIO(&resolver));
  AFF4Flusher<AFF4Stream> b(new StringIO(&resolver));

  AFF4Flusher<AFF4Stream> devnul(new StringIO(&resolver));

  AFF4Flusher<AFF4Map> map;
  EXPECT_OK(AFF4Map::NewAFF4Map(
                &resolver, image_urn, zip.get(),
                devnul.get(), map));

  // First test - overlapping regions:
  map->AddRange(0, 0, 100, a.get());
  map->AddRange(10, 10, 100, a.get());

  // Should be merged into a single range.
  auto ranges = map->GetRanges();
  EXPECT_EQ(ranges.size(), 1);
  EXPECT_EQ(ranges[0].length, 110);

  map->Clear();

  // Repeating regions - should not be merged but first region should be
  // truncated.
  map->AddRange(0, 0, 100, a.get());
  map->AddRange(50, 0, 100, a.get());

  ranges = map->GetRanges();
  EXPECT_EQ(ranges.size(), 2);
  EXPECT_EQ(ranges[0].length, 50);

  // Inserted region. Should split existing region into three.
  map->Clear();

  map->AddRange(0, 0, 100, a.get());
  map->AddRange(50, 0, 10, b.get());

  ranges = map->GetRanges();
  EXPECT_EQ(ranges.size(), 3);
  EXPECT_EQ(ranges[0].length, 50);
  EXPECT_EQ(ranges[0].target_id, 0);

  EXPECT_EQ(ranges[1].length, 10);
  EXPECT_EQ(ranges[1].target_id, 1);

  EXPECT_EQ(ranges[2].length, 40);
  EXPECT_EQ(ranges[2].target_id, 0);

  // New range overwrites all the old ranges.
  map->AddRange(0, 0, 100, b.get());

  ranges = map->GetRanges();
  EXPECT_EQ(ranges.size(), 1);
  EXPECT_EQ(ranges[0].length, 100);
  EXPECT_EQ(ranges[0].target_id, 1);


  // Simulate writing contiguous regions. These should be merged into a single
  // region automatically.
  map->Clear();

  map->AddRange(0, 100, 10, a.get());
  map->AddRange(10, 110, 10, a.get());
  map->AddRange(20, 120, 10, a.get());
  map->AddRange(30, 130, 10, a.get());

  ranges = map->GetRanges();
  EXPECT_EQ(ranges.size(), 1);
  EXPECT_EQ(ranges[0].length, 40);
  EXPECT_EQ(ranges[0].target_id, 0);

  // Writing sparse image.
  map->Clear();

  map->AddRange(0, 100, 10, a.get());
  map->AddRange(30, 130, 10, a.get());

  ranges = map->GetRanges();
  EXPECT_EQ(ranges.size(), 2);
  EXPECT_EQ(ranges[0].length, 10);
  EXPECT_EQ(ranges[0].target_id, 0);
  EXPECT_EQ(ranges[1].length, 10);
  EXPECT_EQ(ranges[1].map_offset, 30);
  EXPECT_EQ(ranges[1].target_id, 0);

  // Now merge. Adding the missing region makes the image not sparse.
  map->AddRange(10, 110, 20, a.get());
  ranges = map->GetRanges();
  EXPECT_EQ(ranges.size(), 1);
  EXPECT_EQ(ranges[0].length, 40);
}


TEST_F(AFF4MapTest, CreateMapStream) {
  MemoryDataStore resolver;

  AFF4Flusher<AFF4Stream> file;
  AFF4Flusher<AFF4Volume> zip;
  EXPECT_OK(NewFileBackedObject(&resolver, filename, "read", file));
  EXPECT_OK(ZipFile::OpenZipFile(&resolver, std::move(file), zip));

  VolumeGroup volumes(&resolver);
  volumes.AddVolume(std::move(zip));

  AFF4Flusher<AFF4Map> map;
  EXPECT_OK(AFF4Map::OpenAFF4Map(
                &resolver, image_urn, &volumes, map));

  map->Seek(50, SEEK_SET);
  EXPECT_STREQ(map->Read(2).c_str(), "50");

  map->Seek(0, SEEK_SET);
  EXPECT_STREQ(map->Read(2).c_str(), "00");

  std::vector<Range> ranges = map->GetRanges();
  EXPECT_EQ(ranges.size(), 3);
  EXPECT_EQ(ranges[0].length, 26);
  EXPECT_EQ(ranges[0].map_offset, 0);
  EXPECT_EQ(ranges[0].target_offset, 26);

  // This is the extra "overwritten" 2 bytes which were appended to the end of
  // the target stream and occupy the map range from 50-52.
  EXPECT_EQ(ranges[1].length, 2);
  EXPECT_EQ(ranges[1].map_offset, 50);
  EXPECT_EQ(ranges[1].target_offset, 52);

  EXPECT_EQ(ranges[2].length, 24);
  EXPECT_EQ(ranges[2].map_offset, 52);
  EXPECT_EQ(ranges[2].target_offset, 2);

  // Test that reads outside the ranges null pad correctly.
  map->Seek(48, SEEK_SET);
  std::string read_string = map->Read(4);
  EXPECT_EQ(read_string[0], 0);
  EXPECT_EQ(read_string[1], 0);
  EXPECT_EQ(read_string[2], '5');
  EXPECT_EQ(read_string[3], '0');

  {
      AFF4Flusher<AFF4Map> streamed_map;
      EXPECT_OK(AFF4Map::OpenAFF4Map(
                    &resolver, image_urn_streamed, &volumes, streamed_map));

      EXPECT_EQ(streamed_map->Size(), 16);

      read_string = streamed_map->Read(1000);
      EXPECT_EQ(read_string, std::string("DDDDAAAA\0\0\0\0EEEE", 16));
  }

  // Check the untransformed data stream - it is written in the same order as
  // the ranges are given.
  {
      AFF4Flusher<AFF4Image> map_data;
      EXPECT_OK(AFF4Image::OpenAFF4Image(
                    &resolver, image_urn.Append("streamed").Append("data"),
                    &volumes, map_data));

    std::string read_string = map_data->Read(1000);
    EXPECT_STREQ(read_string.c_str(), "DDDDAAAAEEEE");
  }
}

} // namespace aff4
