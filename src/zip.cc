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
#include "config.h"

#include <glog/logging.h>
#include <pcre++.h>
#include <sstream>
#include <iomanip>
#include "zip.h"
#include "rdf.h"
#include "lexicon.h"
#include "libaff4.h"

ZipFile::ZipFile(DataStore* resolver) :
    AFF4Volume(resolver) {

}

// Prototypes.
static std::string CompressBuffer(const std::string& buffer);
static unsigned int DecompressBuffer(char* buffer, int length, const std::string& c_buffer);

AFF4ScopedPtr<ZipFile> ZipFile::NewZipFile(DataStore* resolver, URN backing_store_urn) {
    URN volume_urn;

    // First check if we already know whats stored there.
    if (resolver->Get(backing_store_urn, AFF4_CONTAINS, volume_urn) == STATUS_OK) {
        AFF4ScopedPtr<ZipFile> result = resolver->AFF4FactoryOpen<ZipFile>(volume_urn);

        if (result.get()) {
            return result;
        }
    }

    // We need to create an empty temporary object to get a new URN.
    std::unique_ptr<ZipFile> self(new ZipFile(resolver));

    resolver->Set(self->urn, AFF4_TYPE, new URN(AFF4_ZIP_TYPE));
    resolver->Set(self->urn, AFF4_STORED, new URN(backing_store_urn));

    AFF4ScopedPtr<ZipFile> result = resolver->AFF4FactoryOpen<ZipFile>(self->urn);

    if(result.get() != nullptr && result->members.size() == 0) {

        // Mark the container with its URN
        AFF4ScopedPtr<AFF4Stream> desc = self->CreateMember(self->urn.Append(AFF4_CONTAINER_DESCRIPTION));

        if (!desc) {
            return result;
        } else {
            desc->Write(self->urn.SerializeToString());
            desc->Flush();
        }

        // Create a version.txt file.
        AFF4ScopedPtr<AFF4Stream> ver = self->CreateMember(self->urn.Append(AFF4_CONTAINER_VERSION_TXT));
        if (!ver) {
            return result;
        } else {
            ver->Write("major=" AFF4_VERSION_MAJOR "\n");
            ver->Write("minor=" AFF4_VERSION_MINOR "\n");
            ver->Write("tool=" AFF4_TOOL " " PACKAGE_VERSION "\n");
            ver->Flush();
        }
    }
    return result;
}

AFF4Status ZipFile::LoadTurtleMetadata() {
    // Try to load the RDF metadata file.
    AFF4ScopedPtr<ZipFileSegment> turtle_stream = OpenZipSegment(
                AFF4_CONTAINER_INFO_TURTLE);

    if (turtle_stream.get()) {
        AFF4Status res = resolver->LoadFromTurtle(*turtle_stream);

        // Ensure the correct backing store URN overrides the one stored in the
        // turtle file since it is more current.
        resolver->Set(urn, AFF4_STORED, new URN(backing_store_urn));
        resolver->Set(backing_store_urn, AFF4_CONTAINS, new URN(urn));

        return res;
    }

    return NOT_FOUND;
}

AFF4Status ZipFile::LoadFromURN() {
    if (resolver->Get(urn, AFF4_STORED, backing_store_urn) != STATUS_OK) {
        return NOT_FOUND;
    }

    // Parse the ZIP file.
    if (parse_cd() == STATUS_OK) {
        LoadTurtleMetadata();
    }

    return STATUS_OK;
}

