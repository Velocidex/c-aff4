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

#include "aff4/config.h"
#include "aff4/libaff4.h"

#include "aff4/aff4_directory.h"
#include "aff4/aff4_file.h"

#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace aff4 {

AFF4Status AFF4Directory::NewAFF4Directory(
    DataStore* resolver, std::string root_path,
    bool truncate,
    AFF4Flusher<AFF4Volume> &result) {
    AFF4Flusher<AFF4Directory> new_obj;
    RETURN_IF_ERROR(AFF4Directory::NewAFF4Directory(
                        resolver, root_path, truncate, new_obj));

    result = std::move(new_obj);

    return STATUS_OK;
}

AFF4Status AFF4Directory::NewAFF4Directory(
    DataStore* resolver, std::string root_path,
    bool truncate,
    AFF4Flusher<AFF4Directory> &result) {

    auto new_obj = make_flusher<AFF4Directory>(resolver);
    new_obj->root_path = root_path;

    // If mode is truncate we need to clear the directory.
    if (truncate) {
        RemoveDirectory(resolver, root_path.c_str());
    } else {
        // Try to read the existing turtle file.
        AFF4Flusher<FileBackedObject> turtle_stream;
        if (STATUS_OK == NewFileBackedObject(
                resolver,
                root_path + PATH_SEP_STR + AFF4_CONTAINER_INFO_TURTLE,
                "read", turtle_stream)) {
            resolver->LoadFromTurtle(*turtle_stream.get());
        }
    }

    struct stat s;
    if (stat(root_path.c_str(), &s) < 0) {
        // Path does not exist. Try to create it.
        RETURN_IF_ERROR(MkDir(resolver, root_path.c_str()));
    }

    resolver->Set(new_obj->urn, AFF4_TYPE, new URN(AFF4_DIRECTORY_TYPE),
                  /* replace= */ false);

    resolver->Set(new_obj->urn, AFF4_STORED, new URN(URN::NewURNFromFilename(root_path, true)));

    result = std::move(new_obj);

    return STATUS_OK;
}

AFF4Status AFF4Directory::CreateMemberStream(
    URN child, AFF4Flusher<AFF4Stream> &result) {

    // Check that child is a relative path in our URN.
    std::string relative_path = urn.RelativePath(child);
    if (relative_path == child.SerializeToString()) {
        resolver->logger->warn("Can not create URN {} not inside directory {}",
                               child, urn);
        return INVALID_INPUT;
    }

    // Use this filename. Note that since filesystems cannot typically represent
    // files and directories as the same path component we cannot allow slashes
    // in the filename. Otherwise we will fail to create e.g. stream/0000000 and
    // stream/0000000.index.
    std::string filename = member_name_for_urn(child, urn, false);

    // We are allowed to create any files inside the directory volume.
    resolver->Set(child, AFF4_TYPE, new URN(AFF4_FILE_TYPE));
    resolver->Set(child, AFF4_STORED, new URN(urn));
    resolver->Set(child, AFF4_DIRECTORY_CHILD_FILENAME, new XSDString(filename));

    // Store the member inside our storage location.
    std::string full_path = root_path + PATH_SEP_STR + filename;
    std::vector<std::string> directory_components = break_path_into_components(full_path);
    directory_components.pop_back();

    CreateIntermediateDirectories(resolver, directory_components);

    AFF4Flusher<FileBackedObject> child_fd;
    RETURN_IF_ERROR(NewFileBackedObject(resolver, full_path, "truncate", child_fd));
    child_fd->urn = child;

    MarkDirty();

    result = std::move(child_fd);

    return STATUS_OK;
}


AFF4Status AFF4Directory::OpenMemberStream(
    URN child, AFF4Flusher<AFF4Stream> &result) {

    // Check that child is a relative path in our URN.
    std::string relative_path = urn.RelativePath(child);
    if (relative_path == child.SerializeToString()) {
        resolver->logger->warn("Can not read URN {} not inside directory {}",
                               child, urn);
        return INVALID_INPUT;
    }

    // Use this filename. Note that since filesystems cannot typically represent
    // files and directories as the same path component we cannot allow slashes
    // in the filename. Otherwise we will fail to create e.g. stream/0000000 and
    // stream/0000000.index.
    std::string filename = member_name_for_urn(child, urn, false);

    AFF4Flusher<FileBackedObject> child_fd;
    RETURN_IF_ERROR(NewFileBackedObject(
                        resolver,
                        root_path + PATH_SEP_STR + filename,
                        "read", child_fd));

    child_fd->urn = child;

    result = std::move(child_fd);

    return STATUS_OK;
}

AFF4Status AFF4Directory::OpenAFF4Directory(
    DataStore *resolver, std::string dirname,
    AFF4Flusher<AFF4Directory> &result) {

    auto new_obj = make_flusher<AFF4Directory>(resolver);
    new_obj->root_path = dirname;

    AFF4Flusher<FileBackedObject> desc;
    RETURN_IF_ERROR(
        NewFileBackedObject(
            resolver,
            dirname + PATH_SEP_STR + AFF4_CONTAINER_DESCRIPTION,
            "read", desc));

    new_obj->urn = URN(desc->Read(10000));

    AFF4Flusher<FileBackedObject> turtle_stream;
    RETURN_IF_ERROR(NewFileBackedObject(
                        resolver,
                        dirname + PATH_SEP_STR + AFF4_CONTAINER_INFO_TURTLE,
                        "read", turtle_stream));

    result = std::move(new_obj);

    return resolver->LoadFromTurtle(*turtle_stream);
}


