/*
Copyright 2014 Google Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use
this file except in compliance with the License.  You may obtain a copy of the
License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.  See the License for the
specific language governing permissions and limitations under the License.
*/

#ifndef     SRC_ZIP_H_
#define     SRC_ZIP_H_

#include "config.h"

#include "aff4_errors.h"
#include "aff4_io.h"
#include "data_store.h"
#include <string.h>
#include <zlib.h>
#include <list>

using std::list;

/* This is the largest file size which may be represented by a regular
   zip file without using Zip64 extensions.
*/
#define  ZIP64_LIMIT ((1LL << 31)-1)


/** These are ZipFile structures */
struct EndCentralDirectory {
    uint32_t magic = 0x6054b50;
    uint16_t number_of_this_disk = 0;
    uint16_t disk_with_cd = 0;
    uint16_t total_entries_in_cd_on_disk;
    uint16_t total_entries_in_cd;
    int32_t size_of_cd = -1;
    int32_t offset_of_cd = -1;
    uint16_t comment_len = 0;
} __attribute__((packed));


/** As we parse these fields we populate the oracle */
struct CDFileHeader {
    uint32_t magic = 0x2014b50;
    uint16_t version_made_by = 0x317;
    uint16_t version_needed = 0x14;
    uint16_t flags = 0x8;
    uint16_t compression_method;
    uint16_t dostime;
    uint16_t dosdate;
    uint32_t crc32_cs;
    int32_t compress_size = -1;
    int32_t file_size = -1;
    uint16_t file_name_length;
    uint16_t extra_field_len = 32;
    uint16_t file_comment_length = 0;
    uint16_t disk_number_start = 0;
    uint16_t internal_file_attr = 0;
    uint32_t external_file_attr = 0644 << 16L;
    uint32_t relative_offset_local_header = -1;
} __attribute__((packed));


struct ZipFileHeader {
    uint32_t magic = 0x4034b50;
    uint16_t version = 0x14;
    uint16_t flags = 0x8;
    uint16_t compression_method;
    uint16_t lastmodtime;
    uint16_t lastmoddate;
    uint32_t crc32_cs;
    uint32_t compress_size;
    uint32_t file_size;
    uint16_t file_name_length;
    uint16_t extra_field_len;
} __attribute__((packed));


struct Zip64FileHeaderExtensibleField {
    uint16_t header_id = 1;
    uint16_t data_size = 28;
    uint64_t file_size;
    uint64_t compress_size;
    int64_t relative_offset_local_header;
    uint32_t disk_number_start = 0;
} __attribute__((packed));

struct ZipExtraFieldHeader {
	uint16_t header_id;
	uint16_t data_size;
} __attribute__((packed));

struct Zip64EndCD {
    uint32_t magic = 0x06064b50;
    uint64_t size_of_header = 0;
    uint16_t version_made_by = 0x2d;
    uint16_t version_needed = 0x2d;
    uint32_t number_of_disk = 0;
    uint32_t number_of_disk_with_cd = 0;
    uint64_t number_of_entries_in_volume;
    uint64_t number_of_entries_in_total;
    uint64_t size_of_cd;
    uint64_t offset_of_cd;
} __attribute__((packed));


struct Zip64CDLocator {
    uint32_t magic = 0x07064b50;
    uint32_t disk_with_cd = 0;
    uint64_t offset_of_end_cd;
    uint32_t number_of_disks = 1;
} __attribute__((packed));


#define ZIP_STORED 0
#define ZIP_DEFLATE 8

class ZipFile;

/**
 * The ZipFileSegment is created by the ZipFile.CreateMember() method and is
 * given to callers to write on. When this object is destroyed, the member will
 * be flushed to the zip file.
 *
 * Note that in AFF4 we typically write smallish segments, hence its ok to keep
 * this segment in memory before flushing it.
 *
 */
class ZipFileSegment: public StringIO {
    friend class ZipFile;

  protected:
    URN owner_urn;                        /**< The zip file who owns us. */

    // If this is set, we are backing a backing store for reading. Otherwise we
    // use our own StringIO buffers.
    URN _backing_store_urn;

    // The start offset for the backing store.
    aff4_off_t _backing_store_start_offset = -1;
    size_t _backing_store_length = 0;

  public:
    /**
     * A convenience function to add a new segment to an existing ZipFile volume.
     *
     * @param resolver
     * @param segment_urn
     * @param volume_urn
     *
     * @return An AFF4ScopedPtr reference to the new segment or NULL if the
     * segment could not be created.
     */
    static AFF4ScopedPtr<ZipFileSegment> NewZipFileSegment(
        DataStore* resolver, const URN& segment_urn, const URN& volume_urn);

    explicit ZipFileSegment(DataStore* resolver);
    ZipFileSegment(std::string filename, ZipFile& zipfile);

    virtual AFF4Status LoadFromURN();
    virtual AFF4Status LoadFromZipFile(ZipFile& owner);

    virtual AFF4Status Flush();
    virtual AFF4Status Truncate();