// Locate the Zip file central directory.
AFF4Status ZipFile::parse_cd() {
    EndCentralDirectory* end_cd = nullptr;
    AFF4ScopedPtr<AFF4Stream> backing_store = resolver->AFF4FactoryOpen<AFF4Stream>(backing_store_urn);

    if (!backing_store) {
        LOG(ERROR)<< "Unable to open backing URN " <<
                  backing_store_urn.SerializeToString().c_str();
        return IO_ERROR;
    }

    // Find the End of Central Directory Record - We read about 4k of
    // data and scan for the header from the end, just in case there is
    // an archive comment appended to the end.
    if (backing_store->Seek(-AFF4_BUFF_SIZE, SEEK_END) != STATUS_OK) {
        return IO_ERROR;
    }

    aff4_off_t ecd_real_offset = backing_store->Tell();
    std::string buffer = backing_store->Read(AFF4_BUFF_SIZE);

    // Not enough data to contain an EndCentralDirectory
    if (buffer.size() < sizeof(EndCentralDirectory)) {
        return PARSING_ERROR;
    }

    // Scan the buffer backwards for an End of Central Directory magic
    for (int i = buffer.size() - sizeof(EndCentralDirectory); i > 0; i--) {
        end_cd = reinterpret_cast<EndCentralDirectory*>(&buffer[i]);
        if (end_cd->magic == 0x6054b50) {
            ecd_real_offset += i;
            LOG(INFO)<< "Found ECD at " << std::hex << ecd_real_offset;
            break;
        }
    }

    if (end_cd->magic != 0x6054b50) {
        LOG(INFO)<< "Unable to find EndCentralDirectory.";
        return PARSING_ERROR;
    }

    // Fetch the volume comment.
    if (end_cd->comment_len > 0) {
        backing_store->Seek(ecd_real_offset + sizeof(EndCentralDirectory),
                            SEEK_SET);
        std::string urn_string = backing_store->Read(end_cd->comment_len);
        LOG(INFO)<< "Loaded AFF4 volume URN " << urn_string.c_str() <<
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
        }
    }

    aff4_off_t directory_offset = end_cd->offset_of_cd;
    directory_number_of_entries = end_cd->total_entries_in_cd;

    // Traditional zip file - non 64 bit.
    if (directory_offset > 0) {
        // The global difference between the zip file offsets and real file
        // offsets. This is non zero when the zip file was appended to another file.
        global_offset = (
                            // Real ECD offset.
                            ecd_real_offset - end_cd->size_of_cd -

                            // Claimed CD offset.
                            directory_offset);

        LOG(INFO)<< "Global offset: " << std::hex << global_offset;

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
        }

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
        }

        directory_offset = end_cd.offset_of_cd;
        directory_number_of_entries = end_cd.number_of_entries_in_volume;

        // The global offset is now known:
        global_offset = (
                            // Real offset of the central directory.
                            locator_real_offset - sizeof(Zip64EndCD) - end_cd.size_of_cd -

                            // The directory offset in zip file offsets.
                            directory_offset);

        LOG(INFO) << "Global offset: " << std::hex << global_offset;
    }

    // Now iterate over the directory and read all the ZipInfo structs.
    aff4_off_t entry_offset = directory_offset;
    for (int i = 0; i < directory_number_of_entries; i++) {
        CDFileHeader entry;
        uint32_t magic = entry.magic;
        backing_store->Seek(entry_offset + global_offset, SEEK_SET);
        backing_store->ReadIntoBuffer(&entry, sizeof(entry));

        if (entry.magic != magic) {
            LOG(INFO)<< "CDFileHeader at offset " << std::hex << entry_offset <<
                     "invalid.";

            return PARSING_ERROR;
        }

        std::unique_ptr<ZipInfo> zip_info(new ZipInfo());

        zip_info->filename = backing_store->Read(entry.file_name_length).c_str();
        zip_info->local_header_offset = entry.relative_offset_local_header;
        zip_info->compression_method = entry.compression_method;
        zip_info->compress_size = entry.compress_size;
        zip_info->file_size = entry.file_size;
        zip_info->crc32_cs = entry.crc32_cs;
        zip_info->lastmoddate = entry.dosdate;
        zip_info->lastmodtime = entry.dostime;

        // Zip64 local header - parse the extra field.
        if (zip_info->local_header_offset == 4294967295) {
            // Parse all the extra field records.
        	ZipExtraFieldHeader extra;
            aff4_off_t real_end_of_extra = (backing_store->Tell() + entry.extra_field_len);

			while (backing_store->Tell() < real_end_of_extra) {
				backing_store->ReadIntoBuffer(&extra, 4);

				if (extra.header_id == 1) {
					uint16_t data_size = extra.data_size;
					if ((zip_info->file_size == 4294967295) && (data_size >= 8)) {
						backing_store->ReadIntoBuffer(&(zip_info->file_size), 8);
						data_size -= 8;
					}
					if ((zip_info->compress_size == 4294967295) && (data_size >= 8)) {
						backing_store->ReadIntoBuffer(&(zip_info->compress_size), 8);
						data_size -= 8;
					}
					if ((zip_info->local_header_offset == 4294967295) && (data_size >= 8)) {
						backing_store->ReadIntoBuffer(&(zip_info->local_header_offset), 8);
						data_size -= 8;
					}
					if (data_size > 0) {
						backing_store->Seek(data_size, 1);
					}
				} else {
					// skip the data length going forward.
					backing_store->Seek(extra.data_size, 1);
				}
			}
        }

        if (zip_info->local_header_offset >= 0) {
            LOG(INFO)<< "Found file " << zip_info->filename.c_str() << " @ " <<
                     std::hex << zip_info->local_header_offset;

            // Store this information in the resolver. Ths allows segments to be
            // directly opened by URN.
            URN member_urn = urn_from_member_name(zip_info->filename, urn);

            resolver->Set(member_urn, AFF4_TYPE, new URN(AFF4_ZIP_SEGMENT_TYPE));
            resolver->Set(member_urn, AFF4_STORED, new URN(urn));

            members[zip_info->filename] = std::move(zip_info);
        }

        // Go to the next entry.
        entry_offset += (sizeof(entry) + entry.file_name_length + entry.extra_field_len + entry.file_comment_length);
    }

    return STATUS_OK;
}