AFF4Status AFF4Directory::Flush() {
    if (IsDirty()) {
        AFF4Flusher<FileBackedObject> desc;
        RETURN_IF_ERROR(
            NewFileBackedObject(
                resolver,
                root_path + PATH_SEP_STR + AFF4_CONTAINER_DESCRIPTION,
                "truncate", desc));

        std::string urn_str = urn.SerializeToString();
        desc->Write(urn_str.data(), urn_str.size());

        AFF4Flusher<FileBackedObject> turtle_stream;
        RETURN_IF_ERROR(
            NewFileBackedObject(
                resolver,
                root_path + PATH_SEP_STR + AFF4_CONTAINER_INFO_TURTLE,
                "truncate", turtle_stream));

        resolver->DumpToTurtle(*turtle_stream, urn);
    }

    return STATUS_OK;
}


bool AFF4Directory::IsDirectory(const URN& urn, bool must_exist) {
    std::string filename = urn.ToFilename();
    return AFF4Directory::IsDirectory(filename, must_exist);
}

#ifdef _WIN32

// Recursively deletes the specified directory and all its contents
//   path: Absolute path of the directory that will be deleted

//   The path must not be terminated with a path separator.
AFF4Status AFF4Directory::RemoveDirectory(DataStore *resolver,
                                          const std::string& path) {
    WIN32_FIND_DATA ffd;
    std::string search_str = path + PATH_SEP_STR + "*";
    HANDLE hFind = INVALID_HANDLE_VALUE;

    hFind = FindFirstFile(search_str.c_str(), &ffd);
    if (INVALID_HANDLE_VALUE == hFind) {
        return IO_ERROR;
    }

    // List all the files in the directory with some info about them.
    do {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (ffd.cFileName[0] == '.') {
                continue;
            }

            // Recurse into the subdir.
            AFF4Status result = AFF4Directory::RemoveDirectory(
                resolver, path + PATH_SEP_STR + ffd.cFileName);
            if (result != STATUS_OK) {
                return result;
            }
        } else {
            std::string filename = path + PATH_SEP_STR + ffd.cFileName;
            resolver->logger->info("Deleting file {}", filename);
            if (!::DeleteFile(filename.c_str())) {
                resolver->logger->error("Failed: {}", GetLastErrorMessage());
                return IO_ERROR;
            }
        }
    } while (FindNextFile(hFind, &ffd) != 0);

    resolver->logger->info("Deleting directory {}", path);
    if (!::RemoveDirectory(path.c_str())) {
        resolver->logger->error("Failed: {}", GetLastErrorMessage());
        FindClose(hFind);
        return IO_ERROR;
    }

    FindClose(hFind);
    return STATUS_OK;
}

AFF4Status AFF4Directory::MkDir(DataStore* resolver, const std::string& path) {
    resolver->logger->info("MkDir {}", path);

    if (!CreateDirectory(path.c_str(), nullptr)) {
        DWORD res = GetLastError();
        if (res == ERROR_ALREADY_EXISTS) {
            return STATUS_OK;
        }

        resolver->logger->error("Cant create directory {}: {}", path,
                                GetLastErrorMessage());
        return IO_ERROR;
    }

    return STATUS_OK;
}

bool AFF4Directory::IsDirectory(const std::string& filename,
                                bool must_exist) {
    DWORD dwAttrib = GetFileAttributes(filename.c_str());

    bool result = (dwAttrib != INVALID_FILE_ATTRIBUTES &&
                   (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));

    // If the URN ends with a / and we have permissions to create it, it is a
    // directory.
    if (!must_exist) {
        char last = *(filename.rbegin());
        result |= (last == '/' || last == '\\');
    }

    return result;
}

#else

AFF4Status AFF4Directory::MkDir(DataStore *resolver, const std::string& path) {
    if (mkdir(path.c_str(), 0777) < 0) {
        resolver->logger->error("Failed to create directory {}: {}", path,
                                GetLastErrorMessage());
        return IO_ERROR;
    }
    return STATUS_OK;
}

bool AFF4Directory::IsDirectory(const std::string& filename,
                                bool must_exist) {
    DIR* dir = opendir(filename.c_str());
    if (!dir) {
        // If the URN ends with a / and we have permissions to create it, it is a
        // directory.
        if (!must_exist && *(filename.rbegin()) == PATH_SEP) {
            return true;
        }

        return false;
    }

    closedir(dir);
    return true;
}


// Recursively remove all files and subdirectories in the directory.
AFF4Status AFF4Directory::RemoveDirectory(DataStore *resolver, const std::string& path) {
    DIR* dir;
    std::string dirname = path;

    if (*dirname.rbegin() != PATH_SEP) {
        dirname += PATH_SEP_STR;
    }

    /* Open directory stream */
    dir = opendir(dirname.c_str());
    if (dir != nullptr) {
        struct dirent* ent;

        /* Print all files and directories within the directory */
        while ((ent = readdir(dir)) != nullptr) {
            if (ent->d_name[0] == '.') {
                continue;
            }

            std::string full_path = dirname + ent->d_name;

            switch (ent->d_type) {
                case DT_REG: {
                    resolver->logger->info("Removing file {}", full_path);
                    unlink(full_path.c_str());
                }
                break;

                case DT_DIR: {
                    AFF4Status result = AFF4Directory::RemoveDirectory(resolver, full_path);
                    resolver->logger->info("Removing directory {}", full_path);
                    rmdir(full_path.c_str());
                    closedir(dir);
                    return result;
                }
                break;
                default:
                    break;
            }
        }
        closedir(dir);
    }

    return STATUS_OK;
}

#endif

} // namespace aff4
