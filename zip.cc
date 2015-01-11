#include "zip.h"

ZipFile::ZipFile(): directory_offset(0), _dirty(false) {}

unique_ptr<ZipFile> ZipFile::NewZipFile(
    unique_ptr<AFF4Stream> backing_store) {

  ZipFile *self = new ZipFile();
  unique_ptr<ZipFile> result(self);

  self->backing_store.swap(backing_store);

  return result;
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

    zip_info->WriteCDFileHeader(*backing_store.get());
  };

  locator.offset_of_end_cd = backing_store->Tell();

  end_cd.size_of_header = sizeof(end_cd)-12;
  end_cd.number_of_entries_in_volume = total_entries;
  end_cd.number_of_entries_in_total = total_entries;
  end_cd.size_of_cd = locator.offset_of_end_cd - directory_offset;
  end_cd.offset_of_cd = directory_offset;

  end.total_entries_in_cd_on_disk = members.size();
  end.total_entries_in_cd = members.size();

  DEBUG_OBJECT("writing ECD at %#lx\n", backing_store->Tell());
  backing_store->Write((char *)&end_cd, sizeof(end_cd));
  backing_store->Write((char *)&locator, sizeof(locator));
  backing_store->Write((char *)&end, sizeof(end));

};

unique_ptr<AFF4Stream> ZipFile::CreateMember(string filename) {
  unique_ptr<AFF4Stream>result(new ZipFileSegment(filename, this));

  return result;
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

#define BUFF_SIZE 4096

// In AFF4 we use smallish buffers, therefore we just do everything in memory.
unique_ptr<string> CompressBuffer(const string &buffer) {
  z_stream strm;

  memset(&strm, 0, sizeof(strm));

  strm.next_in = (Bytef*)buffer.c_str();
  strm.avail_in = buffer.size();

  if(deflateInit2(&strm, 9, Z_DEFLATED, -15,
                  9, Z_DEFAULT_STRATEGY) != Z_OK) {
    DEBUG_OBJECT("Unable to initialise zlib (%s)", strm.msg);
    return NULL;
  };

  int buffer_size = deflateBound(&strm, buffer.length());
  Bytef c_buffer[buffer_size];

  strm.next_out = c_buffer;
  strm.avail_out = sizeof(c_buffer);

  if (deflate(&strm, Z_FINISH) != Z_STREAM_END) {
    deflateEnd(&strm);
    return NULL;
  };

  deflateEnd(&strm);

  return unique_ptr<string>(
      new string((char *)c_buffer, sizeof(c_buffer) - strm.avail_out));
};


ZipFileSegment::~ZipFileSegment() {
  if (_dirty) {
    unique_ptr<string> cdata = CompressBuffer(buffer);
    unique_ptr<ZipInfo> zip_info(new ZipInfo());

    // TODO: Lock owner.
    zip_info->compression_method = ZIP_DEFLATE;
    zip_info->relative_offset_local_header = owner->backing_store->Tell();
    zip_info->filename = filename;
    zip_info->file_size = buffer.length();
    zip_info->compress_size = cdata->length();
    zip_info->crc32 = crc32(0, (Bytef*)buffer.c_str(), buffer.length());

    zip_info->WriteZipFileHeader(*owner->backing_store.get());

    owner->backing_store->Write(cdata);

    // Remove ourselves from the members list.
    owner->outstanding_members.erase(iter);

    // Replace ourselves in the members map.
    owner->members[filename] = std::move(zip_info);

    owner->_dirty = true;
  };
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

int ZipInfo::WriteCDFileHeader(AFF4Stream &output) {
  struct CDFileHeader header;
  struct Zip64FileHeaderExtensibleField zip64header;

  header.compression_method = compression_method;
  header.crc32 = crc32;
  header.file_name_length = filename.length();
  header.dostime = lastmodtime;
  header.dosdate = lastmoddate;
  header.extra_field_len = sizeof(zip64header);

  output.Write((char *)&header, sizeof(header));
  output.Write(filename);

  zip64header.file_size = file_size;
  zip64header.compress_size = compress_size;
  zip64header.relative_offset_local_header = relative_offset_local_header;

  output.Write((char *)&zip64header, sizeof(zip64header));

  return 1;
};

int ZipInfo::WriteZipFileHeader(AFF4Stream &output) {
  struct ZipFileHeader header;
  struct Zip64FileHeaderExtensibleField zip64header;

  header.crc32 = crc32;
  header.compress_size = compress_size;
  header.file_size = file_size;
  header.file_name_length = filename.length();
  header.compression_method = compression_method;
  header.lastmodtime = lastmodtime;
  header.lastmoddate = lastmoddate;
  header.extra_field_len = sizeof(zip64header);

  output.Write((char *)&header, sizeof(header));
  output.Write(filename);

  zip64header.file_size = file_size;
  zip64header.compress_size = compress_size;
  zip64header.relative_offset_local_header = relative_offset_local_header;
  output.Write((char *)&zip64header, sizeof(zip64header));

  return 1;
};
