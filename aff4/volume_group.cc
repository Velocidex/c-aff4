#include <set>

#include "aff4/volume_group.h"

#include "aff4/aff4_errors.h"
#include "aff4/data_store.h"
#include "aff4/aff4_image.h"
#include "aff4/aff4_map.h"
#include "aff4/aff4_file.h"
#include "aff4/aff4_directory.h"
#include "aff4/zip.h"
#include "aff4/aff4_symstream.h"
#include <stdlib.h>
#include <limits.h>

#ifndef _WIN32
#include <unistd.h>
#include <dirent.h>
#endif

namespace aff4 {

    void VolumeGroup::AddVolume(AFF4Flusher<AFF4Volume> &&volume) {
        /*
         *  If the volume is a ZipFile, add the filename to the search path.
         */
        AFF4Volume* vol = volume.get();
        if (ZipFile * v = dynamic_cast<ZipFile*> (vol)) {
            AFF4Stream* stream = v->backing_stream.get();
            if (FileBackedObject * f = dynamic_cast<FileBackedObject*> (stream)) {
                std::string filename = f->filename;
                AddSearchPath(filename);
            }
        }
        volume_objs.insert(std::make_pair(volume->urn, std::move(volume)));
    }

    void VolumeGroup::AddSearchPath(std::string path) {
        // Convert to absolute.
#ifdef _WIN32
        char resolved[_PATH_MAX + 1];
        memset(&resolved, 0, _PATH_MAX + 1);
        if (_fullpath((char*)&resolved, path.c_str(), _PATH_MAX) != NULL) {
            path = std::string(resolved);
        }
#else 
        char resolved[PATH_MAX + 1];
        memset(&resolved, 0, PATH_MAX + 1);
        if (realpath(path.c_str(), (char*)&resolved) != NULL) {
            path = std::string(resolved);
        }
#endif
        if (AFF4Directory::IsDirectory(path, /* must_exist= */ true)) {
            searchPaths.insert(path);
        } else {
            // get the parent path.
            path = path.substr(0, path.find_last_of("/\\"));
            searchPaths.insert(path);
        }
    }

    void VolumeGroup::RemoveSearchPath(std::string path) {
        searchPaths.erase(path);
    }

    // Construct the appropriate stream and return it.

    AFF4Status VolumeGroup::GetStream(URN stream_urn, AFF4Flusher<AFF4Stream> &result) {
        // Get all the type attrbutes of the URN.
        std::vector<std::shared_ptr < RDFValue>> types;
        if (STATUS_OK == resolver->Get(stream_urn, AFF4_TYPE, types)) {
            for (auto &type : types) {
                std::string type_str(type->SerializeToString());

                if (type_str == AFF4_IMAGESTREAM_TYPE ||
                        type_str == AFF4_LEGACY_IMAGESTREAM_TYPE) {
                    AFF4Flusher<AFF4Image> image_stream;
                    RETURN_IF_ERROR(
                            AFF4Image::OpenAFF4Image(
                            resolver, stream_urn, this, image_stream));

                    result.reset(image_stream.release());

                    resolver->logger->debug("Openning {} as type {}",
                            stream_urn, type_str);
                    return STATUS_OK;
                }

                // The AFF4 Standard specifies an "AFF4 Image" as an abstract
                // container for image related properties. It is not actually a
                // concrete stream but it refers to a storage stream using its
                // aff4:dataStream property.

                // Note that to create such a stream, you can simply create a
                // regular stream with NewAFF4Image or NewAFF4Map and then set
                // the aff4:dataStream of a new object to a concerete Map or
                // ImageStream.
                if (type_str == AFF4_IMAGE_TYPE ||
                        type_str == AFF4_DISK_IMAGE_TYPE ||
                        type_str == AFF4_VOLUME_IMAGE_TYPE ||
                        type_str == AFF4_MEMORY_IMAGE_TYPE ||
                        type_str == AFF4_CONTIGUOUS_IMAGE_TYPE ||
                        type_str == AFF4_DISCONTIGUOUS_IMAGE_TYPE) {
                    URN delegate;

                    if (STATUS_OK == resolver->Get(stream_urn, AFF4_DATASTREAM, delegate)) {
                        // TODO: This can get recursive. Protect against abuse.
                        return GetStream(delegate, result);
                    }
                }

                if (type_str == AFF4_MAP_TYPE) {
                    AFF4Flusher<AFF4Map> map_stream;
                    RETURN_IF_ERROR(
                            AFF4Map::OpenAFF4Map(
                            resolver, stream_urn, this, map_stream));

                    result.reset(map_stream.release());
                    resolver->logger->debug("Openning {} as type {}",
                            stream_urn, type_str);

                    return STATUS_OK;
                }

                // Zip segments are stored directly in each volume. We use
                // the resolver to figure out which volume has each
                // segment.
                if (type_str == AFF4_ZIP_SEGMENT_TYPE ||
                        type_str == AFF4_FILE_TYPE) {
                    URN owner;
                    RETURN_IF_ERROR(resolver->Get(stream_urn, AFF4_STORED, owner));

                    resolver->logger->debug("Openning {} as type {}", stream_urn, type_str);
                    auto it = volume_objs.find(owner);
                    if (it != volume_objs.end()) {
                        return (it->second->OpenMemberStream(stream_urn, result));
                    }
                }
            }
        }

        // Handle symbolic streams now.
        if (stream_urn == AFF4_IMAGESTREAM_ZERO) {
            result.reset(new AFF4SymbolicStream(resolver, stream_urn, 0));
            return STATUS_OK;
        }
        if (stream_urn == AFF4_IMAGESTREAM_FF) {
            result.reset(new AFF4SymbolicStream(resolver, stream_urn, 0xff));
            return STATUS_OK;
        }
        if (stream_urn == AFF4_IMAGESTREAM_UNKNOWN) {
            result.reset(new AFF4SymbolicStream(resolver, stream_urn, "UNKNOWN"));
            return STATUS_OK;
        }
        if (stream_urn == AFF4_IMAGESTREAM_UNREADABLE) {
            result.reset(new AFF4SymbolicStream(resolver, stream_urn, "UNREADABLEDATA"));
            return STATUS_OK;
        }

        for (int i = 0; i < 256; i++) {
            std::string urn = aff4_sprintf(
                    "%s%02X", AFF4_IMAGESTREAM_SYMBOLIC_PREFIX, i);

            if (stream_urn == urn) {
                result.reset(new AFF4SymbolicStream(resolver, stream_urn, i));
                return STATUS_OK;
            }
        }

        return NOT_FOUND;
    }

