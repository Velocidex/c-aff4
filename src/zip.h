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

#ifndef     AFF4_ZIP_H_
#define     AFF4_ZIP_H_

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
}__attribute__((packed));


/** As we parse these fields we populate the oracle */
struct CDFileHeader {
  uint32_t magic = 0x2014b50;
  uint16_t version_made_by = 0x317;
  uint16_t version_needed = 0x14;
  uint16_t flags = 0x8;
  uint16_t compression_method;
  uint16_t dostime;
  uint16_t dosdate;
  uint32_t crc32;
  int32_t compress_size = -1;
  int32_t file_size = -1;
  uint16_t file_name_length;
  uint16_t extra_field_len = 32;
  uint16_t file_comment_length = 0;
  uint16_t disk_number_start = 0;
  uint16_t internal_file_attr = 0;
  uint32_t external_file_attr = 0644 << 16L;
  int32_t relative_offset_local_header = -1;
}__attribute__((packed));


struct ZipFileHeader {
  uint32_t magic = 0x4034b50;
  uint16_t version = 0x14;
  uint16_t flags = 0x8;
  uint16_t compression_method;
  uint16_t lastmodtime;
  uint16_t lastmoddate;
  uint32_t crc32;
  uint32_t compress_size;
  uint32_t file_size;
  uint16_t file_name_length;
  uint16_t extra_field_len;
}__attribute__((packed));


struct Zip64FileHeaderExtensibleField {
  uint16_t header_id = 1;
  uint16_t data_size = 28;
  uint64_t file_size;
  uint64_t compress_size;
  uint64_t relative_offset_local_header;
  uint32_t disk_number_start = 0;
}__attribute__((packed));

struct Zip64EndCD {
  uint32_t magic = 0x06064b50;
  uint64_t size_of_header;
  uint16_t version_made_by = 0x2d;
  uint16_t version_needed = 0x2d;
  uint32_t number_of_disk = 0;
  uint32_t number_of_disk_with_cd = 0;
  uint64_t number_of_entries_in_volume;
  uint64_t number_of_entries_in_total;
  uint64_t size_of_cd;
  uint64_t offset_of_cd;
}__attribute__((packed));


struct Zip64CDLocator {
  uint32_t magic = 0x07064b50;
  uint32_t disk_with_cd = 0;
  uint64_t offset_of_end_cd;
  uint32_t number_of_disks = 1;
}__attribute__((packed));


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
  string filename;
  ZipFile *owner; // The zip file who owns us. NOTE: We assume the owner remains
                  // valid for the lifetime of the segment. The owner destructor
                  // will bug check if the owner zip file is destroyed with
                  // outstanding segments.

  list<ZipFileSegment *>::iterator iter; /**< An iterator to the outstanding
                                          * list in the owner. We remove
                                          * ourselves from the owner when
                                          * destroyed. */
 public:

  int compression_method = ZIP_STORED;  /**< Compression method. */

  ZipFileSegment(const string &filename, ZipFile *owner);
  ZipFileSegment(const string &filename, ZipFile *owner, const string &data);

  // When this object is destroyed it will be flushed to the owner zip file.
  virtual ~ZipFileSegment();

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
  string filename;
  ssize_t local_header_offset = 0;
  int crc32;
  int lastmoddate;
  int lastmodtime;
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
  void write_zip64_CD();
  AFF4Status WriteCDFileHeader(ZipInfo &zip_info, AFF4Stream &output);
  AFF4Status WriteZipFileHeader(ZipInfo &zip_info, AFF4Stream &output);

 protected:
  ssize_t directory_offset = -1;
  int directory_number_of_entries = -1;
  unique_ptr<AFF4Stream> backing_store;
  bool _dirty;

  // This is a list of outstanding segments.
  std::list<ZipFileSegment *> outstanding_members;

  // Returns the total number of entries found or -1 on error.
  int parse_cd();

  string rdf_type = "aff4:zip_volume";

 public:
  ZipFile();
  virtual ~ZipFile();

  /**
   * Creates a new ZipFile object.
   *
   * @param stream: An AFF4Stream to write the zip file onto. Note that we first
   *                read and preserve the objects in the existing volume and
   *                just append new objects to it.
   *
   * @return A new ZipFile reference.
   */
  static unique_ptr<ZipFile> NewZipFile(unique_ptr<AFF4Stream> stream);

  static unique_ptr<ZipFile> OpenZipFile(URN urn);

  /**
   * Open a new ZipFile from an existing stream.
   *
   * @param stream: Stream to read.
   *
   * @return A new ZipFile reference.
   */
  static unique_ptr<ZipFile> OpenZipFile(unique_ptr<AFF4Stream> stream);

  // Generic volume interface.
  virtual unique_ptr<AFF4Stream> CreateMember(string filename);
  virtual unique_ptr<AFF4Stream> OpenMember(const char *filename);
  virtual unique_ptr<AFF4Stream> OpenMember(const string filename);

  // Specific ZipFile interface. Can be used to set compression type.
  unique_ptr<ZipFileSegment> CreateZipSegment(string filename);


  // Load the ZipFile from its URN and the information in the oracle.
  virtual AFF4Status LoadFromURN(const string &mode);

  // All the members of the zip file. Used to reconstruct the central directory.
  unordered_map<string, unique_ptr<ZipInfo>> members;
};

#endif   // AFF4_ZIP_H_
