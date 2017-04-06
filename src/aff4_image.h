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
struct BevvyIndex {
    uint64_t offset = 0;
    uint32_t length = 0;
} __attribute__((packed));


class AFF4Image: public AFF4Stream {
  protected:
    // Delegates to the workers for support compression methods.
    AFF4Status FlushChunk(const char* data, size_t length);

    AFF4Status _FlushBevy();

    // Convert the legecy formatted bevvy data into the new format.
    std::string _FixupBevvyData(std::string* data);

    int _ReadPartial(
        unsigned int chunk_id, int chunks_to_read, std::string& result);

    AFF4Status _ReadChunkFromBevy(
        std::string& result, unsigned int chunk_id,
        AFF4ScopedPtr<AFF4Stream>& bevy, BevvyIndex bevy_index[],
        uint32_t index_size);

    std::string buffer;

    // The current bevy we write into.
    StringIO bevy_index;
    StringIO bevy;

    unsigned int bevy_number = 0;           /**< The current bevy number for
                                           * writing. */
    unsigned int chunk_count_in_bevy = 0;

    URN volume_urn;                       /**< The Volume we are stored on. */

    AFF4Status _write_metadata();

    // FALSE if stream is aff4:ImageStream, true if stream is aff4:stream.
    bool isAFF4Legacy = false;

  public:
    AFF4Image(DataStore* resolver, URN urn): AFF4Stream(resolver, urn) {}
    explicit AFF4Image(DataStore* resolver): AFF4Stream(resolver) {}

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
    virtual AFF4Status LoadFromURN();


    /**
     * An optimized WriteStream() API.
     */
    virtual AFF4Status WriteStream(
        AFF4Stream* source,
        ProgressContext* progress = nullptr);

    virtual int Write(const char* data, int length);

    /**
     * Read data from the current read pointer.
     *
     * @param length: How much data to read.
     *
     * @return A string containing the data to read.
     */
    virtual std::string Read(size_t length);


    AFF4Status Flush();

    using AFF4Stream::Write;
};

extern void aff4_image_init();

#endif  // SRC_AFF4_IMAGE_H_