AFF4Status ZipFile::Flush() {
    // If the zip file was changed, re-write the central directory.
    if (IsDirty()) {
        // First Flush all our children, but only if they are still in the cache.
        for (auto it : children) {
            AFF4ScopedPtr<AFF4Object> obj = resolver->CacheGet<AFF4Object>(it);
            if (obj.get()) {
                obj->Flush();
            }
        }

        // Mark the container with its URN
        AFF4ScopedPtr<AFF4Stream> desc = CreateMember(urn.Append(AFF4_CONTAINER_DESCRIPTION));

        if (!desc) {
            return IO_ERROR;
        }

        desc->Truncate();
        desc->Write(urn.SerializeToString());
        desc->Flush();

        // Update the resolver into the zip file.
        {
            AFF4ScopedPtr<ZipFileSegment> turtle_segment = CreateZipSegment("information.turtle");

            if (!turtle_segment) {
                return IO_ERROR;
            }

            turtle_segment->compression_method = ZIP_DEFLATE;

            // Overwrite the old turtle file with the newer data.
            turtle_segment->Truncate();
            resolver->DumpToTurtle(*turtle_segment, urn);
            turtle_segment->Flush();

#ifdef AFF4_HAS_LIBYAML_CPP
            AFF4ScopedPtr<ZipFileSegment> yaml_segment = CreateZipSegment(
                        "information.yaml");

            if (!yaml_segment) {
                return IO_ERROR;
            }

            yaml_segment->compression_method = ZIP_DEFLATE;
            resolver->DumpToYaml(*yaml_segment);
            yaml_segment->Flush();
#endif
        }

        AFF4ScopedPtr<AFF4Stream> backing_store = resolver->AFF4FactoryOpen<AFF4Stream>(backing_store_urn);

        if (!backing_store) {
            return IO_ERROR;
        }

        AFF4Status res = write_zip64_CD(*backing_store);
        if (res != STATUS_OK) {
            return res;
        }
    }

    return AFF4Volume::Flush();
}

/** This writes a zip64 end of central directory and a central
 directory locator */
AFF4Status ZipFile::write_zip64_CD(AFF4Stream& backing_store) {
    // We write to a memory stream first, and then copy it into the backing_store
    // at once. This really helps when we have lots of members in the zip archive.
    StringIO cd_stream;

    struct Zip64CDLocator locator;
    struct Zip64EndCD end_cd;
    struct EndCentralDirectory end;

    // Append a new central directory to the end of the zip file.
    if (backing_store.Seek(0, SEEK_END) != STATUS_OK) {
        LOG(ERROR)<< "Unable to write EOCD on non-seekable stream: " <<
                  urn.SerializeToString();
        return IO_ERROR;
    }

    // The real start of the ECD.
    aff4_off_t ecd_real_offset = backing_store.Tell();

    int total_entries = members.size();

    for (auto it = members.begin(); it != members.end(); it++) {
        ZipInfo* zip_info = it->second.get();
        LOG(INFO)<< "Writing CD entry for " << it->first.c_str();
        zip_info->WriteCDFileHeader(cd_stream);
    }

    locator.offset_of_end_cd = cd_stream.Tell() + ecd_real_offset - global_offset;

    end_cd.size_of_header = sizeof(end_cd) - 12;

    end_cd.number_of_entries_in_volume = total_entries;
    end_cd.number_of_entries_in_total = total_entries;
    end_cd.size_of_cd = cd_stream.Tell();
    end_cd.offset_of_cd = locator.offset_of_end_cd - end_cd.size_of_cd;

    end.total_entries_in_cd_on_disk = members.size();
    end.total_entries_in_cd = members.size();
    std::string urn_string = urn.SerializeToString();
    end.comment_len = urn_string.size();

    LOG(INFO)<< "Writing Zip64EndCD at " << std::hex <<
             cd_stream.Tell() + ecd_real_offset;

    cd_stream.Write(reinterpret_cast<char*>(&end_cd), sizeof(end_cd));
    cd_stream.Write(reinterpret_cast<char*>(&locator), sizeof(locator));

    LOG(INFO)<< "Writing ECD at " << std::hex <<
             cd_stream.Tell() + ecd_real_offset;

    cd_stream.Write(reinterpret_cast<char*>(&end), sizeof(end));
    cd_stream.Write(urn_string);

    // Now copy the cd_stream into the backing_store in one write operation.
    cd_stream.Seek(0, SEEK_SET);

    // Deliberately suppress the progress.
    ProgressContext progress;

    return cd_stream.CopyToStream(backing_store, cd_stream.Size(), &progress);
}

