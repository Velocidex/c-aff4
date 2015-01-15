#include "zip.h"
#include "rdf.h"

#define BUFF_SIZE 4096

ZipFile::ZipFile(): directory_offset(0), _dirty(false) {}

// Prototypes.
static string CompressBuffer(const string &buffer);
static unsigned int DecompressBuffer(
    char *buffer, int length, const string &c_buffer);


unique_ptr<ZipFile> ZipFile::NewZipFile(
    unique_ptr<AFF4Stream> backing_store) {
  unique_ptr<ZipFile> self(new ZipFile());

  self->backing_store.swap(backing_store);

  // Try to parse the central directory from the backing store.
  if(self->parse_cd() < 0) {
    // This file does not have a CD. Position ourselves at the end of the file.
    self->backing_store->Seek(0, SEEK_END);
    self->directory_offset = self->backing_store->Tell();
  };


  return self;
};

unique_ptr<ZipFile> ZipFile::OpenZipFile(
    unique_ptr<AFF4Stream> backing_store) {
  unique_ptr<ZipFile> self(new ZipFile());

  self->backing_store.swap(backing_store);

  if(self->parse_cd() < 0) {
    return NULL;
  };

  return self;
};

// Locate the Zip file central directory.
int ZipFile::parse_cd() {
  EndCentralDirectory *end_cd = NULL;

  // Find the End of Central Directory Record - We read about 4k of
  // data and scan for the header from the end, just in case there is
  // an archive comment appended to the end.
  backing_store->Seek(-BUFF_SIZE, SEEK_END);

  size_t ecd_offset = backing_store->Tell();
  string buffer = backing_store->Read(BUFF_SIZE);

  // Not enough data to contain an EndCentralDirectory
  if (buffer.size() < sizeof(EndCentralDirectory)) {
    return -1;
  };

  // Scan the buffer backwards for an End of Central Directory magic
  for(int i=buffer.size() - 4; i > 0; i--) {
    end_cd = (EndCentralDirectory *)&buffer[i];
    if(end_cd->magic == 0x6054b50) {
      ecd_offset += i;
      DEBUG_OBJECT("Found ECD at %#lx", ecd_offset);
      break;
    };
  };

  if (!end_cd) {
    DEBUG_OBJECT("Unable to find EndCentralDirectory.");
    return -1;
  };

  directory_offset = end_cd->offset_of_cd;
  directory_number_of_entries = end_cd->total_entries_in_cd;

  // This is a 64 bit archive, find the Zip64EndCD.
  if (directory_offset < 0) {
    Zip64CDLocator locator;
    uint32_t magic = locator.magic;
    int locator_offset = ecd_offset - sizeof(Zip64CDLocator);
    backing_store->Seek(locator_offset, 0);
    backing_store->ReadIntoBuffer(&locator, sizeof(locator));

    if (locator.magic != magic ||
        locator.disk_with_cd != 0 ||
        locator.number_of_disks != 1) {
      DEBUG_OBJECT("Zip64CDLocator invalid or not supported.");
      return -1;
    };

    Zip64EndCD end_cd;
    magic = end_cd.magic;

    backing_store->Seek(locator.offset_of_end_cd, 0);
    backing_store->ReadIntoBuffer(&end_cd, sizeof(end_cd));

    directory_offset = end_cd.offset_of_cd;
    directory_number_of_entries = end_cd.number_of_entries_in_volume;
  };

  // Now iterate over the directory and read all the ZipInfo structs.
  ssize_t entry_offset = directory_offset;
  for(int i=0; i<directory_number_of_entries; i++) {
    CDFileHeader entry;
    uint32_t magic = entry.magic;
    backing_store->Seek(entry_offset, 0);
    backing_store->ReadIntoBuffer(&entry, sizeof(entry));

    if (entry.magic != magic) {
      DEBUG_OBJECT("CDFileHeader at offset %#lx invalid.",
                   entry_offset);
      return -1;
    };

    unique_ptr<ZipInfo> zip_info(new ZipInfo());

    zip_info->filename = backing_store->Read(entry.file_name_length).c_str();
    zip_info->local_header_offset = entry.relative_offset_local_header;
    zip_info->compression_method = entry.compression_method;
    zip_info->compress_size = entry.compress_size;
    zip_info->file_size = entry.file_size;
    zip_info->crc32 = entry.crc32;
    zip_info->lastmoddate = entry.dosdate;
    zip_info->lastmodtime = entry.dostime;

    // Zip64 local header - parse the extra field.
    if (zip_info->local_header_offset < 0) {
      // Parse all the extra field records.
      Zip64FileHeaderExtensibleField extra;
      size_t end_of_extra = backing_store->Tell() + entry.extra_field_len;

      while(backing_store->Tell() < end_of_extra) {
        backing_store->ReadIntoBuffer(&extra, entry.extra_field_len);

        if (extra.header_id == 1) {
          zip_info->local_header_offset = extra.relative_offset_local_header;
          zip_info->file_size = extra.file_size;
          zip_info->compress_size = extra.compress_size;
          break;
        };
      };
    };

    if (zip_info->local_header_offset >= 0) {
      DEBUG_OBJECT("Found file %s @ %#lx", zip_info->filename.c_str(),
                   zip_info->local_header_offset);

      members[zip_info->filename] = std::move(zip_info);
    };

    // Go to the next entry.
    entry_offset += (sizeof(entry) +
                     entry.file_name_length +
                     entry.extra_field_len +
                     entry.file_comment_length);
  };

  return members.size();
};



