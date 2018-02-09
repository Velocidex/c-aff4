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

#include "config.h"
#include "aff4_io.h"

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
AFF4Status CompressSnappy_(const char* data, size_t length, std::string* output);
AFF4Status DeCompressSnappy_(const char* data, size_t length, std::string* output);


// This is the type written to the map stream in this exact binary layout.
struct BevyIndex {
    uint64_t offset = 0;   // Offset of the chunk within the bevy.
    uint32_t length = 0;   // Length of the compressed chunk.
} __attribute__((packed));


class _BevyWriter;


class AFF4Image: public AFF4Stream {
  protected:
    AFF4Status FlushBevy();

    // Convert the legecy formatted bevvy data into the new format.
    std::string _FixupBevyData(std::string* data);

    int _ReadPartial(
        unsigned int chunk_id, int chunks_to_read, std::string& result);

    AFF4Status ReadChunkFromBevy(
        std::string& result, unsigned int chunk_id,
        AFF4ScopedPtr<AFF4Stream>& bevy, BevyIndex bevy_index[],
        uint32_t index_size);

    // The below are used to implement variable sized write support
    // through the Write() interfaces. This is not recommmended - it
    // is more efficient to write the image using the WriteStream()
    // interface.

    // Accept small writes into this buffer - when we have a complete
    // chunk we can sent it to the bevy writer.
    std::string buffer;

    // Collect chunks here for the current bevy.
    std::unique_ptr<_BevyWriter> bevy_writer;

    // The current bevy we write into.
    unsigned int bevy_number = 0;

    // The next chunk to write in the bevy.
    unsigned int chunk_count_in_bevy = 0;

    // Called when we finished the image to finalize the metadata
    AFF4Status _write_metadata();

    // FALSE if stream is aff4:ImageStream, true if stream is aff4:stream.
    bool isAFF4Legacy = false;

  public:
    AFF4Image(DataStore* resolver, URN urn);
    explicit AFF4Image(DataStore* resolver);

    unsigned int chunk_size = 32*1024;    /**< The number of bytes in each
                                         * chunk. */
    unsigned int chunks_per_segment = 1024; /**< Maximum number of chunks in each
                                           * Bevy. */

    // Which compression should we use.
    AFF4_IMAGE_COMPRESSION_ENUM compression = AFF4_IMAGE_COMPRESSION_ENUM_ZLIB;

    /**
     * Create a new AFF4Image instance.
     *
     * After callers receive a new AFF4Image object they may modify the parameters
     * before calling Write().
     *
     * @param image_urn: The URN of the stream which will be created in the
     *                   volume.
     *
     * @param volume: An AFF4Volume instance. We take a shared reference to the
     *                volume object and write segments into it as required.
     *
     * @return A unique reference to a new AFF4Image object.
     */
    static AFF4ScopedPtr<AFF4Image> NewAFF4Image(
        DataStore* resolver, const URN& image_urn, const URN& volume_urn);

    /**
     * Load the file from an AFF4 URN.
     *
     *
     * @return
     */
    AFF4Status LoadFromURN() override;


    /**
     * An optimized WriteStream() API.
     */
    AFF4Status WriteStream(
        AFF4Stream* source,
        ProgressContext* progress = nullptr) override;

    AFF4Status Write(const char* data, int length) override;

    /**
     * Read data from the current read pointer.
     *
     * @param length: How much data to read.
     *
     * @return A string containing the data to read.
     */
    std::string Read(size_t length) override;

    AFF4Status Flush() override;

    using AFF4Stream::Write;
};


// The AFF4 Standard specifies an "AFF4 Image" as an abstract
// container for image related properties. It is not actually a
// concrete stream but it refers to a storage stream using its
// aff4:dataStream property.

// We implement a read-only stream which delegates reads to the
// underlying storage stream. This is necessary in order to be able to
// open such a stream for reading.

// Note that to create such a stream, you can simply set the
// aff4:dataStream to a concerete Map or ImageStream.
class AFF4StdImage : public AFF4Stream {
 public:
   AFF4StdImage(DataStore* resolver, URN urn): AFF4Stream(resolver, urn) {}
   explicit AFF4StdImage(DataStore* resolver): AFF4Stream(resolver) {}

    static AFF4ScopedPtr<AFF4StdImage> NewAFF4StdImage(
        DataStore* resolver, const URN& image_urn, const URN& volume_urn);

    AFF4Status LoadFromURN() override;

    std::string Read(size_t length) override;

 protected:
    URN delegate;
};



extern void aff4_image_init();

} // namespace aff4


#endif  // SRC_AFF4_IMAGE_H_