AFF4ScopedPtr<AFF4Stream> ZipFile::CreateMember(URN child) {
    // Create a zip member with a name relative to our volume URN. So for example,
    // say our volume URN is "aff4://e21659ea-c7d6-4f4d-8070-919178aa4c7b" and the
    // child is
    // "aff4://e21659ea-c7d6-4f4d-8070-919178aa4c7b/bin/ls/00000000.index" then
    // the zip member will be simply "/bin/ls/00000000.index".
    std::string member_filename = member_name_for_urn(child, urn, true);
    AFF4ScopedPtr<ZipFileSegment> result = CreateZipSegment(member_filename);

    return result.cast<AFF4Stream>();
}

AFF4ScopedPtr<ZipFileSegment> ZipFile::OpenZipSegment(std::string filename) {
    auto it = members.find(filename);
    if (it == members.end()) {
        return AFF4ScopedPtr<ZipFileSegment>();
    }

    // Is it already in the cache?
    URN segment_urn = urn_from_member_name(filename, urn);
    auto res = resolver->CacheGet<ZipFileSegment>(segment_urn);
    if (res.get()) {
        LOG(INFO)<< "Opening ZipFileSegment (cached) " <<
                 res->urn.SerializeToString();
        return res;
    }

    ZipFileSegment* result = new ZipFileSegment(filename, *this);

    LOG(INFO)<< "Opening ZipFileSegment " << result->urn.SerializeToString();

    return resolver->CachePut<ZipFileSegment>(result);
}

AFF4ScopedPtr<ZipFileSegment> ZipFile::CreateZipSegment(std::string filename) {
    MarkDirty();

    URN segment_urn = urn.Append(filename);

    // Is it in the cache?
    auto res = resolver->CacheGet<ZipFileSegment>(segment_urn);
    if (res.get()) {
        LOG(INFO)<< "Creating ZipFileSegment (cached) " <<
                 res->urn.SerializeToString();

        return res;
    }

    resolver->Set(segment_urn, AFF4_TYPE, new URN(AFF4_ZIP_SEGMENT_TYPE));
    resolver->Set(segment_urn, AFF4_STORED, new URN(urn));

    // Keep track of all the segments we issue.
    children.insert(segment_urn.SerializeToString());

    ZipFileSegment* result = new ZipFileSegment(filename, *this);
    result->Truncate();

    LOG(INFO)<< "Creating ZipFileSegment " << result->urn.SerializeToString();

    // Add the new object to the object cache.
    return resolver->CachePut<ZipFileSegment>(result);
}

ZipFileSegment::ZipFileSegment(DataStore* resolver) :
    StringIO(resolver) {
}

ZipFileSegment::ZipFileSegment(std::string filename, ZipFile& owner) {
    resolver = owner.resolver;
    owner_urn = owner.urn;
    urn = urn_from_member_name(filename, owner.urn);

    LoadFromZipFile(owner);
}

AFF4ScopedPtr<ZipFileSegment> ZipFileSegment::NewZipFileSegment(DataStore* resolver, const URN& segment_urn,
        const URN& volume_urn) {
    AFF4ScopedPtr<ZipFile> volume = resolver->AFF4FactoryOpen<ZipFile>(volume_urn);

    if (!volume) {
        return AFF4ScopedPtr<ZipFileSegment>();    /** Volume not known? */
    }

    std::string member_filename = member_name_for_urn(segment_urn, volume->urn, true);

    return volume->CreateZipSegment(member_filename);
}

