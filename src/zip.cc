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

/*
  Notes: In the following code we have two offset systems. Offsets with the word
  "real" in their name refer to offsets relative to the underlying storage
  stream (e.g. file offsets). The zip archive itself stores offsets relative to
  a constant global offset. So for example, if the zip file is appended to the
  end of another file, global_offset will be > 0 and "real" offsets need to be
  converted from "zip offsets" but adding this global value.
 */
#include "aff4_config.h"

#include <glog/logging.h>
#include <pcre++.h>
#include <sstream>
#include <iomanip>
#include "zip.h"
#include "rdf.h"
#include "lexicon.h"

#define BUFF_SIZE 4096

ZipFile::ZipFile(DataStore *resolver): AFF4Volume(resolver) {}

// Prototypes.
static string CompressBuffer(const string &buffer);
static unsigned int DecompressBuffer(
    char *buffer, int length, const string &c_buffer);


AFF4ScopedPtr<ZipFile> ZipFile::NewZipFile(
    DataStore *resolver, URN backing_store_urn) {
  URN volume_urn;

  // First check if we already know whats stored there.
  if (resolver->Get(backing_store_urn, AFF4_CONTAINS, volume_urn) ==
      STATUS_OK) {
    AFF4ScopedPtr<ZipFile> result = resolver->AFF4FactoryOpen<ZipFile>(
        volume_urn);

    if (result.get())
      return result;
  };

  // We need to create an empty temporary object to get a new URN.
  unique_ptr<ZipFile> self(new ZipFile(resolver));

  resolver->Set(self->urn, AFF4_TYPE, new URN(AFF4_ZIP_TYPE));
  resolver->Set(self->urn, AFF4_STORED, new URN(backing_store_urn));

  AFF4ScopedPtr<ZipFile> result = resolver->AFF4FactoryOpen<ZipFile>(
      self->urn);

  return result;
};


AFF4Status ZipFile::LoadTurtleMetadata() {
  // Try to load the RDF metadata file.
  AFF4ScopedPtr<ZipFileSegment> turtle_stream = OpenZipSegment(
      "information.turtle");

  if (turtle_stream.get()) {
    AFF4Status res = resolver->LoadFromTurtle(*turtle_stream);

    // Ensure the correct backing store URN overrides the one stored in the
    // turtle file since it is more current.
    resolver->Set(urn, AFF4_STORED, new URN(backing_store_urn));
    resolver->Set(backing_store_urn, AFF4_CONTAINS, new URN(urn));

    return res;
  };

  return NOT_FOUND;
};


AFF4Status ZipFile::LoadFromURN() {
  if (resolver->Get(urn, AFF4_STORED, backing_store_urn) != STATUS_OK) {
    return NOT_FOUND;
  };

  // Parse the ZIP file.
  if (parse_cd() == STATUS_OK) {
    LoadTurtleMetadata();
  };

  return STATUS_OK;
};


