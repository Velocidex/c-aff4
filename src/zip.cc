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

#include "zip.h"
#include "rdf.h"
#include "lexicon.h"

#define BUFF_SIZE 4096

ZipFile::ZipFile(DataStore *resolver): AFF4Volume(resolver), _dirty(false) {}

// Prototypes.
static string CompressBuffer(const string &buffer);
static unsigned int DecompressBuffer(
    char *buffer, int length, const string &c_buffer);


ZipFile* ZipFile::NewZipFile(DataStore *resolver, URN backing_store_urn) {
  // We need to create an empty temporary object to get a new URN.
  unique_ptr<ZipFile> self(new ZipFile(resolver));

  resolver->Set(self->urn, AFF4_TYPE, new URN(AFF4_ZIP_TYPE));
  resolver->Set(self->urn, AFF4_STORED, new URN(backing_store_urn));

  ZipFile *result = AFF4FactoryOpen<ZipFile>(resolver, self->urn);

  return result;
};


AFF4Status ZipFile::OpenZipFile(DataStore *resolver, AFF4Stream *backing_store,
                                URN &volume_urn) {
  unique_ptr<ZipFile> self(new ZipFile(resolver));

  if(self->parse_cd() == STATUS_OK) {
    self->LoadTurtleMetadata();
  };

  return STATUS_OK;
};


AFF4Status ZipFile::LoadTurtleMetadata() {
  // Try to load the RDF metadata file.
  unique_ptr<AFF4Stream> turtle_stream = OpenZipSegment("information.turtle");

  if (turtle_stream) {
    return resolver->LoadFromTurtle(*turtle_stream);
  };

  return NOT_FOUND;
};


AFF4Status ZipFile::LoadFromURN() {
  if (resolver->Get(urn, AFF4_STORED, backing_store_urn) != STATUS_OK) {
    return NOT_FOUND;
  };

  // Parse the ZIP file.
  if(parse_cd() == STATUS_OK) {
    LoadTurtleMetadata();
  };

  return STATUS_OK;
};


// Locate the Zip file central directory.
AFF4Status ZipFile::parse_cd() {
  EndCentralDirectory *end_cd = NULL;
  AFF4Stream *backing_store = AFF4FactoryOpen<AFF4Stream>(
      resolver, backing_store_urn);

  // Find the End of Central Directory Record - We read about 4k of
  // data and scan for the header from the end, just in case there is
  // an archive comment appended to the end.
  backing_store->Seek(-BUFF_SIZE, SEEK_END);

  size_t ecd_offset = backing_store->Tell();
  string buffer = backing_store->Read(BUFF_SIZE);

  // Not enough data to contain an EndCentralDirectory
  if (buffer.size() < sizeof(EndCentralDirectory)) {
    return PARSING_ERROR;
  };

  // Scan the buffer backwards for an End of Central Directory magic
  for(int i=buffer.size() - sizeof(EndCentralDirectory); i > 0; i--) {
    end_cd = (EndCentralDirectory *)&buffer[i];
    if(end_cd->magic == 0x6054b50) {
      ecd_offset += i;
      DEBUG_OBJECT("Found ECD at %#lx", ecd_offset);
      break;
    };
  };

  if (end_cd->magic != 0x6054b50) {
    DEBUG_OBJECT("Unable to find EndCentralDirectory.");
    return PARSING_ERROR;
  };

  if (end_cd->comment_len > 0) {
    backing_store->Seek(ecd_offset + sizeof(EndCentralDirectory), SEEK_SET);
    string urn_string = backing_store->Read(end_cd->comment_len);
    DEBUG_OBJECT("Loaded AFF4 volume URN %s from zip file.",
                 urn_string.c_str());

    // There is a catch 22 here - before we parse the ZipFile we dont know the
    // Volume's URN, but we need to know the URN so the AFF4FactoryOpen() can
    // open it. Therefore we start with a random URN and then create a new
    // ZipFile volume. After parsing the central directory we discover our URN
    // and therefore we can delete the old, randomly selected URN.
    if (urn.value != urn_string) {
      resolver->DeleteSubject(urn);
      urn.Set(urn_string);

      // Set these triples so we know how to open the zip file again.
      resolver->Set(urn, AFF4_TYPE, new URN(AFF4_ZIP_TYPE));
      resolver->Set(urn, AFF4_STORED, new URN(backing_store_urn));
    };

  };

  ssize_t directory_offset = end_cd->offset_of_cd;
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
      return PARSING_ERROR;
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
      return PARSING_ERROR;
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

      // Store this information in the resolver. This allows segments to be
      // directly opened by URN.
      URN member_urn(zip_info->filename);

      resolver->Set(member_urn, AFF4_TYPE, new URN(AFF4_ZIP_SEGMENT_TYPE));
      resolver->Set(member_urn, AFF4_STORED, new URN(urn));

      members[zip_info->filename] = std::move(zip_info);
    };

    // Go to the next entry.
    entry_offset += (sizeof(entry) +
                     entry.file_name_length +
                     entry.extra_field_len +
                     entry.file_comment_length);
  };

  return STATUS_OK;
};