AFF4Status ZipFileSegment::LoadFromZipFile(ZipFile& owner) {
    std::string member_name = member_name_for_urn(urn, owner.urn, true);

    // Parse the ZipFileHeader for this filename.
    auto it = owner.members.find(member_name);
    if (it == owner.members.end()) {
        // The owner does not have this file yet.
        return STATUS_OK;
    }

    // Just borrow the reference to the ZipInfo.
    ZipInfo* zip_info = it->second.get();
    ZipFileHeader file_header;
    uint32_t magic = file_header.magic;

    AFF4ScopedPtr<AFF4Stream> backing_store = resolver->AFF4FactoryOpen<AFF4Stream>(owner.backing_store_urn);

    if (!backing_store) {
        return IO_ERROR;
    }

    backing_store->Seek(zip_info->local_header_offset + owner.global_offset,
                        SEEK_SET);

    backing_store->ReadIntoBuffer(&file_header, sizeof(file_header));

    if (file_header.magic != magic || file_header.compression_method != zip_info->compression_method) {
        LOG(INFO)<< "Local file header invalid!";
        return PARSING_ERROR;
    }

    // The filename should be null terminated so we force c_str().
    std::string file_header_filename = backing_store->Read(file_header.file_name_length).c_str();
    if (file_header_filename != zip_info->filename) {
        LOG(INFO)<< "Local filename different from central directory.";
        return PARSING_ERROR;
    }

    backing_store->Seek(file_header.extra_field_len, SEEK_CUR);

    switch (file_header.compression_method) {
        // If the file is deflated we have no choice but to read it all into memory.
        case ZIP_DEFLATE: {
            unsigned int buffer_size = zip_info->file_size;
            std::unique_ptr<char[]> decomp_buffer(new char[buffer_size]);

            std::string c_buffer = backing_store->Read(zip_info->compress_size);

            if (DecompressBuffer(decomp_buffer.get(), buffer_size, c_buffer) != buffer_size) {
                LOG(INFO)<< "Unable to decompress file.";
                return PARSING_ERROR;
            }

            buffer.assign(decomp_buffer.get(), buffer_size);
        }
        break;

        case ZIP_STORED: {
            // If the file is just stored we only need to map a slice of it.
            _backing_store_urn = owner.backing_store_urn;
            _backing_store_start_offset = backing_store->Tell();
            _backing_store_length = zip_info->file_size;
        }
        break;

        default:
            LOG(INFO)<< "Unsupported compression method.";
            return NOT_IMPLEMENTED;
    }

    return STATUS_OK;
}

std::string ZipFileSegment::Read(size_t length) {
    if (_backing_store_start_offset < 0) {
        return StringIO::Read(length);
    }

    AFF4ScopedPtr<AFF4Stream> backing_store = resolver->AFF4FactoryOpen<AFF4Stream>(_backing_store_urn);
    if (!backing_store || readptr > _backing_store_length) {
        return "";
    }

    aff4_off_t offset = _backing_store_start_offset + readptr;
    size_t to_read = std::min((aff4_off_t) length, (aff4_off_t) _backing_store_length - readptr);

    backing_store->Seek(offset, SEEK_SET);

    std::string result = backing_store->Read(to_read);
    readptr += result.size();

    return result;
}

aff4_off_t ZipFileSegment::Size() {
    if (_backing_store_start_offset < 0) {
        return StringIO::Size();
    }

    return _backing_store_length;
}

AFF4Status ZipFileSegment::Truncate() {
    // Ensure we stop mapping the backing file.
    _backing_store_start_offset = -1;

    return StringIO::Truncate();
}

int ZipFileSegment::Write(const char* data, int length) {
    // The segment is mapped from the backing store and the user wants to modify
    // it. We need to make a local copy.
    if (_backing_store_start_offset > 0) {
        AFF4ScopedPtr<AFF4Stream> backing_store = resolver->AFF4FactoryOpen<AFF4Stream>(_backing_store_urn);

        if (!backing_store) {
            return 0;
        }

        backing_store->Seek(_backing_store_start_offset, SEEK_SET);
        buffer = backing_store->Read(_backing_store_length);
        _backing_store_length = -1;
    }

    return StringIO::Write(data, length);
}