// Locate the Zip file central directory.
AFF4Status ZipFile::parse_cd() {
  EndCentralDirectory *end_cd = NULL;
  AFF4ScopedPtr<AFF4Stream> backing_store = resolver->AFF4FactoryOpen
      <AFF4Stream>(backing_store_urn);

  if (!backing_store) {
    LOG(ERROR) << "Unable to open backing URN " <<
        backing_store_urn.SerializeToString().c_str();
    return IO_ERROR;
  };

  // Find the End of Central Directory Record - We read about 4k of
  // data and scan for the header from the end, just in case there is
  // an archive comment appended to the end.
  if (backing_store->Seek(-BUFF_SIZE, SEEK_END) != STATUS_OK) {
    return IO_ERROR;
  };

  aff4_off_t ecd_real_offset = backing_store->Tell();
  string buffer = backing_store->Read(BUFF_SIZE);

  // Not enough data to contain an EndCentralDirectory
  if (buffer.size() < sizeof(EndCentralDirectory)) {
    return PARSING_ERROR;
  };

  // Scan the buffer backwards for an End of Central Directory magic
  for (int i = buffer.size() - sizeof(EndCentralDirectory); i > 0; i--) {
    end_cd = reinterpret_cast<EndCentralDirectory *>(&buffer[i]);
    if (end_cd->magic == 0x6054b50) {
      ecd_real_offset += i;
      LOG(INFO) << "Found ECD at " << std::hex << ecd_real_offset;
      break;
    };
  };

  if (end_cd->magic != 0x6054b50) {
    LOG(INFO) << "Unable to find EndCentralDirectory.";
    return PARSING_ERROR;
  };

  // Fetch the volume comment.
  if (end_cd->comment_len > 0) {
    backing_store->Seek(ecd_real_offset + sizeof(EndCentralDirectory),
                        SEEK_SET);
    string urn_string = backing_store->Read(end_cd->comment_len);
    LOG(INFO) << "Loaded AFF4 volume URN " << urn_string.c_str() <<
        " from zip file.";

    // There is a catch 22 here - before we parse the ZipFile we dont know the
    // Volume's URN, but we need to know the URN so the AFF4FactoryOpen() can
    // open it. Therefore we start with a random URN and then create a new
    // ZipFile volume. After parsing the central directory we discover our URN
    // and therefore we can delete the old, randomly selected URN.
    if (urn.SerializeToString() != urn_string) {
      resolver->DeleteSubject(urn);
      urn.Set(urn_string);

      // Set these triples so we know how to open the zip file again.
      resolver->Set(urn, AFF4_TYPE, new URN(AFF4_ZIP_TYPE));
      resolver->Set(urn, AFF4_STORED, new URN(backing_store_urn));
      resolver->Set(backing_store_urn, AFF4_CONTAINS, new URN(urn));
    };
  };

  aff4_off_t directory_offset = end_cd->offset_of_cd;
  directory_number_of_entries = end_cd->total_entries_in_cd;

  // Traditional zip file - non 64 bit.
  if (directory_offset > 0) {
    // The global difference between the zip file offsets and real file
    // offsets. This is non zero when the zip file was appended to another file.
    global_offset = (
        // Real ECD offset.
        ecd_real_offset - sizeof(EndCentralDirectory) - end_cd->size_of_cd -

        // Claimed CD offset.
        directory_offset);

    LOG(INFO) << "Global offset: " << std::hex << global_offset;

  // This is a 64 bit archive, find the Zip64EndCD.
  } else {
    Zip64CDLocator locator;
    uint32_t magic = locator.magic;
    aff4_off_t locator_real_offset = ecd_real_offset - sizeof(Zip64CDLocator);
    backing_store->Seek(locator_real_offset, 0);
    backing_store->ReadIntoBuffer(&locator, sizeof(locator));

    if (locator.magic != magic ||
        locator.disk_with_cd != 0 ||
        locator.number_of_disks != 1) {
      LOG(INFO) << "Zip64CDLocator invalid or not supported.";
      return PARSING_ERROR;
    };

    // Although it may appear that we can use the Zip64CDLocator to locate the
    // Zip64EndCD record via it's offset_of_cd record this is not quite so. If
    // the zip file was appended to another file, the offset_of_cd field will
    // not be valid, as it still points to the old offset. In this case we also
    // need to know the global shift.

    Zip64EndCD end_cd;
    magic = end_cd.magic;

    backing_store->Seek(
        locator_real_offset - sizeof(Zip64EndCD), SEEK_SET);
    backing_store->ReadIntoBuffer(&end_cd, sizeof(end_cd));

    if (end_cd.magic != magic) {
      LOG(INFO) << "Zip64EndCD magic not correct @0x" << std::hex <<
          locator_real_offset - sizeof(Zip64EndCD);

      return PARSING_ERROR;
    };

    directory_offset = end_cd.offset_of_cd;
    directory_number_of_entries = end_cd.number_of_entries_in_volume;

    // The global offset is now known:
    global_offset = (
        // Real offset of the central directory.
        locator_real_offset - sizeof(Zip64EndCD) - end_cd.size_of_cd -

        // The directory offset in zip file offsets.
        directory_offset);

    LOG(INFO) << "Global offset: " << std::hex << global_offset;
  };

  // Now iterate over the directory and read all the ZipInfo structs.
  aff4_off_t entry_offset = directory_offset;
  for (int i = 0; i < directory_number_of_entries; i++) {
    CDFileHeader entry;
    uint32_t magic = entry.magic;
    backing_store->Seek(entry_offset + global_offset, SEEK_SET);
    backing_store->ReadIntoBuffer(&entry, sizeof(entry));

    if (entry.magic != magic) {
      LOG(INFO) << "CDFileHeader at offset " << std::hex << entry_offset <<
          "invalid.";

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
      aff4_off_t real_end_of_extra = (
          backing_store->Tell() + entry.extra_field_len);

      while (backing_store->Tell() < real_end_of_extra) {
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
      LOG(INFO) << "Found file " << zip_info->filename.c_str() << " @ " <<
          std::hex << zip_info->local_header_offset;

      // Store this information in the resolver. Ths allows segments to be
      // directly opened by URN.
      URN member_urn = _urn_from_member_name(zip_info->filename);

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
  // If the zip file was changed, re-write the central directory.
  if (IsDirty()) {
    // First Flush all our children, but only if they are still in the cache.
    for (auto it : children) {
      AFF4ScopedPtr<AFF4Object> obj = resolver->CacheGet<AFF4Object>(it);
      if (obj.get()) {
        obj->Flush();
      };
    };

    // Update the resolver into the zip file.
    {
      AFF4ScopedPtr <ZipFileSegment> turtle_segment = CreateZipSegment(
          "information.turtle");

      if (!turtle_segment)
        return IO_ERROR;

      turtle_segment->compression_method = ZIP_DEFLATE;

      // Overwrite the old turtle file with the newer data.
      turtle_segment->Truncate();
      resolver->DumpToTurtle(*turtle_segment, urn);
      turtle_segment->Flush();

#ifdef AFF4_HAS_LIBYAML_CPP
      AFF4ScopedPtr<ZipFileSegment> yaml_segment = CreateZipSegment(
          "information.yaml");

      if (!yaml_segment)
        return IO_ERROR;


      yaml_segment->compression_method = ZIP_DEFLATE;
      resolver->DumpToYaml(*yaml_segment);
      yaml_segment->Flush();
#endif
    };

    AFF4ScopedPtr<AFF4Stream> backing_store = resolver->AFF4FactoryOpen<
      AFF4Stream>(backing_store_urn);

    if (!backing_store)
      return IO_ERROR;

    AFF4Status res = write_zip64_CD(*backing_store);
    if (res != STATUS_OK)
      return res;
  };

  return AFF4Volume::Flush();
};

/** This writes a zip64 end of central directory and a central
    directory locator */
AFF4Status ZipFile::write_zip64_CD(AFF4Stream &backing_store) {
  // We write to a memory stream first, and then copy it into the backing_store
  // at once. This really helps when we have lots of members in the zip archive.
  StringIO cd_stream;

  struct Zip64CDLocator locator;
  struct Zip64EndCD end_cd;
  struct EndCentralDirectory end;

  // Append a new central directory to the end of the zip file.
  if (backing_store.Seek(0, SEEK_END) != STATUS_OK) {
    LOG(ERROR) << "Unable to write EOCD on non-seekable stream: " <<
        urn.SerializeToString();
    return IO_ERROR;
  };

  // The real start of the ECD.
  aff4_off_t ecd_real_offset = backing_store.Tell();

  int total_entries = members.size();

  for (auto it = members.begin(); it != members.end(); it++) {
    ZipInfo *zip_info = it->second.get();
    LOG(INFO) << "Writing CD entry for " << it->first.c_str();
    WriteCDFileHeader(*zip_info, cd_stream);
  };

  locator.offset_of_end_cd = cd_stream.Tell() + ecd_real_offset -
      global_offset;

  end_cd.size_of_header = sizeof(end_cd)-12;

  end_cd.number_of_entries_in_volume = total_entries;
  end_cd.number_of_entries_in_total = total_entries;
  end_cd.size_of_cd = cd_stream.Tell();
  end_cd.offset_of_cd = locator.offset_of_end_cd - end_cd.size_of_cd;

  end.total_entries_in_cd_on_disk = members.size();
  end.total_entries_in_cd = members.size();
  string urn_string = urn.SerializeToString();
  end.comment_len = urn_string.size();

  LOG(INFO) << "Writing Zip64EndCD at " << std::hex <<
      cd_stream.Tell() + ecd_real_offset;

  cd_stream.Write(reinterpret_cast<char *>(&end_cd), sizeof(end_cd));
  cd_stream.Write(reinterpret_cast<char *>(&locator), sizeof(locator));

  LOG(INFO) << "Writing ECD at " << std::hex <<
      cd_stream.Tell() + ecd_real_offset;

  cd_stream.Write(reinterpret_cast<char *>(&end), sizeof(end));
  cd_stream.Write(urn_string);

  // Now copy the cd_stream into the backing_store in one write operation.
  cd_stream.Seek(0, SEEK_SET);

  return cd_stream.CopyToStream(
      backing_store, cd_stream.Size(), empty_progress);
};

AFF4ScopedPtr<AFF4Stream> ZipFile::CreateMember(URN child) {
  // Create a zip member with a name relative to our volume URN. So for example,
  // say our volume URN is "aff4://e21659ea-c7d6-4f4d-8070-919178aa4c7b" and the
  // child is
  // "aff4://e21659ea-c7d6-4f4d-8070-919178aa4c7b/bin/ls/00000000/index" then
  // the zip member will be simply "/bin/ls/00000000/index".
  string member_filename = _member_name_for_urn(child);
  AFF4ScopedPtr<ZipFileSegment> result = CreateZipSegment(member_filename);

  return result.cast<AFF4Stream>();
};

AFF4ScopedPtr<ZipFileSegment> ZipFile::OpenZipSegment(string filename) {
  auto it = members.find(filename);
  if (it == members.end()) {
    return AFF4ScopedPtr<ZipFileSegment>();
  };

  // Is it already in the cache?
  URN segment_urn = _urn_from_member_name(filename);
  auto res = resolver->CacheGet<ZipFileSegment>(segment_urn);
  if (res.get()) {
    LOG(INFO) << "Openning ZipFileSegment (cached) " <<
        res->urn.SerializeToString();
    return res;
  };

  ZipFileSegment *result = new ZipFileSegment(filename, *this);

  LOG(INFO) << "Openning ZipFileSegment " << result->urn.SerializeToString();

  return resolver->CachePut<ZipFileSegment>(result);
};

AFF4ScopedPtr<ZipFileSegment> ZipFile::CreateZipSegment(string filename) {
  MarkDirty();

  URN segment_urn = urn.Append(filename);

  // Is it in the cache?
  auto res = resolver->CacheGet<ZipFileSegment>(segment_urn);
  if (res.get()) {
    LOG(INFO) << "Creating ZipFileSegment (cached) " <<
        res->urn.SerializeToString();

    return res;
  };

  resolver->Set(segment_urn, AFF4_TYPE, new URN(AFF4_ZIP_SEGMENT_TYPE));
  resolver->Set(segment_urn, AFF4_STORED, new URN(urn));

  // Keep track of all the segments we issue.
  children.insert(segment_urn.SerializeToString());

  ZipFileSegment *result = new ZipFileSegment(filename, *this);
  result->Truncate();

  LOG(INFO) << "Creating ZipFileSegment " << result->urn.SerializeToString();

  // Add the new object to the object cache.
  return resolver->CachePut<ZipFileSegment>(result);
};


ZipFileSegment::ZipFileSegment(DataStore *resolver): StringIO(resolver) {}

ZipFileSegment::ZipFileSegment(string filename, ZipFile &owner) {
  resolver = owner.resolver;
  owner_urn = owner.urn;
  urn = owner._urn_from_member_name(filename);

  LoadFromZipFile(owner);
};

AFF4ScopedPtr<ZipFileSegment> ZipFileSegment::NewZipFileSegment(
    DataStore *resolver, const URN &segment_urn, const URN &volume_urn) {
  AFF4ScopedPtr<ZipFile> volume = resolver->AFF4FactoryOpen<ZipFile>(
      volume_urn);

  if (!volume)
    return AFF4ScopedPtr<ZipFileSegment>();        /** Volume not known? */

  string member_filename = volume->_member_name_for_urn(segment_urn);

  return volume->CreateZipSegment(member_filename);
};


AFF4Status ZipFileSegment::LoadFromZipFile(ZipFile &owner) {
  string member_name = owner._member_name_for_urn(urn);

  // Parse the ZipFileHeader for this filename.
  auto it = owner.members.find(member_name);
  if (it == owner.members.end()) {
    // The owner does not have this file yet.
    return STATUS_OK;
  };

  // Just borrow the reference to the ZipInfo.
  ZipInfo *zip_info = it->second.get();
  ZipFileHeader file_header;
  uint32_t magic = file_header.magic;

  AFF4ScopedPtr<AFF4Stream> backing_store = resolver->AFF4FactoryOpen
      <AFF4Stream>(owner.backing_store_urn);

  if (!backing_store) {
    return IO_ERROR;
  };

  backing_store->Seek(
      zip_info->local_header_offset + owner.global_offset, SEEK_SET);

  backing_store->ReadIntoBuffer(&file_header, sizeof(file_header));

  if (file_header.magic != magic ||
      file_header.compression_method != zip_info->compression_method) {
    LOG(INFO) << "Local file header invalid!";
    return PARSING_ERROR;
  };

  // The filename should be null terminated so we force c_str().
  string file_header_filename = backing_store->Read(
      file_header.file_name_length).c_str();
  if (file_header_filename != zip_info->filename) {
    LOG(INFO) << "Local filename different from central directory.";
    return PARSING_ERROR;
  };

  backing_store->Seek(file_header.extra_field_len, SEEK_CUR);

  // We write the entire file in a memory buffer.
  unsigned int buffer_size = zip_info->file_size;
  unique_ptr<char[]> decomp_buffer(new char[buffer_size]);

  switch (file_header.compression_method) {
    case ZIP_DEFLATE: {
      string c_buffer = backing_store->Read(zip_info->compress_size);

      if (DecompressBuffer(decomp_buffer.get(), buffer_size, c_buffer) !=
          buffer_size) {
        LOG(INFO) << "Unable to decompress file.";
        return PARSING_ERROR;
      };
    } break;

    case ZIP_STORED: {
      backing_store->ReadIntoBuffer(decomp_buffer.get(), buffer_size);
    } break;

    default:
      LOG(INFO) << "Unsupported compression method.";
      return NOT_IMPLEMENTED;
  };

  buffer.assign(decomp_buffer.get(), buffer_size);
  return STATUS_OK;
};


AFF4Status ZipFileSegment::LoadFromURN() {
  if (resolver->Get(urn, AFF4_STORED, owner_urn) != STATUS_OK) {
    return NOT_FOUND;
  };

  AFF4ScopedPtr<ZipFile> owner = resolver->AFF4FactoryOpen<ZipFile>(
      owner_urn);

  if (!owner) {
    return IO_ERROR;
  };

  return LoadFromZipFile(*owner);
};


#define BUFF_SIZE 4096

// In AFF4 we use smallish buffers, therefore we just do everything in memory.
static string CompressBuffer(const string &buffer) {
  z_stream strm;

  memset(&strm, 0, sizeof(strm));

  strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(buffer.data()));
  strm.avail_in = buffer.size();

  if (deflateInit2(&strm, 9, Z_DEFLATED, -15,
                  9, Z_DEFAULT_STRATEGY) != Z_OK) {
    LOG(INFO) << "Unable to initialise zlib (" <<  strm.msg << ")";
    return NULL;
  };

  // Get an upper bound on the size of the compressed buffer.
  int buffer_size = deflateBound(&strm, buffer.size() + 10);
  std::unique_ptr<char[]> c_buffer(new char[buffer_size]);

  strm.next_out = reinterpret_cast<Bytef *>(c_buffer.get());
  strm.avail_out = buffer_size;

  if (deflate(&strm, Z_FINISH) != Z_STREAM_END) {
    deflateEnd(&strm);
    return NULL;
  };

  deflateEnd(&strm);

  return string(c_buffer.get(), strm.total_out);
};

static unsigned int DecompressBuffer(
    char *buffer, int length, const string &c_buffer) {
  z_stream strm;

  memset(&strm, 0, sizeof(strm));

  strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(
      c_buffer.data()));

  strm.avail_in = c_buffer.size();
  strm.next_out = reinterpret_cast<Bytef *>(buffer);
  strm.avail_out = length;

  if (inflateInit2(&strm, -15) != Z_OK) {
    LOG(ERROR) << "Unable to initialise zlib (" <<  strm.msg << ")";
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
  if (IsDirty()) {
    AFF4ScopedPtr<ZipFile> owner = resolver->AFF4FactoryOpen<ZipFile>(
        owner_urn);

    if (!owner) return GENERIC_ERROR;

    AFF4ScopedPtr<AFF4Stream> backing_store = resolver->AFF4FactoryOpen
        <AFF4Stream>(owner->backing_store_urn);

    if (!backing_store) return GENERIC_ERROR;

    LOG(INFO) << "Writing member " << urn.SerializeToString().c_str();
    unique_ptr<ZipInfo> zip_info(new ZipInfo());

    // Append member at the end of the file.
    if (backing_store->Seek(0, SEEK_END) != STATUS_OK) {
      return IO_ERROR;
    };

    // zip_info offsets are relative to the start of the zip file.
    zip_info->local_header_offset = (
        backing_store->Tell() - owner->global_offset);

    zip_info->filename = owner->_member_name_for_urn(urn);
    zip_info->file_size = buffer.size();
    zip_info->crc32 = crc32(
        0, reinterpret_cast<Bytef*>(const_cast<char *>(buffer.data())),
        buffer.size());

    if (compression_method == ZIP_DEFLATE) {
      string cdata = CompressBuffer(buffer);
      zip_info->compress_size = cdata.size();
      zip_info->compression_method = ZIP_DEFLATE;

      owner->WriteZipFileHeader(*zip_info, *backing_store);
      if (backing_store->Write(cdata) < 0)
        return IO_ERROR;

    } else {
      zip_info->compress_size = buffer.size();

      owner->WriteZipFileHeader(*zip_info, *backing_store);
      if (backing_store->Write(buffer) < 0) {
        return IO_ERROR;
      };
    };

    // Replace ourselves in the members map.
    string member_name = owner->_member_name_for_urn(urn);
    owner->members[member_name] = std::move(zip_info);

    // Mark the owner as dirty.
    LOG(INFO) << owner->urn.SerializeToString().c_str() <<
        " is dirtied by segment " << urn.SerializeToString().c_str();

    owner->MarkDirty();
  };

  return AFF4Stream::Flush();
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

string ZipFile::_member_name_for_urn(const URN member) const {
  string filename = urn.RelativePath(member);
  std::stringstream result;

  // Make sure zip members do not have leading /.
  if (filename[0] == '/') {
    filename = filename.substr(1, filename.size());
  };

  // Now escape any chars which are non printable.
  for (int i = 0; i < filename.size(); i++) {
    char j = filename[i];
    if (!std::isprint(j) || j == '!' || j == '$' ||
        j == '\\' || j == ':' || j == '*' || j == '%' ||
        j == '?' || j == '"' || j == '<' || j == '>' || j == '|') {
      result << "%" << std::hex << std::setw(2) << std::setfill('0') <<
          static_cast<int>(j);
      continue;
    };

    // Escape // sequences.
    if (filename[i] == '/' && i < filename.size()-1 &&
       filename[i+1] == '/') {
      result << "%" << std::hex << std::setw(2) << std::setfill('0') <<
          static_cast<int>(filename[i]);

      result << "%" << std::hex << std::setw(2) << std::setfill('0') <<
          static_cast<int>(filename[i+1]);
      i++;
      continue;
    };

    result << j;
  };

  return result.str();
};

URN ZipFile::_urn_from_member_name(const string member) const {
  std::stringstream result;

  // Now escape any chars which are non printable.
  for (int i = 0; i < member.size(); i++) {
    if (member[i] == '%') {
      i++;

      int number = std::stoi(member.substr(i, 2), NULL, 16);
      if (number)
        result << static_cast<char>(number);

      // We consume 2 chars.
      i++;
    } else {
      result << member[i];
    };
  };

  // If this is a fully qualified AFF4 URN we return it as is, else we return
  // the relative URN to our base.
  URN result_urn(result.str());
  string scheme = result_urn.Scheme();
  if (scheme == "aff4") {
    return result_urn;
  };

  return urn.Append(result.str());
};


AFF4Status ZipFile::WriteCDFileHeader(
    ZipInfo &zip_info, AFF4Stream &output) {
  struct CDFileHeader header;
  struct Zip64FileHeaderExtensibleField zip64header;
  string filename = zip_info.filename;

  header.compression_method = zip_info.compression_method;
  header.crc32 = zip_info.crc32;
  header.file_name_length = filename.length();
  header.dostime = zip_info.lastmodtime;
  header.dosdate = zip_info.lastmoddate;
  header.extra_field_len = sizeof(zip64header);

  output.Write(reinterpret_cast<char *>(&header), sizeof(header));
  output.Write(filename);

  zip64header.file_size = zip_info.file_size;
  zip64header.compress_size = zip_info.compress_size;
  zip64header.relative_offset_local_header = zip_info.local_header_offset;

  output.Write(reinterpret_cast<char *>(&zip64header), sizeof(zip64header));

  return STATUS_OK;
};

AFF4Status ZipFile::WriteZipFileHeader(
    ZipInfo &zip_info, AFF4Stream &output) {
  struct ZipFileHeader header;
  struct Zip64FileHeaderExtensibleField zip64header;
  string filename = zip_info.filename;

  header.crc32 = zip_info.crc32;
  header.compress_size = zip_info.compress_size;
  header.file_size = zip_info.file_size;
  header.file_name_length = filename.length();
  header.compression_method = zip_info.compression_method;
  header.lastmodtime = zip_info.lastmodtime;
  header.lastmoddate = zip_info.lastmoddate;
  header.extra_field_len = sizeof(zip64header);

  output.Write(reinterpret_cast<char *>(&header), sizeof(header));
  output.Write(filename);

  zip64header.file_size = zip_info.file_size;
  zip64header.compress_size = zip_info.compress_size;
  zip64header.relative_offset_local_header = zip_info.local_header_offset;
  output.Write(reinterpret_cast<char *>(&zip64header), sizeof(zip64header));

  return STATUS_OK;
};


// Register ZipFile as an AFF4 object.
static AFF4Registrar<ZipFile> r1(AFF4_ZIP_TYPE);
static AFF4Registrar<ZipFileSegment> r2(AFF4_ZIP_SEGMENT_TYPE);