    AFF4Status VolumeGroup::LocateAndAdd(URN& urn) {
        if (volume_objs.find(urn) != volume_objs.end()) {
            // Already loaded....
            return STATUS_OK;
        }

        // Check known volume URNs.
        if (foundVolumes.find(urn) != foundVolumes.end()) {

            std::string volume_to_load = foundVolumes.find(urn)->second;
            // load and return;
            AFF4Flusher<FileBackedObject> backing_stream;
            RETURN_IF_ERROR(NewFileBackedObject(
                    resolver, volume_to_load,
                    "read", backing_stream));

            AFF4Flusher<ZipFile> volume;
            RETURN_IF_ERROR(ZipFile::OpenZipFile(
                    resolver,
                    std::move(AFF4Flusher<AFF4Stream>(
                    backing_stream.release())),
                    volume));

            AddVolume(std::move(AFF4Flusher<AFF4Volume>(volume.release())));
            return STATUS_OK;
        }

        // Search for container.
        resolver->logger->info("Searching for container {}", urn);
        for (std::string path : searchPaths) {
            /* 
             * Look for all AFF4 in this path, and check it's URN.
             */
            ScanForAFF4Volumes(path);
        }
        // Check known volume URNs.
        if (foundVolumes.find(urn) != foundVolumes.end()) {

            std::string volume_to_load = foundVolumes.find(urn)->second;
            
            resolver->logger->info("Searching for container {} = {}", urn, volume_to_load);
            
            // load and return;
            AFF4Flusher<FileBackedObject> backing_stream;
            RETURN_IF_ERROR(NewFileBackedObject(
                    resolver, volume_to_load,
                    "read", backing_stream));

            AFF4Flusher<ZipFile> volume;
            RETURN_IF_ERROR(ZipFile::OpenZipFile(
                    resolver,
                    std::move(AFF4Flusher<AFF4Stream>(
                    backing_stream.release())),
                    volume));

            AddVolume(std::move(AFF4Flusher<AFF4Volume>(volume.release())));
            return STATUS_OK;
        }

        return NOT_FOUND;
    }

    bool VolumeGroup::FoundVolumesContains(const std::string& filename) {
	for (auto it = foundVolumes.begin(); it != foundVolumes.end(); it++) {
		if (it->second.compare(filename) == 0) {
			return true;
		}
	}
	return false;
}

    void VolumeGroup::ScanForAFF4Volumes(const std::string& path) {
        // It is expected that the map is LOCKED prior to this call.
        if (path.empty()) {
            return;
        }
        resolver->logger->info("Scanning path {}", path);
#ifdef _WIN32
        /*
         * Windows based systems
         */
        std::wstring wpath = s2ws(path);

        std::wstring pattern(wpath);
        pattern.append(L"\\*");

        WIN32_FIND_DATAW data;
        HANDLE hFind;
        if ((hFind = FindFirstFile(pattern.c_str(), &data)) != INVALID_HANDLE_VALUE) {
            do {
                std::wstring filename(data.cFileName);
                if (filename.compare(L".") && filename.compare(L"..")) {
                    std::string absoluteFilename = path + "\\" + ws2s(filename);
                    if ((IsFile(absoluteFilename)) && (IsAFF4Container(ws2s(filename)))) {
                        if (!FoundVolumesContains(absoluteFilename)) {
                            // We don't have this file
                            std::string resID = aff4::ZipFile::GetResourceID(absoluteFilename, resolver->logger);
                            if (!resID.empty()) {
                                foundVolumes[resID] = absoluteFilename;
                            }
                        }
                    }
                }
                while (FindNextFile(hFind, &data) != 0);
                FindClose(hFind);
            }
#else
        /*
         * POSIX based systems.
         */
        DIR* dirp = ::opendir(path.c_str());
        struct dirent * dp;
        while ((dp = readdir(dirp)) != NULL) {
            std::string filename(dp->d_name);
            if (filename.compare(".") && filename.compare("..")) {
                std::string absoluteFilename = path + "/" + filename;
                if ((IsFile(absoluteFilename)) && (IsAFF4Container(filename))) {
                    if (!FoundVolumesContains(absoluteFilename)) {
                        // We don't have this file
                        std::string resID = aff4::ZipFile::GetResourceID(absoluteFilename, resolver->logger);
                        if (!resID.empty()) {
                            foundVolumes[resID] = absoluteFilename;
                        }
                    }
                }
            }
        }
        closedir(dirp);
#endif
        }

    } // namespace aff4