AFF4Status ZipFileSegment::LoadFromURN() {
    if (resolver->Get(urn, AFF4_STORED, owner_urn) != STATUS_OK) {
        return NOT_FOUND;
    }

    AFF4ScopedPtr<ZipFile> owner = resolver->AFF4FactoryOpen<ZipFile>(owner_urn);

    if (!owner) {
        return IO_ERROR;
    }

    return LoadFromZipFile(*owner);
}

// In AFF4 we use smallish buffers, therefore we just do everything in memory.
static std::string CompressBuffer(const std::string& buffer) {
    z_stream strm;

    memset(&strm, 0, sizeof(strm));

    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(buffer.data()));
    strm.avail_in = buffer.size();

    if (deflateInit2(&strm, 9, Z_DEFLATED, -15,
                     9, Z_DEFAULT_STRATEGY) != Z_OK) {
        LOG(INFO)<< "Unable to initialise zlib (" << strm.msg << ")";
        return nullptr;
    }

    // Get an upper bound on the size of the compressed buffer.
    int buffer_size = deflateBound(&strm, buffer.size() + 10);
    std::unique_ptr<char[]> c_buffer(new char[buffer_size]);

    strm.next_out = reinterpret_cast<Bytef*>(c_buffer.get());
    strm.avail_out = buffer_size;

    if (deflate(&strm, Z_FINISH) != Z_STREAM_END) {
        deflateEnd(&strm);
        return nullptr;
    }

    deflateEnd(&strm);

    return std::string(c_buffer.get(), strm.total_out);
}

static unsigned int DecompressBuffer(char* buffer, int length, const std::string& c_buffer) {
    z_stream strm;

    memset(&strm, 0, sizeof(strm));

    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(c_buffer.data()));

    strm.avail_in = c_buffer.size();
    strm.next_out = reinterpret_cast<Bytef*>(buffer);
    strm.avail_out = length;

    if (inflateInit2(&strm, -15) != Z_OK) {
        LOG(ERROR)<< "Unable to initialise zlib (" << strm.msg << ")";
        return 0;
    }

    if (inflate(&strm, Z_FINISH) != Z_STREAM_END) {
        inflateEnd(&strm);
        return 0;
    }

    inflateEnd(&strm);

    return length - strm.avail_out;
}

AFF4Status ZipFileSegment::Flush() {
    if (IsDirty()) {
        AFF4ScopedPtr<ZipFile> owner = resolver->AFF4FactoryOpen<ZipFile>(owner_urn);

        if (!owner) {
            return GENERIC_ERROR;
        }

        AFF4ScopedPtr<AFF4Stream> backing_store = resolver->AFF4FactoryOpen<AFF4Stream>(owner->backing_store_urn);

        if (!backing_store) {
            return GENERIC_ERROR;
        }

        LOG(INFO)<< "Writing member " << urn.SerializeToString().c_str();
        std::unique_ptr<ZipInfo> zip_info(new ZipInfo());

        // Append member at the end of the file.
        if (backing_store->Seek(0, SEEK_END) != STATUS_OK) {
            return IO_ERROR;
        }

        // zip_info offsets are relative to the start of the zip file.
        zip_info->local_header_offset = (backing_store->Tell() - owner->global_offset);

        zip_info->filename = member_name_for_urn(urn, owner->urn, true);
        zip_info->file_size = Size();

        zip_info->crc32_cs = crc32(
                                 zip_info->crc32_cs,
                                 reinterpret_cast<Bytef*>(const_cast<char*>(buffer.data())),
                                 buffer.size());

        if (compression_method == AFF4_IMAGE_COMPRESSION_ENUM_DEFLATE) {
            std::string cdata = CompressBuffer(buffer);
            zip_info->compress_size = cdata.size();
            zip_info->compression_method = ZIP_DEFLATE;

            zip_info->WriteFileHeader(*backing_store);
            if (backing_store->Write(cdata) < 0) {
                return IO_ERROR;
            }

            // Compression method not known - ignore and store uncompressed.
        } else {
            zip_info->compress_size = buffer.size();

            zip_info->WriteFileHeader(*backing_store);
            if (backing_store->Write(buffer) < 0) {
                return IO_ERROR;
            }
        }

        // Replace ourselves in the members map.
        std::string member_name = member_name_for_urn(urn, owner->urn, true);
        owner->members[member_name] = std::move(zip_info);

        // Mark the owner as dirty.
        LOG(INFO)<< owner->urn.SerializeToString().c_str() <<
                 " is dirtied by segment " << urn.SerializeToString().c_str();

        owner->MarkDirty();
    }

    return AFF4Stream::Flush();
}

