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
  uint16_t compression_method;  /* aff4volatile:compression */
  uint16_t dostime;             /* aff4volatile:timestamp */
  uint16_t dosdate;
  uint32_t crc32;
  int32_t compress_size = -1;       /* aff4volatile:compress_size */
  int32_t file_size = -1;           /* aff4volatile:file_size */
  uint16_t file_name_length;
  uint16_t extra_field_len = 32;
  uint16_t file_comment_length = 0;
  uint16_t disk_number_start = 0;
  uint16_t internal_file_attr = 0;
  uint32_t external_file_attr = 0644 << 16L;
  int32_t relative_offset_local_header = -1; /* aff2volatile:header_offset */
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


/** Represents a single file in the archive. */
class ZipSegment: public AFF4Stream {
  struct CDFileHeader cd;

  /* These need to be configured before calling finish() */
  URN container;
  int compression_method;
  time_t timestamp;

  z_stream strm;
  uint64_t offset_of_file_header;
  string filename;

  /* Data is compressed to this buffer and only written when the Segment is
   * closed.
   *
   * Note that in AFF4 we assume segments are not too large so we can cache them
   *  in memory.
   */
  StringIO buffer;
};


#define ZIP_STORED 0
#define ZIP_DEFLATE 8

class ZipFile;


class ZipFileSegment: public StringIO {
  friend class ZipFile;

 protected:
  string filename;
  ZipFile *owner; // The zip file who owns us. NOTE: We assume the owner remains
                  // valid for the lifetime of the segment. The owner destructor
                  // will bug check if the owner zip file is destroyed with
                  // outstanding segments.

  list<ZipFileSegment *>::iterator iter;
  int compression_method = ZIP_STORED;

 public:
  ZipFileSegment(string filename, ZipFile *owner);
  ZipFileSegment(string filename, ZipFile *owner, const string data);

  // When this object is destroyed it will be flushed to the owner zip file.
  virtual ~ZipFileSegment();

  using AFF4Stream::Write;
};

// Stores information about the zip file.
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

 public:
  ZipFile();
  virtual ~ZipFile();

  static unique_ptr<ZipFile> NewZipFile(unique_ptr<AFF4Stream> stream);

  static unique_ptr<ZipFile> OpenZipFile(URN urn);
  static unique_ptr<ZipFile> OpenZipFile(unique_ptr<AFF4Stream> stream);

  virtual unique_ptr<AFF4Stream> CreateMember(string filename);
  virtual unique_ptr<AFF4Stream> OpenMember(const char *filename);
  virtual unique_ptr<AFF4Stream> OpenMember(const string filename);

  // All the members of the zip file. Used to reconstruct the central directory.
  unordered_map<string, unique_ptr<ZipInfo>> members;

};

#endif   // AFF4_ZIP_H_