    virtual std::string Read(size_t length);
    virtual int Write(const char* data, int length);

    virtual aff4_off_t Size();

    virtual AFF4Status WriteStream(
        AFF4Stream* source, ProgressContext* progress = nullptr);

    using AFF4Stream::Write;
};


/**
 * A simple struct which represents information about a member in the zip
 * file. We use this to recreate the central directory when updating the
 * ZipFile.
 *
 */
class ZipInfo {
  public:
    ZipInfo();

    int compression_method = ZIP_STORED;
    uint64_t compress_size = 0;
    uint64_t file_size = 0;
    std::string filename;
    aff4_off_t local_header_offset = 0;
    int crc32_cs = 0;
    int lastmoddate = 0;
    int lastmodtime = 0;

    // Where the zip file header is located.
    aff4_off_t file_header_offset = -1;

    AFF4Status WriteFileHeader(AFF4Stream& output);
    AFF4Status WriteCDFileHeader(AFF4Stream& output);
};

/**
 * The main AFF4 ZipFile based container.

 This container can be opened for reading or writing.

 Example usage:

~~~~~~~~~~~~~~~~~~~~~{.c}
  // First create a backing file for writing the ZipFile onto.
  unique_ptr<AFF4Stream> file = FileBackedObject::NewFileBackedObject(
      "test.zip", "w");

  // The backing file ownership is given to the zip.
  unique_ptr<AFF4Volume> zip = ZipFile::NewZipFile(std::move(file));

  // The CreateMember() method returns a writable stream.
  unique_ptr<AFF4Stream> segment = zip->CreateMember("Foobar.txt");
  segment->Write("I am another segment!");
~~~~~~~~~~~~~~~~~~~~~

The will result in a zip file:

~~~~~~~~~~~~~~~~~~~~~
Archive:  src/test.zip
aff4:/8811872a-3fab-45b0-be31-daad6ab4fa70
  Length      Date    Time    Name
---------  ---------- -----   ----
      180  2015-01-18 18:26   information.yaml
      259  2015-01-18 18:26   information.turtle
       15  2015-01-18 18:26   Foobar.txt
---------                     -------
      454                     3 files
~~~~~~~~~~~~~~~~~~~~~

Note that when the volume is destroyed, it will automatically update
the *information.turtle* file if needed.

 *
 */

class ZipFile: public AFF4Volume {
    friend class ZipFileSegment;

  private:
    AFF4Status write_zip64_CD(AFF4Stream& backing_store);

  protected:
    int directory_number_of_entries = -1;
    URN backing_store_urn;

    /// The global offset of all zip file references from the real file
    /// references. This might be non-zero if the zip file was appended to another
    /// file.
    int global_offset = 0;

    /**
     * Parse the central directory in the Zip File.
     *
     * @param backing_store
     *
     * @return
     */
    AFF4Status parse_cd();


    /**
     * Load the information.turtle file in this volume into the resolver.
     *
     *
     * @return
     */
    AFF4Status LoadTurtleMetadata();

  public:
    explicit ZipFile(DataStore* resolver);

    /**
     * Creates a new ZipFile object.
     *
     * @param backing_store_urn: An URN of an object to write the zip file
     *                onto. Note that we first read and preserve the objects in
     *                the existing volume and just append new objects to it.
     *
     * @return A new ZipFile reference.
     */
    static AFF4ScopedPtr<ZipFile> NewZipFile(
        DataStore* resolver, URN backing_store_urn);

    // Generic volume interface.
    virtual AFF4ScopedPtr<AFF4Stream> CreateMember(URN child);

    /**
     * Creates a new ZipFileSegment object. The new object is automatically added
     * to the resolver cache and therefore the caller does not own it (it is
     * always owned by the resolver cache).
     *
     * @param filename
     *
     * @return
     */
    AFF4ScopedPtr<ZipFileSegment> CreateZipSegment(std::string filename);
    AFF4ScopedPtr<ZipFileSegment> OpenZipSegment(std::string filename);

    // Supports a stream interface.
    // An efficient interface to add a new archive member.
    //
    // Args:
    //   member_urn: The new member URN to be added.
    //   stream: A file-like object (with read() method) that generates data to
    //     be written as the member.
    //   compression_method: How to compress the member.
    virtual AFF4Status StreamAddMember(URN child, AFF4Stream& stream,
                                       int compression_method,
                                       ProgressContext* progress = nullptr);

    // Load the ZipFile from its URN and the information in the oracle.
    virtual AFF4Status LoadFromURN();

    virtual AFF4Status Flush();

    // All the members of the zip file. Used to reconstruct the central
    // directory. Note these store the members as the ZipFile sees them. The
    // Segment URNs must be constructed from _urn_from_member_name(). Adding new
    // objects to this must use the member names using _member_name_for_urn(URN).
    std::unordered_map<std::string, std::unique_ptr<ZipInfo>> members;
};

#endif   // SRC_ZIP_H_