AFF4Status ZipFile::Flush() {
  for (auto it: children) {
    AFF4Object *obj = AFF4FactoryOpen<AFF4Object>(resolver, URN(it));
    if(obj) {
      obj->Flush();
    };
  };

  // If the zip file was changed, re-write the central directory.
  if (_dirty) {
    AFF4Stream *backing_store = AFF4FactoryOpen<AFF4Stream>(
        resolver, backing_store_urn);

    // Update the resolver into the zip file.
    ZipFileSegment *turtle_segment = CreateZipSegment("information.turtle");
    turtle_segment->compression_method = ZIP_DEFLATE;
    resolver->DumpToTurtle(*turtle_segment);
    turtle_segment->Flush();

    ZipFileSegment *yaml_segment = CreateZipSegment("information.yaml");
    yaml_segment->compression_method = ZIP_DEFLATE;
    resolver->DumpToYaml(*yaml_segment);
    yaml_segment->Flush();

    write_zip64_CD(backing_store);

    backing_store->Flush();

    _dirty = false;
  };

  return STATUS_OK;
};

/** This writes a zip64 end of central directory and a central
    directory locator */
void ZipFile::write_zip64_CD(AFF4Stream *backing_store) {
  struct Zip64CDLocator locator;
  struct Zip64EndCD end_cd;
  struct EndCentralDirectory end;

  // Append a new central directory to the end of the zip file.
  backing_store->Seek(0, SEEK_END);

  ssize_t directory_offset = backing_store->Tell();

  int total_entries = members.size();

  for(auto it=members.begin(); it != members.end(); it++) {
    ZipInfo *zip_info = it->second.get();

    WriteCDFileHeader(*zip_info, *backing_store);
  };

  locator.offset_of_end_cd = backing_store->Tell();

  end_cd.size_of_header = sizeof(end_cd)-12;
  end_cd.number_of_entries_in_volume = total_entries;
  end_cd.number_of_entries_in_total = total_entries;
  end_cd.size_of_cd = locator.offset_of_end_cd - directory_offset;
  end_cd.offset_of_cd = directory_offset;

  end.total_entries_in_cd_on_disk = members.size();
  end.total_entries_in_cd = members.size();
  string urn_string = urn.SerializeToString();
  end.comment_len = urn_string.size();

  DEBUG_OBJECT("Writing Zip64EndCD at %#lx", backing_store->Tell());
  backing_store->Write((char *)&end_cd, sizeof(end_cd));
  backing_store->Write((char *)&locator, sizeof(locator));

  DEBUG_OBJECT("Writing ECD at %#lx", backing_store->Tell());
  backing_store->Write((char *)&end, sizeof(end));
  backing_store->Write(urn_string);
};

AFF4Stream *ZipFile::CreateMember(string filename) {
  AFF4Stream *result = CreateZipSegment(filename);

  return result;
};

unique_ptr<ZipFileSegment> ZipFile::OpenZipSegment(string filename) {
  auto it = members.find(filename);
  if (it == members.end()) {
    return NULL;
  };

  unique_ptr<ZipFileSegment> result(new ZipFileSegment(resolver));
  result->urn.Set(filename);

  result->LoadFromZipFile(this);

  return result;
};

ZipFileSegment *ZipFile::CreateZipSegment(string filename) {
  // Keep track of all the segments we issue.
  children.insert(filename);

  resolver->Set(filename, AFF4_TYPE, new URN(AFF4_ZIP_SEGMENT_TYPE));
  resolver->Set(filename, AFF4_STORED, new URN(urn));

  // Add the new object to the object cache.
  return AFF4FactoryOpen<ZipFileSegment>(resolver, filename);
};


ZipFileSegment::ZipFileSegment(DataStore *resolver): StringIO(resolver) {};

ZipFileSegment::ZipFileSegment(const string &filename, URN &owner_urn):
    owner_urn(owner_urn) {

  urn.Set(filename);
};

// Initializer with knwon data.
ZipFileSegment::ZipFileSegment(
    const string &filename, URN &owner_urn, const string &data):
    ZipFileSegment::ZipFileSegment(filename, owner_urn) {
  buffer = data;
};