// Copy the stream into this new segment.
AFF4Status ZipFileSegment::WriteStream(AFF4Stream* source, ProgressContext* progress) {
    AFF4ScopedPtr<ZipFile> owner = resolver->AFF4FactoryOpen<ZipFile>(owner_urn);

    if (!owner) {
        LOG(ERROR)<< "Unable to open owner volume of ZipFileSegment " <<
                  urn.SerializeToString().c_str();
        return IO_ERROR;
    }

    return owner->StreamAddMember(urn, *source, compression_method, progress);
}

//-------------------------------------------------------------------------
// ZipInfo Class.
//-------------------------------------------------------------------------
ZipInfo::ZipInfo() {
    struct tm now;
    time_t epoch_time;

    epoch_time = time(nullptr);
    localtime_r(&epoch_time, &now);

    lastmoddate = (now.tm_year + 1900 - 1980) << 9 | (now.tm_mon + 1) << 5 | now.tm_mday;
    lastmodtime = now.tm_hour << 11 | now.tm_min << 5 | now.tm_sec / 2;

    file_header_offset = -1;

    // Initialize the crc32.
    crc32_cs = crc32(0L, Z_NULL, 0);
}

AFF4Status ZipInfo::WriteFileHeader(AFF4Stream& output) {
    // Remember where we wrote the file header.
    if (file_header_offset < 0) {
        file_header_offset = output.Tell();
    }

    struct ZipFileHeader header;
    struct Zip64FileHeaderExtensibleField zip64header;

    header.crc32_cs = crc32_cs;
    header.compress_size = -1;
    header.file_size = -1;
    header.file_name_length = filename.length();
    header.compression_method = compression_method;
    header.lastmodtime = lastmodtime;
    header.lastmoddate = lastmoddate;
    header.extra_field_len = sizeof(zip64header);

    output.Seek(file_header_offset, SEEK_SET);
    output.Write(reinterpret_cast<char*>(&header), sizeof(header));
    output.Write(filename);

    zip64header.file_size = file_size;
    zip64header.compress_size = compress_size;
    zip64header.relative_offset_local_header = local_header_offset;
    output.Write(reinterpret_cast<char*>(&zip64header), sizeof(zip64header));

    return STATUS_OK;
}

AFF4Status ZipInfo::WriteCDFileHeader(AFF4Stream& output) {
    struct CDFileHeader header;
    struct Zip64FileHeaderExtensibleField zip64header;

    header.compression_method = compression_method;
    header.crc32_cs = crc32_cs;
    header.file_name_length = filename.length();
    header.dostime = lastmodtime;
    header.dosdate = lastmoddate;
    header.extra_field_len = sizeof(zip64header);

    output.Write(reinterpret_cast<char*>(&header), sizeof(header));
    output.Write(filename);

    zip64header.file_size = file_size;
    zip64header.compress_size = compress_size;
    zip64header.relative_offset_local_header = local_header_offset;

    output.Write(reinterpret_cast<char*>(&zip64header), sizeof(zip64header));

    return STATUS_OK;
}

