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

#ifndef SRC_AFF4_IMAGE_H_
#define SRC_AFF4_IMAGE_H_

#include "aff4/config.h"
#include "aff4/aff4_io.h"
#include "aff4/volume_group.h"

#include <unordered_map>

namespace aff4 {


/**
 * An AFF4Image is an object which stores an image inside an AFF4Volume.
 *
 * The image data is split into *Bevies*. A Bevy contains a large number of
 * chunks and is stored as a single member of the ZipFile.

 Example usage:

~~~~~~~~~~~~~{.c}
  unique_ptr<DataStore> resolver(new MemoryDataStore());
  AFF4Volume* zip = ZipFile::NewZipFile(resolver, "test.zip");

  AFF4Stream *image = AFF4Image::NewAFF4Image("image.dd", zip->urn);

  // Can only modify the image attributes before the first write.
  image->chunks_per_segment = 100;

  image->Write("Hello wolrd!");
~~~~~~~~~~~~~

 Will result in a zip file containing a bevy and a bevy index member:

~~~~~~~~~~~~~
  Archive:  test.zip
  aff4:/9632a8a4-ed83-4564-ba5a-492271985d80
  Length      Date    Time    Name
  ---------  ---------- -----   ----
       20  2015-01-18 17:29   image.dd/00000000
        4  2015-01-18 17:29   image.dd/00000000.index
      538  2015-01-18 17:29   information.yaml
      434  2015-01-18 17:29   information.turtle
  ---------                     -------
      996                     4 files
~~~~~~~~~~~~~

 */

// Compression methods we support.
AFF4Status CompressZlib_(const char* data, size_t length, std::string* output);
AFF4Status DeCompressZlib_(const char* data, size_t length, std::string* output);
AFF4Status CompressDeflate_(const char* data, size_t length, std::string* output);
AFF4Status DeCompressDeflate_(const char* data, size_t length, std::string* output);
AFF4Status CompressSnappy_(const char* data, size_t length, std::string* output);
AFF4Status DeCompressSnappy_(const char* data, size_t length, std::string* output);
AFF4Status CompressLZ4_(const char* data, size_t length, std::string* output);
AFF4Status DeCompressLZ4_(const char* data, size_t length, std::string* output);


// This is the type written to the map stream in this exact binary layout.
struct BevyIndex {
    uint64_t offset = 0;   // Offset of the chunk within the bevy.
    uint32_t length = 0;   // Length of the compressed chunk.
} __attribute__((packed));


class _BevyWriter;

struct _BevyWriterDeleter {
    void operator()(_BevyWriter *p);
};

class AFF4Image: public AFF4Stream {
  protected:
    AFF4Status FlushBevy();

    // Convert the legecy formatted bevvy data into the new format.
    std::string _FixupBevyData(std::string* data);

    int _ReadPartial(
        unsigned int chunk_id, int chunks_to_read, std::string& result);

    AFF4Status ReadChunkFromBevy(
        std::string& result, unsigned int chunk_id,
        AFF4Flusher<AFF4Stream>& bevy, BevyIndex bevy_index[],
        uint32_t index_size);

    // The below are used to implement variable sized write support
    // through the Write() interfaces. This is not recommmended - it
    // is more efficient to write the image using the WriteStream()
    // interface.

    // Accept small writes into this buffer - when we have a complete
    // chunk we can sent it to the bevy writer.
    std::string buffer;

    // Collect chunks here for the current bevy.
    std::unique_ptr<_BevyWriter, _BevyWriterDeleter> bevy_writer;

    // The current bevy we write into.
    unsigned int bevy_number = 0;

    // The next chunk to write in the bevy.
    unsigned int chunk_count_in_bevy = 0;

    // Called when we finished the image to finalize the metadata
    AFF4Status _write_metadata();

    // FALSE if stream is aff4:ImageStream, true if stream is aff4:stream.
    bool isAFF4Legacy = false;

    // Simple cache of decompressed bevys
    std::unordered_map<unsigned int, std::string> chunk_cache{};

    // When this is true it is ok to switch volumes. This flag will
    // only be true when the AFF4Image has flushed all its bevies to
    // the current volume.
    bool checkpointed = true;

  public:
    AFF4Image(DataStore* resolver, URN urn);

    explicit AFF4Image(DataStore* resolver);

    unsigned int chunk_size = 32*1024;    /** The number of bytes in each
                                           * chunk. */
    unsigned int chunks_per_segment = 1024; /** Maximum number of chunks in each
                                             * Bevy. */
    unsigned int chunk_cache_size = 1024; /** The max number of cached chunks */

    bool CanSwitchVolume() override;
    AFF4Status SwitchVolume(AFF4Volume *volume) override;

    // Which compression should we use.
    AFF4_IMAGE_COMPRESSION_ENUM compression = AFF4_IMAGE_COMPRESSION_ENUM_DEFLATE;

    static AFF4Status NewAFF4Image(
        DataStore* resolver,
        URN image_urn,
        // An initial reference to the current volume.
        AFF4Volume *volume,
        AFF4Flusher<AFF4Image> &result);

    static AFF4Status NewAFF4Image(
        DataStore* resolver,
        URN image_urn,
        // An initial reference to the current volume.
        AFF4Volume *volume,
        AFF4Flusher<AFF4Stream> &result);

    // Open an existing AFF4 Image stream.
    static AFF4Status OpenAFF4Image(
        DataStore *resolver,
        URN image_urn,
        VolumeGroup *volumes,
        AFF4Flusher<AFF4Image> &result);

    // Borrowed reference to the current volume. We need to access the
    // volume when writing so it should be valid when a write is
    // issued (unused for reading).
    AFF4Volume *current_volume;

    // A borrowed reference to the volume group. We read bevies from
    // one of these volumes (unused for writing).
    VolumeGroup *volumes;

    /**
     * An optimized WriteStream() API.
     */
    AFF4Status WriteStream(
        AFF4Stream* source,
        ProgressContext* progress = nullptr) override;

    AFF4Status Write(const char* data, size_t length) override;

    AFF4Status ReadBuffer(char* data, size_t* length) override;

    AFF4Status Flush() override;

    using AFF4Stream::Write;
};

extern void aff4_image_init();

} // namespace aff4


#endif  // SRC_AFF4_IMAGE_H_