AFF4Status ZipFileSegment::LoadFromZipFile(ZipFile *owner) {
  // Parse the ZipFileHeader for this filename.
  auto it = owner->members.find(urn.value);
  if (it == owner->members.end()) {
    // The owner does not have this file yet.
    return STATUS_OK;
  };

  // Just borrow the reference to the ZipInfo.
  ZipInfo *zip_info = it->second.get();
  ZipFileHeader file_header;
  uint32_t magic = file_header.magic;

  AFF4Stream *backing_store = AFF4FactoryOpen<AFF4Stream>(
      resolver, owner->backing_store_urn);

  if (!backing_store) {
    return IO_ERROR;
  };

  backing_store->Seek(zip_info->local_header_offset, SEEK_SET);
  backing_store->ReadIntoBuffer(&file_header, sizeof(file_header));

  if (file_header.magic != magic ||
      file_header.compression_method != zip_info->compression_method) {
    DEBUG_OBJECT("Local file header invalid!");
    return PARSING_ERROR;
  };

  string file_header_filename = backing_store->Read(
      file_header.file_name_length).c_str();
  if (file_header_filename != zip_info->filename) {
    DEBUG_OBJECT("Local filename different from central directory.");
    return PARSING_ERROR;
  };

  backing_store->Seek(file_header.extra_field_len, SEEK_CUR);

  // We write the entire file in a memory buffer.
  unsigned int buffer_size = zip_info->file_size;
  buffer.resize(buffer_size);

  switch (file_header.compression_method) {
    case ZIP_DEFLATE: {
      string c_buffer = backing_store->Read(zip_info->compress_size);

      if(DecompressBuffer(&buffer[0], buffer_size, c_buffer) != buffer_size) {
        DEBUG_OBJECT("Unable to decompress file.");
        return PARSING_ERROR;
      };
    } break;

    case ZIP_STORED:{
      backing_store->ReadIntoBuffer(&buffer[0], buffer_size);
    } break;

    default:
      DEBUG_OBJECT("Unsupported compression method.");
      return NOT_IMPLEMENTED;
  };

  return STATUS_OK;
};


AFF4Status ZipFileSegment::LoadFromURN() {
  if (resolver->Get(urn, AFF4_STORED, owner_urn) != STATUS_OK) {
    return NOT_FOUND;
  };

  ZipFile *owner = AFF4FactoryOpen<ZipFile>(resolver, owner_urn);
  if (!owner) {
    return IO_ERROR;
  };

  return LoadFromZipFile(owner);
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
  int buffer_size = deflateBound(&strm, buffer.size() + 10);
  char c_buffer[buffer_size];

  strm.next_out = (Bytef *)c_buffer;
  strm.avail_out = buffer_size;

  if (deflate(&strm, Z_FINISH) != Z_STREAM_END) {
    deflateEnd(&strm);
    return NULL;
  };

  deflateEnd(&strm);

  return string(c_buffer, strm.total_out);
};

static unsigned int DecompressBuffer(
    char *buffer, int length, const string &c_buffer) {
  z_stream strm;

  memset(&strm, 0, sizeof(strm));

  strm.next_in = (Bytef *)c_buffer.data();
  strm.avail_in = c_buffer.size();
  strm.next_out = (Bytef *)buffer;
  strm.avail_out = length;

  if(inflateInit2(&strm, -15) != Z_OK) {
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


AFF4Status ZipFileSegment::Flush() {
  ZipFile *owner = AFF4FactoryOpen<ZipFile>(resolver, owner_urn);

  if(!owner) return GENERIC_ERROR;

  if (_dirty) {
    AFF4Stream *backing_store = AFF4FactoryOpen<AFF4Stream>(
        resolver, owner->backing_store_urn);

    if(!backing_store) return GENERIC_ERROR;

    DEBUG_OBJECT("Writing member %s", urn.value.c_str());
    unique_ptr<ZipInfo> zip_info(new ZipInfo());

    // Append member at the end of the file.
    backing_store->Seek(0, SEEK_END);

    zip_info->local_header_offset = backing_store->Tell();
    zip_info->filename = urn.value.c_str();
    zip_info->file_size = buffer.size();
    zip_info->crc32 = crc32(0, (Bytef*)buffer.data(), buffer.size());

    if (compression_method == ZIP_DEFLATE) {
      string cdata = CompressBuffer(buffer);
      zip_info->compress_size = cdata.size();
      zip_info->compression_method = ZIP_DEFLATE;

      owner->WriteZipFileHeader(*zip_info, *backing_store);
      backing_store->Write(cdata);

    } else {
      zip_info->compress_size = buffer.size();

      owner->WriteZipFileHeader(*zip_info, *backing_store);
      backing_store->Write(buffer);
    };

    // Replace ourselves in the members map.
    owner->members[urn.value] = std::move(zip_info);

    // Mark the owner as dirty.
    DEBUG_OBJECT("%s is dirtied by segment %s", owner->urn.value.c_str(),
                 urn.value.c_str());
    owner->_dirty = true;

    // We are now considered flushed.
    _dirty = false;
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


// Register ZipFile as an AFF4 object.
static AFF4Registrar<ZipFile> r1(AFF4_ZIP_TYPE);
static AFF4Registrar<ZipFileSegment> r2(AFF4_ZIP_SEGMENT_TYPE);