//-------------------------------------------------------------------------
// ZipFile Class.
//-------------------------------------------------------------------------
AFF4Status ZipFile::StreamAddMember(URN member_urn, AFF4Stream& stream, int compression_method,
                                    ProgressContext* progress) {
    if (!progress) {
        progress = &empty_progress;
    }

    if (resolver->Get(urn, AFF4_STORED, backing_store_urn) != STATUS_OK) {
        return NOT_FOUND;
    }

    MarkDirty();

    AFF4ScopedPtr<AFF4Stream> backing_store = resolver->AFF4FactoryOpen<AFF4Stream>(backing_store_urn);

    if (!backing_store) {
        LOG(ERROR)<< "Unable to open backing URN " <<
                  backing_store_urn.SerializeToString().c_str();
        return IO_ERROR;
    }

    LOG(INFO)<< "Writing member %s" << member_urn.SerializeToString().c_str();

    // Append member at the end of the file.
    backing_store->Seek(0, SEEK_END);

    // zip_info offsets are relative to the start of the zip file (take
    // global_offset into account).
    std::unique_ptr<ZipInfo> zip_info(new ZipInfo());
    zip_info->filename = member_name_for_urn(member_urn, urn, true);
    zip_info->local_header_offset = backing_store->Tell() - global_offset;

    // For now we do not support streamed writing so we need to seek back
    // to this position later with an updated crc32.
    if (zip_info->WriteFileHeader(*backing_store) < 0) {
        return IO_ERROR;
    }

    if (compression_method == AFF4_IMAGE_COMPRESSION_ENUM_DEFLATE) {
        zip_info->compression_method = ZIP_DEFLATE;

        z_stream strm;

        memset(&strm, 0, sizeof(strm));

        // Make some room for output buffer.
        std::unique_ptr<char[]> c_buffer(new char[AFF4_BUFF_SIZE]);

        strm.next_out = reinterpret_cast<Bytef*>(c_buffer.get());
        strm.avail_out = AFF4_BUFF_SIZE;

        if (deflateInit2(&strm, 9, Z_DEFLATED, -15,
                         9, Z_DEFAULT_STRATEGY) != Z_OK) {
            LOG(INFO)<< "Unable to initialise zlib (" << strm.msg << ")";
            return FATAL_ERROR;
        }

        while (1) {
            std::string buffer(stream.Read(AFF4_BUFF_SIZE));

            if (buffer.size() == 0) {
                break;
            }

            strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(buffer.data()));
            strm.avail_in = buffer.size();

            if (deflate(&strm, Z_PARTIAL_FLUSH) != Z_OK) {
                deflateEnd(&strm);
                return IO_ERROR;
            }

            int output_bytes = AFF4_BUFF_SIZE - strm.avail_out;
            zip_info->crc32_cs = crc32(
                                     zip_info->crc32_cs,
                                     reinterpret_cast<Bytef*>(const_cast<char*>(buffer.data())),
                                     buffer.size() - strm.avail_in);

            // Report progress.
            if (!progress->Report(stream.Tell())) {
                deflateEnd(&strm);
                return ABORTED;
            }

            if (backing_store->Write(c_buffer.get(), output_bytes) < 0) {
                return IO_ERROR;
            }

            // Give the compressor more room.
            strm.next_out = reinterpret_cast<Bytef*>(c_buffer.get());
            strm.avail_out = AFF4_BUFF_SIZE;
        }

        // Give the compressor more room.
        strm.next_out = reinterpret_cast<Bytef*>(c_buffer.get());
        strm.avail_out = AFF4_BUFF_SIZE;

        // Flush the compressor.
        if (deflate(&strm, Z_FINISH) != Z_STREAM_END) {
            deflateEnd(&strm);
            return GENERIC_ERROR;
        }

        zip_info->file_size = strm.total_in;
        zip_info->compress_size = strm.total_out;
        if (backing_store->Write(c_buffer.get(),
                                 AFF4_BUFF_SIZE - strm.avail_out) < 0) {
            return IO_ERROR;
        }

        deflateEnd(&strm);

        // Compression method not known - ignore and store uncompressed.
    } else {
        zip_info->compression_method = ZIP_STORED;

        while (1) {
            std::string buffer(stream.Read(AFF4_BUFF_SIZE));

            if (buffer.size() == 0) {
                break;
            }

            zip_info->compress_size += buffer.size();
            zip_info->file_size += buffer.size();
            zip_info->crc32_cs = crc32(
                                     zip_info->crc32_cs,
                                     reinterpret_cast<Bytef*>(const_cast<char*>(buffer.data())),
                                     buffer.size());

            // Report progress.
            if (!progress->Report(stream.Tell())) {
                return ABORTED;
            }

            if (backing_store->Write(buffer.data(), buffer.size()) < 0) {
                return IO_ERROR;
            }
        }
    }

    // Update the local file header now that CRC32 is calculated.
    if (zip_info->WriteFileHeader(*backing_store) < 0) {
        return IO_ERROR;
    }

    members[zip_info->filename] = std::move(zip_info);

    // Keep track of all the segments we issue.
    children.insert(member_urn.SerializeToString());

    return STATUS_OK;
}

// Register ZipFile as an AFF4 object.
static AFF4Registrar<ZipFile> r1(AFF4_ZIP_TYPE);
static AFF4Registrar<ZipFileSegment> r2(AFF4_ZIP_SEGMENT_TYPE);