ZipFile::~ZipFile() {
  CHECK(outstanding_members.size() > 0,
        "ZipFile destroyed with %ld outstanding segments.",
        outstanding_members.size());

  // If the zip file was changed, re-write the central directory.
  if (_dirty) {
    write_zip64_CD();
  };
};

/** This writes a zip64 end of central directory and a central
    directory locator */
void ZipFile::write_zip64_CD() {
  struct Zip64CDLocator locator;
  struct Zip64EndCD end_cd;
  struct EndCentralDirectory end;

  int total_entries = members.size();
  size_t directory_offset = backing_store->Tell();

  for(auto it=members.begin(); it != members.end(); it++) {
    ZipInfo *zip_info = it->second.get();

    WriteCDFileHeader(*zip_info, *backing_store.get());
  };

  locator.offset_of_end_cd = backing_store->Tell();

  end_cd.size_of_header = sizeof(end_cd)-12;
  end_cd.number_of_entries_in_volume = total_entries;
  end_cd.number_of_entries_in_total = total_entries;
  end_cd.size_of_cd = locator.offset_of_end_cd - directory_offset;
  end_cd.offset_of_cd = directory_offset;

  end.total_entries_in_cd_on_disk = members.size();
  end.total_entries_in_cd = members.size();

  DEBUG_OBJECT("Writing Zip64EndCD at %#lx", backing_store->Tell());
  backing_store->Write((char *)&end_cd, sizeof(end_cd));
  backing_store->Write((char *)&locator, sizeof(locator));

  DEBUG_OBJECT("Writing ECD at %#lx", backing_store->Tell());
  backing_store->Write((char *)&end, sizeof(end));
};

unique_ptr<AFF4Stream> ZipFile::CreateMember(string filename) {
  unique_ptr<AFF4Stream>result(new ZipFileSegment(filename, this));

  return result;
};

unique_ptr<AFF4Stream> ZipFile::OpenMember(const string filename) {
  // Parse the ZipFileHeader for this filename.
  auto it = members.find(filename);
  if (it == members.end()) {
    DEBUG_OBJECT("File %s not found.", filename.c_str());
    return NULL;
  };

  // Just borrow the reference to the ZipInfo.
  ZipInfo *zip_info = it->second.get();
  ZipFileHeader file_header;
  uint32_t magic = file_header.magic;

  backing_store->Seek(zip_info->local_header_offset, SEEK_SET);
  backing_store->ReadIntoBuffer(&file_header, sizeof(file_header));

  if (file_header.magic != magic ||
      file_header.compression_method != zip_info->compression_method) {
    DEBUG_OBJECT("Local file header invalid!");
    return NULL;
  };

  string file_header_filename = backing_store->Read(
      file_header.file_name_length).c_str();
  if (file_header_filename != zip_info->filename) {
    DEBUG_OBJECT("Local filename different from central directory.");
    return NULL;
  };

  backing_store->Seek(file_header.extra_field_len, SEEK_CUR);

  // We write the entire file in a memory buffer.
  int buffer_size = zip_info->file_size;
  char buffer[buffer_size];

  switch (file_header.compression_method) {
    case ZIP_DEFLATE: {
      string c_buffer = backing_store->Read(zip_info->compress_size);
      if(DecompressBuffer(
             buffer, buffer_size, c_buffer) != zip_info->file_size) {
        DEBUG_OBJECT("Unable to decompress file.");
        return NULL;
      };
    } break;

    case ZIP_STORED:{
      backing_store->ReadIntoBuffer(buffer, buffer_size);
    } break;

    default:
      DEBUG_OBJECT("Unsupported compression method.");
      return NULL;
  };

  unique_ptr<AFF4Stream>result(
      new ZipFileSegment(filename, this, buffer));

  return result;
};

unique_ptr<AFF4Stream> ZipFile::OpenMember(const char *filename) {
  return OpenMember(string(filename));
};


ZipFileSegment::ZipFileSegment(string filename, ZipFile *owner):
    filename(filename), owner(owner) {

  // Keep track of all the segments we issue. Note that we do not actually take
  // ownership here, rather, we rely on the fact that ZipFile out lives
  // ZipFileSegments. If the ZipFileSegment is destroyed, it will be removed
  // from this list automatically.
  owner->outstanding_members.push_front(this);
  iter = owner->outstanding_members.begin();
};

// Initializer with knwon data.
ZipFileSegment::ZipFileSegment(
    string filename, ZipFile *owner, const string data):
    ZipFileSegment::ZipFileSegment(filename, owner) {
  buffer = data;
};


#define BUFF_SIZE 4096

// In AFF4 we use smallish buffers, therefore we just do everything in memory.
static string CompressBuffer(const string &buffer) {
  z_stream strm;

  memset(&strm, 0, sizeof(strm));

  strm.next_in = (Bytef*)buffer.data();
  strm.avail_in = buffer.size();

  if(deflateInit2(&strm, 9, Z_DEFLATED, -15,
                  9, Z_DEFAULT_STRATEGY) != Z_OK) {
    DEBUG_OBJECT("Unable to initialise zlib (%s)", strm.msg);
    return NULL;
  };

  // Get an upper bound on the size of the compressed buffer.
  int buffer_size = deflateBound(&strm, buffer.size());
  char c_buffer[buffer_size];

  strm.next_out = (Bytef *)c_buffer;
  strm.avail_out = buffer_size;

  if (deflate(&strm, Z_FINISH) != Z_STREAM_END) {
    deflateEnd(&strm);
    return NULL;
  };

  deflateEnd(&strm);

  return string(c_buffer, buffer_size);
};

static unsigned int DecompressBuffer(
    char *buffer, int length, const string &c_buffer) {
  z_stream strm;

  memset(&strm, 0, sizeof(strm));

  strm.next_in = (Bytef *)c_buffer.data();
  strm.avail_in = c_buffer.size();
  strm.next_out = (Bytef *)buffer;
  strm.avail_out = length;

  if(inflateInit(&strm) != Z_OK) {
    DEBUG_OBJECT("Unable to initialise zlib (%s)", strm.msg);
    return 0;
  };

  if (inflate(&strm, Z_FINISH) != Z_STREAM_END) {
    inflateEnd(&strm);
    return 0;
  };

  inflateEnd(&strm);

  return length - strm.avail_out;
};


ZipFileSegment::~ZipFileSegment() {
  if (_dirty) {
    DEBUG_OBJECT("Writing member %s", filename.c_str());
    unique_ptr<ZipInfo> zip_info(new ZipInfo());

    // TODO: Lock owner.
    zip_info->local_header_offset = owner->backing_store->Tell();
    zip_info->filename = filename;
    zip_info->file_size = buffer.size();
    zip_info->crc32 = crc32(0, (Bytef*)buffer.data(), buffer.size());

    if (compression_method == ZIP_DEFLATE) {
      string cdata = CompressBuffer(buffer);
      zip_info->compress_size = cdata.size();
      zip_info->compression_method = ZIP_DEFLATE;

      owner->WriteZipFileHeader(*zip_info, *owner->backing_store);
      owner->backing_store->Write(cdata);

    } else {
      zip_info->compress_size = buffer.size();

      owner->WriteZipFileHeader(*zip_info, *owner->backing_store.get());
      owner->backing_store->Write(buffer);
    };

    // Replace ourselves in the members map.
    owner->members[filename] = std::move(zip_info);

    owner->_dirty = true;
  };

  // Remove ourselves from the outstanding_members list since we are no longer
  // outstanding.
  owner->outstanding_members.erase(iter);
};


ZipInfo::ZipInfo() {
  struct tm now;
  time_t epoch_time;

  epoch_time = time(NULL);
  localtime_r(&epoch_time, &now);

  lastmoddate = (now.tm_year + 1900 - 1980) << 9 |
      (now.tm_mon + 1) << 5 | now.tm_mday;
  lastmodtime = now.tm_hour << 11 | now.tm_min << 5 |
      now.tm_sec / 2;
}

AFF4Status ZipFile::WriteCDFileHeader(
    ZipInfo &zip_info, AFF4Stream &output) {
  struct CDFileHeader header;
  struct Zip64FileHeaderExtensibleField zip64header;

  header.compression_method = zip_info.compression_method;
  header.crc32 = zip_info.crc32;
  header.file_name_length = zip_info.filename.length();
  header.dostime = zip_info.lastmodtime;
  header.dosdate = zip_info.lastmoddate;
  header.extra_field_len = sizeof(zip64header);

  output.Write((char *)&header, sizeof(header));
  output.Write(zip_info.filename);

  zip64header.file_size = zip_info.file_size;
  zip64header.compress_size = zip_info.compress_size;
  zip64header.relative_offset_local_header = zip_info.local_header_offset;

  output.Write((char *)&zip64header, sizeof(zip64header));

  return STATUS_OK;
};

AFF4Status ZipFile::WriteZipFileHeader(
    ZipInfo &zip_info, AFF4Stream &output) {
  struct ZipFileHeader header;
  struct Zip64FileHeaderExtensibleField zip64header;

  header.crc32 = zip_info.crc32;
  header.compress_size = zip_info.compress_size;
  header.file_size = zip_info.file_size;
  header.file_name_length = zip_info.filename.length();
  header.compression_method = zip_info.compression_method;
  header.lastmodtime = zip_info.lastmodtime;
  header.lastmoddate = zip_info.lastmoddate;
  header.extra_field_len = sizeof(zip64header);

  output.Write((char *)&header, sizeof(header));
  output.Write(zip_info.filename);

  zip64header.file_size = zip_info.file_size;
  zip64header.compress_size = zip_info.compress_size;
  zip64header.relative_offset_local_header = zip_info.local_header_offset;
  output.Write((char *)&zip64header, sizeof(zip64header));

  return STATUS_OK;
};
